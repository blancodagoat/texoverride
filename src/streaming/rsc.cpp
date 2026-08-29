#include "streaming/rsc.h"
#include "core/logger.h"
#include "core/utils.h"
#include <windows.h>
#include <cstdio>

// ---- streaming-cost audit ------------------------------------------------------------------
// A .ytd/.ydd on disk is an RSC7 resource: dwords 2 (virtual) and 3 (physical) of the 16-byte
// header encode the exact memory the streamer charges while the file is resident. Decode is
// CodeWalker's GetSizeFromFlags (RpfFile.cs). Physical is the texture-budget hit — "texture
// loss" on heavy servers is that budget running dry, so the scan totals what the pack costs
// and names the heavy files. Threshold: 8 MB catches any 4K texture and 2K uncompressed;
// vanilla clothing txds sit well under 2 MB.
uint64_t rscSizeFromFlags(uint32_t f)
{
    // 64-bit throughout: with the max page shift the 32-bit product wraps at 4 GB, and a
    // corrupt header could then report a tiny size and go unreported as huge
    uint64_t pages = ((f >> 27) & 0x1)
                   + (((f >> 26) & 0x1)  << 1)
                   + (((f >> 25) & 0x1)  << 2)
                   + (((f >> 24) & 0x1)  << 3)
                   + (((f >> 17) & 0x7F) << 4)
                   + (((f >> 11) & 0x3F) << 5)
                   + (((f >> 7)  & 0xF)  << 6)
                   + (((f >> 5)  & 0x3)  << 7)
                   + (((f >> 4)  & 0x1)  << 8);
    return (0x200ull << (f & 0xF)) * pages;
}

bool rscCost(const char* path, uint64_t* virt, uint64_t* phys)
{
    FILE* fp = nullptr;
    if (fopen_s(&fp, path, "rb") || !fp) return false;
    uint32_t h[4] = {};
    size_t got = fread(h, sizeof(uint32_t), 4, fp);
    fclose(fp);
    if (got != 4 || h[0] != 0x37435352) return false;   // 'RSC7'
    *virt = rscSizeFromFlags(h[2]);
    *phys = rscSizeFromFlags(h[3]);
    return true;
}

Cost readCost(const char* path)
{
    Cost c = { 0, 0, true, true };
    if (rscCost(path, &c.cv, &c.cp)) return c;
    c.rsc = false;
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fad)) { c.readable = false; return c; }
    c.cp = (((uint64_t)fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;   // on-disk stand-in
    return c;
}

bool cannotLoad(const char* key, const Cost& c, bool quiet)
{
    if (!c.readable) {
        if (!quiet) LOG_WARN(LogCategory::Scan, "UNREADABLE %s - cannot open it; not loaded this launch", key);
        return true;
    }
    // A .ytd/.ydd/.yft/.ydr/.ycd the game can read starts with 'RSC7'. Without it the file is a
    // raw export (CodeWalker's ExtractFile, a renamed dds) and the game dies in its own loader with
    // "Invalid fixup, address is neither virtual nor physical" the moment it streams it in.
    if (!c.rsc && !hasExt(key, ".ymt")) {
        if (!quiet) LOG_WARN(LogCategory::Scan, "NOT A GAME FILE %s - no RSC7 header (export it with OpenIV, not a raw dump); not loaded", key);
        return true;
    }
    uint64_t worst = (c.cv > c.cp) ? c.cv : c.cp;
    if (worst > (32ull << 20) && !quiet)
        LOG_WARN(LogCategory::Scan, "HUGE  %s - %.1f MB of %s data (loaded; if crashing, shrink with CodeWalker)",
                 key, worst / 1048576.0, (c.cv > c.cp) ? "mesh" : "texture");
    return false;
}

bool cannotLoadPath(const char* path, const char* key, bool quiet)
{
    return cannotLoad(key, readCost(path), quiet);
}

// The header reads ARE the cost of a big pack: one file open each, every one of them a disk
// seek and an antivirus look. They do not touch each other, so spread them over a few threads.
// This may only ever run OFF the loader lock (see scanFinish): a thread created inside DllMain
// cannot start until that lock is free, so joining one there deadlocks the launch. 0.7.3 did
// exactly that and hung on "Launching FiveM".
struct CostJob { std::vector<Cand>* v; volatile LONG next; };
static DWORD WINAPI costWorker(LPVOID p)
{
    CostJob* j = (CostJob*)p;
    for (;;) {
        LONG i = InterlockedIncrement(&j->next) - 1;
        if (i < 0 || (size_t)i >= j->v->size()) return 0;
        (*j->v)[i].c = readCost((*j->v)[i].full.c_str());
    }
}

void readCosts(std::vector<Cand>& v)
{
    if (v.empty()) return;
    SYSTEM_INFO si; GetSystemInfo(&si);
    unsigned want = si.dwNumberOfProcessors;
    if (want < 1) want = 1;
    if (want > 8) want = 8;
    if (want > v.size()) want = (unsigned)v.size();
    CostJob job = { &v, 0 };
    HANDLE th[8]; unsigned made = 0;
    for (unsigned i = 0; i + 1 < want; ++i) {   // this thread is one of the workers
        HANDLE t = CreateThread(nullptr, 0, costWorker, &job, 0, nullptr);
        if (!t) break;
        th[made++] = t;
    }
    costWorker(&job);
    for (unsigned i = 0; i < made; ++i) { WaitForSingleObject(th[i], INFINITE); CloseHandle(th[i]); }
}
