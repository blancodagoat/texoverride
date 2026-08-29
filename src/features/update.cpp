#include "features/update.h"
#include "core/version.h"
#include "core/state.h"
#include "core/logger.h"
#include "core/utils.h"
#include "core/settings.h"
#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>
#include <cstdio>
#include <string>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")

// ---- update check: startup query for newest release, with direct download and install ----
// Queries GitHub releases for the latest version tag and asset. If a newer release exists,
// it can download texoverride.asi directly, verify the PE binary, and swap it into place
// so it applies on the next FiveM restart.
// Fails silently when offline. Skipped when _OFF or _NO_UPDATE_CHECK exists in tex_overrides.
// Runs on its own thread so popups/network calls never block the streaming engine.

int verCmp(const char* a, const char* b)   // >0 when a is newer than b
{
    int A[3] = {}, B[3] = {};
    sscanf_s(a, "%d.%d.%d", &A[0], &A[1], &A[2]);
    sscanf_s(b, "%d.%d.%d", &B[0], &B[1], &B[2]);
    for (int i = 0; i < 3; ++i) if (A[i] != B[i]) return A[i] - B[i];
    return 0;
}

static bool parseHttpsUrl(const std::string& url, std::wstring& host, std::wstring& path, INTERNET_PORT& port)
{
    std::string u = url;
    if (u.find("https://") == 0) {
        u = u.substr(8);
        port = INTERNET_DEFAULT_HTTPS_PORT;
    } else if (u.find("http://") == 0) {
        u = u.substr(7);
        port = INTERNET_DEFAULT_HTTP_PORT;
    } else {
        return false;
    }
    size_t slash = u.find('/');
    std::string h = (slash == std::string::npos) ? u : u.substr(0, slash);
    std::string p = (slash == std::string::npos) ? "/" : u.substr(slash);

    int hLen = MultiByteToWideChar(CP_UTF8, 0, h.c_str(), -1, nullptr, 0);
    int pLen = MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, nullptr, 0);
    if (hLen <= 1 || pLen <= 1) return false;

    host.resize(hLen - 1);
    path.resize(pLen - 1);
    MultiByteToWideChar(CP_UTF8, 0, h.c_str(), -1, &host[0], hLen);
    MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, &path[0], pLen);
    return true;
}

