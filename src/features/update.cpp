#include "features/update.h"
#include "core/version.h"
#include "core/state.h"
#include "core/logger.h"
#include "core/utils.h"
#include "core/settings.h"
#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>
#include <bcrypt.h>
#include <cstdio>
#include <string>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "user32.lib")   // MessageBoxA; the build links it anyway, the tests do not

// ---- update check: startup query for newest release, with direct download and install ----
// Queries GitHub releases for the latest version tag and asset. If a newer release exists,
// it can download texoverride.asi directly, verify the PE binary and its SHA-256, and swap it
// into place so it applies on the next FiveM restart.
// Fails silently when offline. Skipped only when the plugin is off. Since 0.8.26 there is no
// way to silence it: a new version always produces a message box, whether it was installed on
// its own (auto_update = yes) or is being offered.
// Runs on its own thread so popups/network calls never block the streaming engine.

static const char* kReleasesPage = "https://github.com/blancodagoat/texoverride/releases";

int verCmp(const char* a, const char* b)   // >0 when a is newer than b
{
    int A[3] = {}, B[3] = {};
    sscanf_s(a, "%d.%d.%d", &A[0], &A[1], &A[2]);
    sscanf_s(b, "%d.%d.%d", &B[0], &B[1], &B[2]);
    for (int i = 0; i < 3; ++i) if (A[i] != B[i]) return A[i] - B[i];
    return 0;
}

// One WinHTTP handle for one scope, so no early return can leak one.
struct Inet {
    HINTERNET h;
    explicit Inet(HINTERNET x) : h(x) {}
    ~Inet() { if (h) WinHttpCloseHandle(h); }
    Inet(const Inet&) = delete;
    Inet& operator=(const Inet&) = delete;
    explicit operator bool() const { return h != nullptr; }
};

// https only. WinHTTP follows the release's redirect to GitHub's download host on its own, and its
// default policy refuses to follow one from https down to http, which is the policy wanted here.
static bool parseHttpsUrl(const std::string& url, std::wstring& host, std::wstring& path)
{
    if (url.compare(0, 8, "https://") != 0) return false;
    std::string u = url.substr(8);
    size_t slash = u.find('/');
    host = widen(slash == std::string::npos ? u : u.substr(0, slash));
    path = widen(slash == std::string::npos ? "/" : u.substr(slash));
    return !host.empty();
}

