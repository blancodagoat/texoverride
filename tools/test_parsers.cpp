// The hand-written parsers and pure functions: verCmp, the GitHub release scrape (the hash check
// on a self-updating binary hangs off it), RSC7 page-flag sizes, joaat, the overlays.xml reader,
// the collection and type classifiers, and the UTF-8 path layer with a real non-ASCII folder.
// Build with tools\run_tests.bat.
#include "features/update.h"
#include "features/placement.h"
#include "streaming/rsc.h"
#include "streaming/gate.h"
#include "core/utils.h"
#include <windows.h>
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

static void releases()
{
    assert(verCmp("0.8.29", "0.8.28") > 0);
    assert(verCmp("0.8.28", "0.8.28") == 0);
    assert(verCmp("0.8.3", "0.8.28") < 0);        // numeric order, not string order
    assert(verCmp("0.9.0", "0.8.99") > 0);
    assert(verCmp("1.0", "0.9.9") > 0);           // a missing patch reads as 0

    const std::string asiSha(64, 'a'), zipSha(64, 'b');
    // shaped like the real answer: the .zip is uploaded first, URL last in each asset
    std::string body =
        "{\"tag_name\":\"v0.8.29\",\"assets\":["
        "{\"url\":\"https://api.github.com/repos/o/r/releases/assets/1\",\"name\":\"texoverride.zip\","
        "\"digest\":\"sha256:" + zipSha + "\",\"browser_download_url\":\"https://x/texoverride.zip\"},"
        "{\"url\":\"https://api.github.com/repos/o/r/releases/assets/2\",\"name\":\"texoverride.asi\","
        "\"digest\":\"sha256:" + asiSha + "\",\"browser_download_url\":\"https://x/texoverride.asi\"}]}";
    Release r = parseRelease(body);
    assert(r.tag == "v0.8.29");
    assert(r.url == "https://x/texoverride.asi");   // never the zip sitting in front of it
    assert(r.sha == asiSha);

    // no digest on the asset: the CI footer in the notes is the fallback
    r = parseRelease("{\"tag_name\":\"v0.8.30\",\"body\":\"" + asiSha + " *texoverride.asi\"}");
    assert(r.sha == asiSha);
    assert(r.url == "https://github.com/blancodagoat/texoverride/releases/download/v0.8.30/texoverride.asi");

    // the zip's digest must never be taken for the asi's
    r = parseRelease("{\"tag_name\":\"v1.0.0\",\"assets\":[{\"name\":\"texoverride.asi\",\"browser_download_url\":\"https://x/a\"},"
                     "{\"name\":\"texoverride.zip\",\"digest\":\"sha256:" + zipSha + "\"}]}");
    assert(r.sha.empty());

    // uppercase or short hex is not what GitHub emits: no hash, so nothing installs
    r = parseRelease("{\"tag_name\":\"v1.0.0\",\"body\":\"" + std::string(64, 'A') + " *texoverride.asi\"}");
    assert(r.sha.empty());

    assert(parseRelease("").tag.empty());
    assert(parseRelease("<html>rate limited</html>").tag.empty());
}

static void rsc()
{
    assert(rscSizeFromFlags(0) == 0);
    assert(rscSizeFromFlags(1u << 27) == 0x200);              // one base page
    assert(rscSizeFromFlags((1u << 27) | 1) == 0x400);        // the shift doubles the page
    assert(rscSizeFromFlags(3u << 5) == 384ull * 0x200);      // 3 pages of 128
    // the biggest field at the biggest shift is past 4 GB, and must not wrap
    assert(rscSizeFromFlags(0xFu | (0x7Fu << 17)) == (0x200ull << 15) * (0x7Full << 4));
}

static void hashes()
{
    assert(joaat("adder", 5) == 0xB779A091);   // the vehicle model hash every trainer knows
    assert(joaat("ADDER", 5) == 0xB779A091);   // case-insensitive
    assert(joaat("", 0) == 0);
}