static bool downloadHttpsFile(const std::string& initialUrl, const std::string& destPath)
{
    std::string currentUrl = initialUrl;
    for (int redirects = 0; redirects < 10; ++redirects) {
        std::wstring host, path;
        INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
        if (!parseHttpsUrl(currentUrl, host, path, port)) {
            LOG_ERROR(LogCategory::Update, "Failed to parse download URL: %s", currentUrl.c_str());
            return false;
        }

        HINTERNET s = WinHttpOpen(L"texoverride", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!s) {
            LOG_ERROR(LogCategory::Update, "WinHttpOpen failed (err %lu)", GetLastError());
            return false;
        }
        WinHttpSetTimeouts(s, 10000, 10000, 15000, 30000);

        HINTERNET c = WinHttpConnect(s, host.c_str(), port, 0);
        if (!c) {
            LOG_ERROR(LogCategory::Update, "WinHttpConnect to %S failed (err %lu)", host.c_str(), GetLastError());
            WinHttpCloseHandle(s);
            return false;
        }

        HINTERNET r = WinHttpOpenRequest(c, L"GET", path.c_str(), nullptr,
                                         WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         (port == INTERNET_DEFAULT_HTTPS_PORT ? WINHTTP_FLAG_SECURE : 0));
        if (!r) {
            LOG_ERROR(LogCategory::Update, "WinHttpOpenRequest failed (err %lu)", GetLastError());
            WinHttpCloseHandle(c);
            WinHttpCloseHandle(s);
            return false;
        }

        DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
        WinHttpSetOption(r, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

        if (!WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) ||
            !WinHttpReceiveResponse(r, nullptr)) {
            LOG_ERROR(LogCategory::Update, "WinHttp request/response failed (err %lu)", GetLastError());
            WinHttpCloseHandle(r);
            WinHttpCloseHandle(c);
            WinHttpCloseHandle(s);
            return false;
        }

        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);

        if (statusCode == 301 || statusCode == 302 || statusCode == 303 || statusCode == 307 || statusCode == 308) {
            DWORD locLen = 0;
            WinHttpQueryHeaders(r, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                                WINHTTP_NO_OUTPUT_BUFFER, &locLen, WINHTTP_NO_HEADER_INDEX);
            if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && locLen > 0) {
                std::wstring loc(locLen / sizeof(wchar_t), L'\0');
                if (WinHttpQueryHeaders(r, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                                        &loc[0], &locLen, WINHTTP_NO_HEADER_INDEX)) {
                    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, loc.c_str(), -1, nullptr, 0, nullptr, nullptr);
                    if (utf8Len > 1) {
                        currentUrl.resize(utf8Len - 1);
                        WideCharToMultiByte(CP_UTF8, 0, loc.c_str(), -1, &currentUrl[0], utf8Len, nullptr, nullptr);
                        WinHttpCloseHandle(r);
                        WinHttpCloseHandle(c);
                        WinHttpCloseHandle(s);
                        continue;
                    }
                }
            }
        }

        if (statusCode != 200) {
            LOG_ERROR(LogCategory::Update, "Download HTTP status %lu (expected 200)", statusCode);
            WinHttpCloseHandle(r);
            WinHttpCloseHandle(c);
            WinHttpCloseHandle(s);
            return false;
        }

        HANDLE hFile = CreateFileA(destPath.c_str(), GENERIC_WRITE, 0, nullptr,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) {
            LOG_ERROR(LogCategory::Update, "Cannot create destination file %s (err %lu)", destPath.c_str(), GetLastError());
            WinHttpCloseHandle(r);
            WinHttpCloseHandle(c);
            WinHttpCloseHandle(s);
            return false;
        }

        char buf[8192];
        DWORD got = 0;
        DWORD totalWritten = 0;
        bool writeErr = false;
        while (WinHttpReadData(r, buf, sizeof(buf), &got) && got > 0) {
            DWORD written = 0;
            if (!WriteFile(hFile, buf, got, &written, nullptr) || written != got) {
                writeErr = true;
                break;
            }
            totalWritten += written;
        }

        CloseHandle(hFile);
        WinHttpCloseHandle(r);
        WinHttpCloseHandle(c);
        WinHttpCloseHandle(s);

        if (writeErr) {
            LOG_ERROR(LogCategory::Update, "File write error while downloading update");
            DeleteFileA(destPath.c_str());
            return false;
        }

        LOG_INFO(LogCategory::Update, "Downloaded %lu bytes to %s", totalWritten, destPath.c_str());
        return true;
    }

    LOG_ERROR(LogCategory::Update, "Too many redirects while downloading update");
    return false;
}