static bool downloadHttpsFile(const std::string& url, const std::string& destPath)
{
    std::wstring host, path;
    if (!parseHttpsUrl(url, host, path)) {
        LOG_ERROR(LogCategory::Update, "Refusing download URL that is not https: %s", url.c_str());
        return false;
    }
    Inet s(WinHttpOpen(L"texoverride", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!s) { LOG_ERROR(LogCategory::Update, "WinHttpOpen failed (err %lu)", GetLastError()); return false; }
    WinHttpSetTimeouts(s.h, 10000, 10000, 15000, 30000);
    Inet c(WinHttpConnect(s.h, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!c) { LOG_ERROR(LogCategory::Update, "WinHttpConnect to %s failed (err %lu)", narrow(host.c_str()).c_str(), GetLastError()); return false; }
    Inet r(WinHttpOpenRequest(c.h, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!r) { LOG_ERROR(LogCategory::Update, "WinHttpOpenRequest failed (err %lu)", GetLastError()); return false; }
    if (!WinHttpSendRequest(r.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) || !WinHttpReceiveResponse(r.h, nullptr)) {
        LOG_ERROR(LogCategory::Update, "WinHttp request/response failed (err %lu)", GetLastError());
        return false;
    }
    DWORD statusCode = 0, statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(r.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);
    if (statusCode != 200) {
        LOG_ERROR(LogCategory::Update, "Download HTTP status %lu (expected 200)", statusCode);
        return false;
    }

    HANDLE hFile = CreateFileW(widen(destPath).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        LOG_ERROR(LogCategory::Update, "Cannot create destination file %s (err %lu)", destPath.c_str(), GetLastError());
        return false;
    }
    char buf[8192];
    DWORD got = 0, totalWritten = 0;
    bool ok = true;
    for (;;) {
        if (!WinHttpReadData(r.h, buf, sizeof(buf), &got)) { ok = false; break; }   // a cut connection, not the end
        if (!got) break;
        DWORD written = 0;
        if (!WriteFile(hFile, buf, got, &written, nullptr) || written != got) { ok = false; break; }
        totalWritten += written;
    }
    CloseHandle(hFile);
    if (!ok) {
        LOG_ERROR(LogCategory::Update, "Download of the update was cut short (err %lu)", GetLastError());
        deleteFile(destPath);
        return false;
    }
    LOG_INFO(LogCategory::Update, "Downloaded %lu bytes to %s", totalWritten, destPath.c_str());
    return true;
}

static bool verifyPeFile(const std::string& path)
{
    HANDLE h = CreateFileW(widen(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        LOG_WARN(LogCategory::Update, "PE verify: cannot open %s (err %lu)", path.c_str(), GetLastError());
        return false;
    }
    IMAGE_DOS_HEADER dos = {};
    IMAGE_NT_HEADERS64 nt = {};
    LARGE_INTEGER size = {};
    DWORD read = 0;
    const char* why = nullptr;
    if (!GetFileSizeEx(h, &size) || size.QuadPart < 30000 || size.QuadPart > 50 * 1024 * 1024) why = "invalid file size";
    else if (!ReadFile(h, &dos, sizeof(dos), &read, nullptr) || read != sizeof(dos)) why = "cannot read DOS header";
    else if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 || dos.e_lfanew > 4096) why = "invalid DOS signature or e_lfanew";
    else if (SetFilePointer(h, dos.e_lfanew, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER) why = "cannot seek to NT headers";
    else if (!ReadFile(h, &nt, sizeof(nt), &read, nullptr) || read != sizeof(nt)) why = "cannot read NT headers";
    else if (nt.Signature != IMAGE_NT_SIGNATURE) why = "invalid NT signature";
    else if (nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) why = "not an x64 PE";
    else if (!(nt.FileHeader.Characteristics & IMAGE_FILE_DLL)) why = "not a DLL/ASI";
    CloseHandle(h);
    if (why) LOG_WARN(LogCategory::Update, "PE verify: %s (%lld bytes)", why, size.QuadPart);
    return why == nullptr;
}

// SHA-256 of a file, lowercase hex.
static std::string sha256File(const std::string& path)
{
    std::string hex;
    HANDLE h = CreateFileW(widen(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return hex;
    BCRYPT_ALG_HANDLE alg = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
        BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
        char buf[8192]; DWORD got = 0; bool ok = true;
        while (ReadFile(h, buf, sizeof buf, &got, nullptr) && got > 0)
            if (BCryptHashData(hash, (PUCHAR)buf, got, 0) != 0) { ok = false; break; }
        UCHAR out[32];
        if (ok && BCryptFinishHash(hash, out, sizeof out, 0) == 0) {
            char tmp[65];
            for (int i = 0; i < 32; ++i) _snprintf_s(tmp + i * 2, 3, _TRUNCATE, "%02x", out[i]);
            hex = tmp;
        }
    }
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(h);
    return hex;
}

static bool installUpdate(const std::string& downloadUrl, const std::string& latest, const std::string& expectHash)
{
    std::string asiPath = modulePath(g_self);
    if (asiPath.empty()) {
        LOG_ERROR(LogCategory::Update, "Cannot determine own module file name (err %lu)", GetLastError());
        return false;
    }
    std::string oldPath = asiPath + ".old";
    std::string dlPath  = asiPath + ".download";

    LOG_INFO(LogCategory::Update, "Downloading update %s from %s", latest.c_str(), downloadUrl.c_str());
    if (!downloadHttpsFile(downloadUrl, dlPath)) {
        LOG_ERROR(LogCategory::Update, "Failed to download update %s", latest.c_str());
        return false;
    }
    if (!verifyPeFile(dlPath)) {
        LOG_ERROR(LogCategory::Update, "Downloaded update %s failed PE binary validation; aborting install", latest.c_str());
        deleteFile(dlPath);
        return false;
    }
    std::string got = sha256File(dlPath);
    if (got != expectHash) {
        LOG_ERROR(LogCategory::Update, "Downloaded update %s does not match the SHA-256 GitHub published for it (got %s, expected %s); not installed",
                  latest.c_str(), got.c_str(), expectHash.c_str());
        deleteFile(dlPath);
        return false;
    }

    // The loaded .asi can be renamed but not overwritten: move it aside, then the new one in.
    deleteFile(oldPath);
    std::wstring wAsi = widen(asiPath), wOld = widen(oldPath), wDl = widen(dlPath);
    if (!MoveFileExW(wAsi.c_str(), wOld.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        LOG_ERROR(LogCategory::Update, "Failed to rename current plugin to .old (err %lu)", GetLastError());
        deleteFile(dlPath);
        return false;
    }
    if (!MoveFileExW(wDl.c_str(), wAsi.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        LOG_ERROR(LogCategory::Update, "Failed to move downloaded file to target .asi (err %lu); rolling back", GetLastError());
        MoveFileExW(wOld.c_str(), wAsi.c_str(), MOVEFILE_REPLACE_EXISTING);
        deleteFile(dlPath);
        return false;
    }
    LOG_INFO(LogCategory::Update, "Update %s installed; takes effect on next FiveM restart. The old version is kept beside it as texoverride.asi.old", latest.c_str());
    return true;
}

// The string value that follows `key` ("key": "value"), or "".
static std::string jsonString(const std::string& body, size_t keyAt)
{
    size_t colon = body.find(':', keyAt);
    size_t q1 = colon == std::string::npos ? colon : body.find('"', colon + 1);
    size_t q2 = q1 == std::string::npos ? q1 : body.find('"', q1 + 1);
    return q2 == std::string::npos ? std::string() : body.substr(q1 + 1, q2 - q1 - 1);
}

static bool isLowerHex64(const std::string& s)
{
    return s.size() == 64 && s.find_first_not_of("0123456789abcdef") == std::string::npos;
}

Release parseRelease(const std::string& body)
{
    Release rel;
    size_t k = body.find("\"tag_name\"");
    if (k != std::string::npos) rel.tag = jsonString(body, k);
    if (rel.tag.empty()) return rel;

    // the asset's own download URL if it is listed, otherwise the release's standard one
    size_t assetPos = body.find("\"name\":\"texoverride.asi\"");
    if (assetPos == std::string::npos) assetPos = body.find("\"name\": \"texoverride.asi\"");
    // Forward from the name only: GitHub emits browser_download_url last in each asset. This used
    // to start 200 bytes BEFORE the name, and CI uploads the .zip first, so the .zip's URL ends
    // about 250 bytes back: one longer field away from downloading the zip as the plugin.
    if (assetPos != std::string::npos) {
        size_t urlAt = body.find("\"browser_download_url\"", assetPos);
        if (urlAt != std::string::npos) rel.url = jsonString(body, urlAt);
    }
    if (rel.url.empty()) rel.url = std::string(kReleasesPage) + "/download/" + rel.tag + "/texoverride.asi";

    // Where the expected SHA-256 comes from. GitHub publishes it on the asset itself
    // ("digest":"sha256:...", right after the asset's name), and that is the source of truth: it
    // cannot be edited, and it cannot be lost. The release notes carry the same hash from CI as
    // "<64 hex> *texoverride.asi", but the notes are prose, and re-uploading them by hand after a
    // wording fix drops the CI footer with the hash in it. That happened to 0.8.14 and it left
    // every 0.8.13 install unable to update itself. Notes are now only the fallback.
    if (assetPos != std::string::npos) {
        // Bounded by this asset's own browser_download_url, which GitHub emits after digest, so
        // the hash can never be read off the .zip entry sitting next to it.
        size_t end = body.find("\"browser_download_url\"", assetPos);
        size_t sha = body.find("\"digest\":\"sha256:", assetPos);
        if (sha != std::string::npos && sha < end && sha + 17 + 64 <= body.size()) {
            std::string hx = body.substr(sha + 17, 64);
            if (isLowerHex64(hx)) rel.sha = hx;
        }
    }
    if (rel.sha.empty()) {
        size_t m = body.find(" *texoverride.asi");   // the notes also say **texoverride.asi** in prose
        if (m != std::string::npos && m >= 64) {
            std::string hx = body.substr(m - 64, 64);
            if (isLowerHex64(hx)) rel.sha = hx;
        }
    }
    return rel;
}

static std::string fetchLatestRelease()
{
    std::string body;
    Inet s(WinHttpOpen(L"texoverride", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!s) return body;
    WinHttpSetTimeouts(s.h, 5000, 5000, 5000, 5000);
    Inet c(WinHttpConnect(s.h, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!c) return body;
    Inet r(WinHttpOpenRequest(c.h, L"GET", L"/repos/blancodagoat/texoverride/releases/latest",
                              nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (r && WinHttpSendRequest(r.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0)
          && WinHttpReceiveResponse(r.h, nullptr)) {
        char buf[4096]; DWORD got = 0;
        while (WinHttpReadData(r.h, buf, sizeof buf, &got) && got) body.append(buf, got);
    }
    return body;
}

static void openReleasesPage()
{
    ShellExecuteA(nullptr, "open", kReleasesPage, nullptr, nullptr, SW_SHOWNORMAL);
}

static void updateCheck()
{
    Release rel = parseRelease(fetchLatestRelease());
    if (rel.tag.empty()) { LOG_DEBUG(LogCategory::Update, "Update check: could not reach GitHub (offline?)"); return; }
    const char* lv = (rel.tag[0] == 'v' || rel.tag[0] == 'V') ? rel.tag.c_str() + 1 : rel.tag.c_str();
    if (verCmp(lv, TEXOVERRIDE_VERSION) <= 0) {
        LOG_INFO(LogCategory::Update, "Plugin is up to date (latest %s)", rel.tag.c_str());
        return;
    }

    LOG_INFO(LogCategory::Update, "Update available: %s (current: " TEXOVERRIDE_VERSION ")", rel.tag.c_str());
    if (rel.sha.empty()) {
        LOG_WARN(LogCategory::Update, "Release carries no SHA-256 for texoverride.asi; auto-install skipped, opening the release page instead");
        char msg[256];
        _snprintf_s(msg, sizeof msg, _TRUNCATE,
            "A newer texoverride is out: %s (you have " TEXOVERRIDE_VERSION ").\n\nOpen the download page now?", rel.tag.c_str());
        if (MessageBoxA(nullptr, msg, "texoverride update", MB_YESNO | MB_ICONINFORMATION | MB_SETFOREGROUND | MB_TOPMOST) == IDYES)
            openReleasesPage();
        return;
    }

    bool autoUpdate = g_set.autoUpdate;
    if (!autoUpdate) {
        char msg[512];
        _snprintf_s(msg, sizeof msg, _TRUNCATE,
            "A newer texoverride is out: %s (you have " TEXOVERRIDE_VERSION ").\n\n"
            "Download and install this update automatically?\n"
            "(The update will take effect on your next FiveM restart)\n\n"
            "- Yes: Download and install automatically\n"
            "- No: Open the release page in your browser\n"
            "- Cancel: Skip for now\n\n"
            "Set auto_update = yes in tex_overrides\\_settings.txt to install new versions without asking.",
            rel.tag.c_str());
        int choice = MessageBoxA(nullptr, msg, "texoverride update", MB_YESNOCANCEL | MB_ICONINFORMATION | MB_SETFOREGROUND | MB_TOPMOST);
        if (choice == IDNO) { openReleasesPage(); return; }
        if (choice != IDYES) { LOG_INFO(LogCategory::Update, "Update %s deferred by user", rel.tag.c_str()); return; }
    }

    // Both outcomes are announced whichever way the install was decided. An update that lands
    // in silence is how "I never turned updates on" reports start.
    if (installUpdate(rel.url, rel.tag, rel.sha)) {
        char doneMsg[320];
        _snprintf_s(doneMsg, sizeof doneMsg, _TRUNCATE,
            "texoverride %s has been installed%s.\n\n"
            "It will take effect the next time you start FiveM. The version you had is kept beside it as texoverride.asi.old.",
            rel.tag.c_str(), autoUpdate ? " (auto_update is on in _settings.txt)" : "");
        MessageBoxA(nullptr, doneMsg, "texoverride update", MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND | MB_TOPMOST);
    } else {
        char failMsg[320];
        _snprintf_s(failMsg, sizeof failMsg, _TRUNCATE,
            "Failed to install texoverride %s automatically (see texoverride.log for details).\n\n"
            "Would you like to open the release page in your browser instead?", rel.tag.c_str());
        if (MessageBoxA(nullptr, failMsg, "texoverride update", MB_YESNO | MB_ICONWARNING | MB_SETFOREGROUND | MB_TOPMOST) == IDYES)
            openReleasesPage();
    }
}

DWORD WINAPI UpdateCheck(LPVOID)
{
    if (g_off) return 0;
    // A thread that lets an exception out takes the whole process with it (std::terminate).
    try { updateCheck(); }
    catch (...) { LOG_ERROR(LogCategory::Update, "Update check stopped (out of memory)"); }
    return 0;
}
