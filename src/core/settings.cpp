#include "core/settings.h"
#include "core/state.h"
#include "core/utils.h"
#include <windows.h>
#include <cstdio>

Settings g_set;

// The file the plugin writes on first run. Everything is off, everything says what it does. The
// point of the wording is that a player who has never opened a config file can still work out
// what to change, so it explains rather than names.
static const char* kDefaultSettings =
R"(# texoverride settings
#
# Change a value below, save the file, then restart FiveM.
# Lines starting with # are notes. The plugin ignores them.


# Stop the plugin completely.
# It stays installed and FiveM still loads it, but it does nothing at all: no
# log, no scanning, nothing replaced. Use this to find out whether a problem is
# caused by texoverride or by something else.
off = no


# Write extra detail into texoverride.log.
# Turn this on when someone is helping you work out a problem, and turn it off
# again afterwards. It makes the log a lot longer.
debug = no


# How much memory the game may use for textures, in GB.
#
#   auto   the plugin picks a number that fits your graphics card
#   game   leave the game's own setting alone
#   8      use this many GB (any number from 1 to 48)
#
# Raising this gives you room before textures start going missing on busy
# servers. It cannot make a pack fit that is simply too big.
texture_budget = auto


# Install new versions on their own, without asking you first.
auto_update = no


# Never check whether a new version is out.
# Normally the plugin asks GitHub for the newest version number when it starts.
# Turning this on stops that, and then it never uses the internet at all.
no_update_check = no
)";

static std::string trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Generous on purpose: people write yes, on, true, 1, enabled. Anything else is off, which is
// the safe direction for every option here.
static bool truthy(const std::string& v)
{
    return v == "yes" || v == "on" || v == "true" || v == "1" || v == "enabled";
}

void loadSettings()
{
    // Legacy marker files first, so an install that predates this file keeps behaving the same.
    // They only ever turn an option ON; the settings file below can add more but never cancels
    // one, which keeps "why is it still off" from having two possible answers.
    g_set.off           = !ctlPath("_off").empty();
    g_set.debug         = !ctlPath("_debug").empty() || !ctlPath("_verbose").empty();
    g_set.autoUpdate    = !ctlPath("_auto_update").empty();
    g_set.noUpdateCheck = !ctlPath("_no_update_check").empty();

    std::string p = ctlPath("_settings");
    if (p.empty()) return;
    FILE* f = nullptr;
    if (fopen_s(&f, p.c_str(), "rb") || !f) return;

    char line[512];
    while (fgets(line, sizeof line, f)) {
        std::string s = trim(line);
        if (s.empty() || s[0] == '#') continue;
        size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        std::string k = lower(trim(s.substr(0, eq)));
        std::string v = lower(trim(s.substr(eq + 1)));
        if      (k == "off")             g_set.off           = g_set.off           || truthy(v);
        else if (k == "debug")           g_set.debug         = g_set.debug         || truthy(v);
        else if (k == "auto_update")     g_set.autoUpdate    = g_set.autoUpdate    || truthy(v);
        else if (k == "no_update_check") g_set.noUpdateCheck = g_set.noUpdateCheck || truthy(v);
        else if (k == "texture_budget")  g_set.budget        = v;
    }
    fclose(f);
}

// Off the loader lock (backgroundStartup). Only ever creates the file, never rewrites one that
// is already there: the user's own edits and their own notes stay untouched across updates.
void writeDefaultSettings()
{
    if (!ctlPath("_settings").empty()) return;
    std::string p = std::string(g_overrideDir) + "_settings.txt";
    FILE* f = nullptr;
    if (fopen_s(&f, p.c_str(), "wb") || !f) return;
    fputs(kDefaultSettings, f);
    fclose(f);
}