static bool verifyPeFile(const std::string& path)
{
    HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        LOG_WARN(LogCategory::Update, "PE verify: cannot open %s (err %lu)", path.c_str(), GetLastError());
        return false;
    }

    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(h, &size) || size.QuadPart < 30000 || size.QuadPart > 50 * 1024 * 1024) {
        LOG_WARN(LogCategory::Update, "PE verify: invalid file size (%lld bytes)", size.QuadPart);
        CloseHandle(h);
        return false;
    }

    IMAGE_DOS_HEADER dos = {};
    DWORD read = 0;
    if (!ReadFile(h, &dos, sizeof(dos), &read, nullptr) || read != sizeof(dos)) {
        LOG_WARN(LogCategory::Update, "PE verify: cannot read DOS header");
        CloseHandle(h);
        return false;
    }
    if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 || dos.e_lfanew > 4096) {
        LOG_WARN(LogCategory::Update, "PE verify: invalid DOS signature or e_lfanew (%ld)", dos.e_lfanew);
        CloseHandle(h);
        return false;
    }

    if (SetFilePointer(h, dos.e_lfanew, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
        LOG_WARN(LogCategory::Update, "PE verify: SetFilePointer failed (err %lu)", GetLastError());
        CloseHandle(h);
        return false;
    }

    IMAGE_NT_HEADERS64 nt = {};
    if (!ReadFile(h, &nt, sizeof(nt), &read, nullptr) || read != sizeof(nt)) {
        LOG_WARN(LogCategory::Update, "PE verify: cannot read NT headers");
        CloseHandle(h);
        return false;
    }

    CloseHandle(h);

    if (nt.Signature != IMAGE_NT_SIGNATURE) {
        LOG_WARN(LogCategory::Update, "PE verify: invalid NT signature (0x%08X)", nt.Signature);
        return false;
    }
    if (nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) {
        LOG_WARN(LogCategory::Update, "PE verify: not an x64 PE (machine 0x%04X)", nt.FileHeader.Machine);
        return false;
    }
    if (!(nt.FileHeader.Characteristics & IMAGE_FILE_DLL)) {
        LOG_WARN(LogCategory::Update, "PE verify: not a DLL/ASI");
        return false;
    }

    return true;
}

#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")

