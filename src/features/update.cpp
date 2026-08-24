#include "features/update.h"
#include "core/version.h"
#include "core/state.h"
#include "core/logger.h"
#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>
#include <cstdio>
#include <string>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")

// ---- update check: one HTTPS ask at startup, "what is the newest release tag?" ----
// Sends nothing except the request itself. Fails silently when offline. Skipped when _OFF or
// _NO_UPDATE_CHECK exists in tex_overrides. Runs on its own thread so a shown popup never
// blocks the re-assert loop.
int verCmp(const char* a, const char* b)   // >0 when a is newer than b
{
    int A[3] = {}, B[3] = {};
    sscanf_s(a, "%d.%d.%d", &A[0], &A[1], &A[2]);
    sscanf_s(b, "%d.%d.%d", &B[0], &B[1], &B[2]);
    for (int i = 0; i < 3; ++i) if (A[i] != B[i]) return A[i] - B[i];
    return 0;
}

DWORD WINAPI UpdateCheck(LPVOID)
{
    if (g_off) return 0;
    if (GetFileAttributesA((std::string(g_overrideDir) + "_NO_UPDATE_CHECK").c_str()) != INVALID_FILE_ATTRIBUTES) return 0;

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

    if (verCmp(lv, TEXOVERRIDE_VERSION) > 0) {
        LOG_INFO(LogCategory::Update, "Update available: %s (current: " TEXOVERRIDE_VERSION ")", latest.c_str());
        char msg[256];
        _snprintf_s(msg, sizeof msg, _TRUNCATE,
            "A newer texoverride is out: %s (you have " TEXOVERRIDE_VERSION ").\n\n"
            "Open the download page now?\n\n"
            "To turn this check off, create a file named _NO_UPDATE_CHECK in tex_overrides.",
            latest.c_str());
        if (MessageBoxA(nullptr, msg, "texoverride update",
                        MB_YESNO | MB_ICONINFORMATION | MB_SETFOREGROUND | MB_TOPMOST) == IDYES)
            ShellExecuteA(nullptr, "open", "https://github.com/blancodagoat/texoverride/releases",
                          nullptr, nullptr, SW_SHOWNORMAL);
    }
    else LOG_INFO(LogCategory::Update, "Plugin is up to date (latest %s)", latest.c_str());
    return 0;
}