static void classifiers()
{
    assert(std::string(classifyCollection("mp_m_freemode_01")) == "Freemode Male");
    assert(std::string(classifyCollection("mp_f_freemode_01_mp_f_gtawclothes1")) == "Freemode Female DLC");
    assert(std::string(classifyCollection("a_c_shepherd")) == "Animal Ped");
    assert(std::string(classifyCollection("cs_amandatownley")) == "Story/Ambient Ped");
    assert(std::string(classifyCollection("caninesd")) == "Custom Server Ped");

    assert(isIgnoredType("shop_tattoo.meta", "shop_tattoo.meta", false));
    assert(isIgnoredType("a_c_husky.ymt", "a_c_husky.ymt", false));
    assert(!isIgnoredType("a_c_husky.ymt", "pack/a_c_husky.ymt", false));   // only at the root
    assert(!isIgnoredType("head_000_r.ydd", "mp_m_freemode_01/head_000_r.ydd", false));

    assert(extOf("mp_m_freemode_01/feet_007_u.YDD") == "ydd");
    assert(extOf("noext").empty() && extOf("trailingdot.").empty() && extOf(nullptr).empty());
    assert(trim("  a b \r\n") == "a b" && trim(" \t").empty());
}

static std::string tempDir()
{
    wchar_t w[MAX_PATH]; GetTempPathW(MAX_PATH, w);
    return narrow(w);
}

static void placementXml()
{
    std::string path = tempDir() + "texo_test_overlays.xml";
    FILE* f = openFile(path, "wb");
    assert(f);
    fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<PedDecorationCollection>\n <presets>\n"
          "  <Item>\n   <uvPos x=\"0.25\" y=\"0.5\" />\n   <scale x=\"0.75\" y=\"1.5\" />\n"
          "   <rotation value=\"-30.0\" />\n   <nameHash>TAT_BB_000_M</nameHash>\n  </Item>\n"
          "  <Item>\n   <uvPos x=\"0.1\" y=\"0.2\" />\n   <nameHash>NO_ROTATION_SO_SKIPPED</nameHash>\n  </Item>\n"
          " </presets>\n <nameHash>mpbeach_overlays</nameHash>\n</PedDecorationCollection>\n", f);
    fclose(f);

    std::vector<PlColl> out;
    parsePlacementXml(path, "texo_test_overlays.xml", out);
    deleteFile(path);
    assert(out.size() == 1);
    assert(out[0].name == "mpbeach_overlays");
    assert(out[0].hash == joaat("mpbeach_overlays", 16));
    assert(out[0].presets.size() == 1);   // the item missing a field is dropped, not half read
    const PlPreset& p = out[0].presets[0];
    assert(p.hash == joaat("TAT_BB_000_M", 12));
    assert(p.v[0] == 0.25f && p.v[1] == 0.5f && p.v[2] == 0.75f && p.v[3] == 1.5f && p.v[4] == -30.0f);

    out.clear();
    parsePlacementXml(tempDir() + "texo_test_missing.xml", "texo_test_missing.xml", out);
    assert(out.empty());
}

// The bug the UTF-8 layer exists for: a folder name outside every ANSI code page at once.
static void unicodePaths()
{
    // "Иван_özge" in UTF-8: Cyrillic and Latin-1 together, so no single code page holds it
    const std::string name = "\xD0\x98\xD0\xB2\xD0\xB0\xD0\xBD_\xC3\xB6zge";
    assert(narrow(widen(name).c_str()) == name);

    const std::string base = tempDir() + "texo_" + name + "\\";
    CreateDirectoryW(widen(base).c_str(), nullptr);
    const std::string file = base + "head_000_r.ydd";
    FILE* f = openFile(file, "wb");
    assert(f);
    fputs("RSC7", f);
    fclose(f);
    assert(fileExists(file));

    int seen = 0;
    walkTree(base, "", [&](const WalkItem& it) { if (!it.dir && it.name == "head_000_r.ydd") ++seen; return true; });
    assert(seen == 1);

    // a nest deeper than the walk will go: it stops instead of following it forever
    std::string deep = base;
    for (int i = 0; i < kMaxWalkDepth + 3; ++i) { deep += "d\\"; CreateDirectoryW(widen(deep).c_str(), nullptr); }
    int dirs = 0;
    walkTree(base, "", [&](const WalkItem& it) { if (it.dir) ++dirs; return true; });
    assert(dirs == kMaxWalkDepth);

    for (int i = kMaxWalkDepth + 3; i > 0; --i) { RemoveDirectoryW(widen(deep).c_str()); deep.resize(deep.size() - 2); }
    assert(deleteFile(file) && !fileExists(file));
    RemoveDirectoryW(widen(base).c_str());
}

int main()
{
    releases();
    rsc();
    hashes();
    classifiers();
    placementXml();
    unicodePaths();
    puts("parsers: 6 groups passed");
    return 0;
}