// SHA-256 of a file, lowercase hex. CI prints the hash of every release asset in the release
// notes, and the notes come back in the same API answer as the tag, so the check costs nothing.
static std::string sha256File(const std::string& path)
{
    std::string hex;
    HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
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
    char selfPath[MAX_PATH];
    if (!GetModuleFileNameA(g_self, selfPath, MAX_PATH)) {
        LOG_ERROR(LogCategory::Update, "Cannot determine own module file name (err %lu)", GetLastError());
        return false;
    }

    std::string asiPath = selfPath;
    std::string oldPath = asiPath + ".old";
    std::string dlPath  = asiPath + ".download";

    LOG_INFO(LogCategory::Update, "Downloading update %s from %s", latest.c_str(), downloadUrl.c_str());
    if (!downloadHttpsFile(downloadUrl, dlPath)) {
        LOG_ERROR(LogCategory::Update, "Failed to download update %s", latest.c_str());
        return false;
    }

    if (!verifyPeFile(dlPath)) {
        LOG_ERROR(LogCategory::Update, "Downloaded update %s failed PE binary validation; aborting install", latest.c_str());
        DeleteFileA(dlPath.c_str());
        return false;
    }
    std::string got = sha256File(dlPath);
    if (got != expectHash) {
        LOG_ERROR(LogCategory::Update, "Downloaded update %s does not match the SHA-256 in the release notes (got %s, expected %s); not installed",
                  latest.c_str(), got.c_str(), expectHash.c_str());
        DeleteFileA(dlPath.c_str());
        return false;
    }

    // Clean up any existing .old file before renaming
    DeleteFileA(oldPath.c_str());

    // Rename the current loaded .asi to .old
    if (!MoveFileExA(asiPath.c_str(), oldPath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DWORD err = GetLastError();
        LOG_ERROR(LogCategory::Update, "Failed to rename current plugin to .old (err %lu)", err);
        DeleteFileA(dlPath.c_str());
        return false;
    }

    // Move the downloaded .download file into place as .asi
    if (!MoveFileExA(dlPath.c_str(), asiPath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DWORD err = GetLastError();
        LOG_ERROR(LogCategory::Update, "Failed to move downloaded file to target .asi (err %lu); rolling back", err);
        MoveFileExA(oldPath.c_str(), asiPath.c_str(), MOVEFILE_REPLACE_EXISTING);
        DeleteFileA(dlPath.c_str());
        return false;
    }

    LOG_INFO(LogCategory::Update, "Update %s installed; takes effect on next FiveM restart. The old version is kept beside it as texoverride.asi.old", latest.c_str());
    return true;
}

DWORD WINAPI UpdateCheck(LPVOID)
{
    if (g_off) return 0;
    if (g_set.noUpdateCheck) return 0;

    std::string body;
    HINTERNET s = WinHttpOpen(L"texoverride", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return 0;
    WinHttpSetTimeouts(s, 5000, 5000, 5000, 5000);
    HINTERNET c = WinHttpConnect(s, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", L"/repos/blancodagoat/texoverride/releases/latest",
                                         nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         WINHTTP_FLAG_SECURE) : nullptr;
    if (r && WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0)
          && WinHttpReceiveResponse(r, nullptr)) {
        char buf[4096]; DWORD got = 0;
        while (WinHttpReadData(r, buf, sizeof buf, &got) && got) body.append(buf, got);
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);

    // pull the version out of "tag_name":"v0.3.0" without a JSON library
    std::string latest;
    size_t k = body.find("\"tag_name\"");
    if (k != std::string::npos) {
        size_t q1 = body.find('"', body.find(':', k) + 1);
        size_t q2 = (q1 == std::string::npos) ? std::string::npos : body.find('"', q1 + 1);
        if (q2 != std::string::npos) latest = body.substr(q1 + 1, q2 - q1 - 1);
    }
    if (latest.empty()) { LOG_DEBUG(LogCategory::Update, "Update check: could not reach GitHub (offline?)"); return 0; }
    const char* lv = (latest[0] == 'v' || latest[0] == 'V') ? latest.c_str() + 1 : latest.c_str();

    // extract asset download URL for texoverride.asi if present, otherwise build release download URL
    std::string downloadUrl;
    size_t assetPos = body.find("\"name\":\"texoverride.asi\"");
    if (assetPos == std::string::npos) assetPos = body.find("\"name\": \"texoverride.asi\"");
    if (assetPos != std::string::npos) {
        size_t bPos = body.find("\"browser_download_url\"", assetPos > 200 ? assetPos - 200 : 0);
        if (bPos == std::string::npos || bPos > assetPos + 400) {
            bPos = body.find("\"browser_download_url\"", assetPos);
        }
        if (bPos != std::string::npos) {
            size_t q1 = body.find('"', body.find(':', bPos) + 1);
            size_t q2 = (q1 == std::string::npos) ? std::string::npos : body.find('"', q1 + 1);
            if (q2 != std::string::npos) downloadUrl = body.substr(q1 + 1, q2 - q1 - 1);
        }
    }
    if (downloadUrl.empty()) {
        downloadUrl = "https://github.com/blancodagoat/texoverride/releases/download/" + latest + "/texoverride.asi";
    }

    // Where the expected SHA-256 comes from. GitHub publishes it on the asset itself
    // ("digest":"sha256:...", right after the asset's name), and that is the source of truth: it
    // cannot be edited, and it cannot be lost. The release notes carry the same hash from CI as
    // "<64 hex> *texoverride.asi", but the notes are prose, and re-uploading them by hand after a
    // wording fix drops the CI footer with the hash in it. That happened to 0.8.14 and it left
    // every 0.8.13 install unable to update itself. Notes are now only the fallback.
    std::string expectHash;
    if (assetPos != std::string::npos) {
        // Bounded by this asset's own browser_download_url, which GitHub emits after digest, so
        // the hash can never be read off the .zip entry sitting next to it.
        size_t end = body.find("\"browser_download_url\"", assetPos);
        size_t sha = body.find("\"digest\":\"sha256:", assetPos);
        if (sha != std::string::npos && sha < end && sha + 17 + 64 <= body.size()) {
            std::string hx = body.substr(sha + 17, 64);
            if (hx.find_first_not_of("0123456789abcdef") == std::string::npos) expectHash = hx;
        }
    }
    if (expectHash.empty())
    { size_t m = body.find(" *texoverride.asi");   // the notes also say **texoverride.asi** in prose
      if (m != std::string::npos && m >= 64) {
          std::string hx = body.substr(m - 64, 64);
          if (hx.find_first_not_of("0123456789abcdef") == std::string::npos) expectHash = hx;
      } }

    if (verCmp(lv, TEXOVERRIDE_VERSION) > 0) {
        LOG_INFO(LogCategory::Update, "Update available: %s (current: " TEXOVERRIDE_VERSION ")", latest.c_str());
        if (expectHash.empty()) {
            LOG_WARN(LogCategory::Update, "Release notes carry no SHA-256 for texoverride.asi; auto-install skipped, opening the release page instead");
            char msg[256];
            _snprintf_s(msg, sizeof msg, _TRUNCATE,
                "A newer texoverride is out: %s (you have " TEXOVERRIDE_VERSION ").\n\nOpen the download page now?", latest.c_str());
            if (MessageBoxA(nullptr, msg, "texoverride update", MB_YESNO | MB_ICONINFORMATION | MB_SETFOREGROUND | MB_TOPMOST) == IDYES)
                ShellExecuteA(nullptr, "open", "https://github.com/blancodagoat/texoverride/releases", nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }

        bool autoUpdate = g_set.autoUpdate;
        bool shouldInstall = autoUpdate;

        if (!autoUpdate) {
            char msg[512];
            _snprintf_s(msg, sizeof msg, _TRUNCATE,
                "A newer texoverride is out: %s (you have " TEXOVERRIDE_VERSION ").\n\n"
                "Download and install this update automatically?\n"
                "(The update will take effect on your next FiveM restart)\n\n"
                "- Yes: Download and install automatically\n"
                "- No: Open the release page in your browser\n"
                "- Cancel: Skip for now\n\n"
                "To turn this check off, create _NO_UPDATE_CHECK in tex_overrides.",
                latest.c_str());
            int choice = MessageBoxA(nullptr, msg, "texoverride update",
                                     MB_YESNOCANCEL | MB_ICONINFORMATION | MB_SETFOREGROUND | MB_TOPMOST);
            if (choice == IDYES) {
                shouldInstall = true;
            } else if (choice == IDNO) {
                ShellExecuteA(nullptr, "open", "https://github.com/blancodagoat/texoverride/releases",
                              nullptr, nullptr, SW_SHOWNORMAL);
                return 0;
            } else {
                LOG_INFO(LogCategory::Update, "Update %s deferred by user", latest.c_str());
                return 0;
            }
        }

        if (shouldInstall) {
            if (installUpdate(downloadUrl, latest, expectHash)) {
                if (!autoUpdate) {
                    char doneMsg[256];
                    _snprintf_s(doneMsg, sizeof doneMsg, _TRUNCATE,
                        "texoverride %s has been installed!\n\n"
                        "It will take effect the next time you start FiveM.",
                        latest.c_str());
                    MessageBoxA(nullptr, doneMsg, "texoverride update",
                                MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND | MB_TOPMOST);
                }
            } else {
                if (!autoUpdate) {
                    char failMsg[320];
                    _snprintf_s(failMsg, sizeof failMsg, _TRUNCATE,
                        "Failed to install texoverride %s automatically (see texoverride.log for details).\n\n"
                        "Would you like to open the release page in your browser instead?",
                        latest.c_str());
                    if (MessageBoxA(nullptr, failMsg, "texoverride update",
                                    MB_YESNO | MB_ICONWARNING | MB_SETFOREGROUND | MB_TOPMOST) == IDYES) {
                        ShellExecuteA(nullptr, "open", "https://github.com/blancodagoat/texoverride/releases",
                                      nullptr, nullptr, SW_SHOWNORMAL);
                    }
                }
            }
        }
    }
    else LOG_INFO(LogCategory::Update, "Plugin is up to date (latest %s)", latest.c_str());
    return 0;
}
