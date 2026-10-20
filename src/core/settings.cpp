#include "core/settings.h"
#include "core/state.h"
#include "core/utils.h"
#include "core/logger.h"
#include <windows.h>
#include <cstdio>
#include <vector>

Settings g_set;

// A marker file found at startup, and the settings line that replaces it. Filled by
// loadSettings under the loader lock, drained by migrateSettings on the beat thread, which is
// the first place a write is safe and the first place the log exists to say what happened.
struct Marker { std::string path; std::string key; std::string value; };
static std::vector<Marker> g_markers;

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
# The plugin always checks for a new version when it starts and always tells
# you when one is out. This only decides whether it asks before installing.
auto_update = no


# Take FiveM's own writing out of your screenshots.
#
# FiveM draws its version in one corner and "N mod packs loaded" in the other,
# and both end up in every picture you take. List the keys you use to take
# screenshots and they come off the screen for a moment when you press one, so
# they are not in the shot.
#
#   printscreen, f1 to f12, a single letter or digit, or no
#   always keeps them off the screen the whole time you play
hide_overlay = no
)";

// Generous on purpose: people write yes, on, true, 1, enabled. Anything else is off, which is
// the safe direction for every option here.
static bool truthy(const std::string& v)
{
    return v == "yes" || v == "on" || v == "true" || v == "1" || v == "enabled";
}

// A key name from _settings.txt (hide_overlay) as a Windows virtual-key code. 0 means the user
// turned it off, -1 means they wrote something that is not a key, which is worth a warning rather
// than a silent default. The caller polls GetAsyncKeyState rather than FiveM's InputHook::IsKeyDown
// on purpose: it needs no export a FiveM update could rename, and the caller's foreground check
// is what IsKeyDown would have bought.
int vkFromName(const std::string& raw)
{
    std::string v = lower(raw);
    if (v.empty() || v == "off" || v == "no" || v == "none" || v == "disabled") return 0;
    // spelled every way people actually type it, because nobody agrees on where the vowels go
    if (v == "printscreen" || v == "prntscrn" || v == "prtscn" || v == "prtsc" || v == "print") return VK_SNAPSHOT;
    if (v.size() >= 2 && v[0] == 'f') {
        int n = atoi(v.c_str() + 1);
        if (n >= 1 && n <= 12) return VK_F1 + n - 1;
    }
    if (v.size() == 1) {
        char c = v[0];
        if (c >= 'a' && c <= 'z') return 'A' + (c - 'a');
        if (c >= '0' && c <= '9') return c;
    }
    return -1;   // written down, but not a key we know
}

// Splits "key = value" into its two halves, lowercased. Blank lines and notes give no key.
static bool splitLine(const std::string& raw, std::string& k, std::string& v)
{
    std::string s = trim(raw);
    if (s.empty() || s[0] == '#') return false;
    size_t eq = s.find('=');
    if (eq == std::string::npos) return false;
    k = lower(trim(s.substr(0, eq)));
    v = lower(trim(s.substr(eq + 1)));
    return !k.empty();
}

// Options that no longer exist. _settings.txt is never rewritten wholesale, so a dropped option
// would otherwise sit in every existing install for ever, still explaining a feature that is not
// there. The note above the line goes with it, or the file keeps describing the missing thing.
static const char* kDeadKeys[] = {
    "refresh_key",       // 0.8.23
    "debug",             // 0.8.26: the log always carries DEBUG detail now
    "force_reload",      // 0.8.26: dropped resident objects the game was still using; crashed the game
    "no_update_check",   // 0.8.26: the update check always runs and always says what it found
};

static bool isDead(const std::string& k)
{
    for (const char* d : kDeadKeys) if (k == d) return true;
    return false;
}

static bool stripDeadKeys(std::vector<std::string>& lines)
{
    bool changed = false;
    for (size_t i = 0; i < lines.size(); ) {
        std::string k, v;
        if (!splitLine(lines[i], k, v) || !isDead(k)) { ++i; continue; }
        size_t a = i;
        while (a > 0) { std::string t = trim(lines[a - 1]); if (t.empty() || t[0] != '#') break; --a; }
        size_t b = i + 1;
        while (b < lines.size() && trim(lines[b]).empty()) ++b;   // the gap after, so blocks stay spaced
        lines.erase(lines.begin() + a, lines.begin() + b);
        i = a;
        changed = true;
    }
    return changed;
}

// Note a marker file if it is there. The value is always "yes": a marker file only ever existed
// to turn something on, so that is the whole of what it can mean.
static bool marker(const char* name, const char* key)
{
    std::string p = ctlPath(name);
    if (p.empty()) return false;
    g_markers.push_back({ p, key, "yes" });
    return true;
}

// _budget is the one marker with contents rather than just a name, so it carries its number over.
static void markerBudget()
{
    std::string p = ctlPath("_budget");
    if (p.empty()) return;
    std::string v;
    if (FILE* f = openFile(p, "rb")) {
        char buf[32] = {};
        fread(buf, 1, 31, f);
        fclose(f);
        v = lower(trim(buf));
    }
    if (v.empty()) v = "auto";
    g_set.budget = v;
    g_markers.push_back({ p, "texture_budget", v });
}

void loadSettings()
{
    // Markers first, then the settings file. A marker only ever turned an option ON, so it can
    // never be the thing that switches one off, and THIS launch behaves exactly as the last one
    // did even though migrateSettings is about to delete the file the setting came from.
    bool mOff   = marker("_off", "off");
    bool mAuto  = marker("_auto_update", "auto_update");
    // These three name options that no longer exist. Still noted, so the file gets deleted.
    marker("_debug", "debug");
    marker("_verbose", "debug");
    marker("_no_update_check", "no_update_check");
    markerBudget();

    g_set.off           = mOff;
    g_set.autoUpdate    = mAuto;

    std::string p = ctlPath("_settings");
    if (p.empty()) return;
    FILE* f = openFile(p, "rb");
    if (!f) return;

    char line[512];
    while (fgets(line, sizeof line, f)) {
        std::string k, v;
        if (!splitLine(line, k, v)) continue;
        if      (k == "off")             g_set.off           = g_set.off           || truthy(v);
        else if (k == "auto_update")     g_set.autoUpdate    = g_set.autoUpdate    || truthy(v);
        else if (k == "texture_budget" && g_set.budget.empty()) g_set.budget = v;
        else if (k == "hide_overlay") g_set.hideOverlay = v;
    }
    fclose(f);
}

static bool writeDefault()
{
    FILE* f = openFile(std::string(g_overrideDir) + "_settings.txt", "wb");
    if (!f) return false;
    fputs(kDefaultSettings, f);
    fclose(f);
    return true;
}

// Off the loader lock (backgroundStartup), which is both where a write is safe and where the log
// exists. Creates the file when it is missing, then folds in any marker files and deletes them,
// so the next launch finds nothing left to migrate and the folder holds one settings file rather
// than a scattering of empty ones. The rewrite is line by line on purpose: the notes in the file
// and anything the user added to it have to survive.
//
// A player running with _off never reaches this, because Setup returns before the beat thread
// starts. That is correct rather than a gap: the plugin is meant to do nothing at all in that
// mode, and the moment they delete _off to turn it back on there is no marker left to migrate.
void migrateSettings()
{
    if (ctlPath("_settings").empty() && !writeDefault()) {
        LOG_WARN(LogCategory::Core, "Could not create _settings.txt in tex_overrides; running on defaults");
        return;
    }
    std::string path = ctlPath("_settings");
    std::vector<std::string> lines;
    {
        FILE* f = openFile(path, "rb");
        if (!f) return;
        char buf[512];
        while (fgets(buf, sizeof buf, f)) lines.push_back(buf);
        fclose(f);
    }

    bool dropped = stripDeadKeys(lines);
    if (g_markers.empty() && !dropped) return;

    // A marker for an option that no longer exists is only deleted, never written into the file.
    std::vector<bool> placed(g_markers.size(), false);
    for (size_t i = 0; i < g_markers.size(); ++i) placed[i] = isDead(g_markers[i].key);
    for (auto& raw : lines) {
        std::string k, v;
        if (!splitLine(raw, k, v)) continue;
        for (size_t i = 0; i < g_markers.size(); ++i) {
            if (placed[i] || g_markers[i].key != k) continue;
            raw = g_markers[i].key + " = " + g_markers[i].value + "\n";
            placed[i] = true;
            break;
        }
    }
    // a key the user deleted out of the file: append it rather than quietly lose the setting
    for (size_t i = 0; i < g_markers.size(); ++i)
        if (!placed[i]) lines.push_back(g_markers[i].key + " = " + g_markers[i].value + "\n");

    FILE* f = openFile(path, "wb");
    if (!f) {
        LOG_WARN(LogCategory::Core, "Could not write _settings.txt; the old settings files are left alone");
        return;
    }
    for (auto& l : lines) fputs(l.c_str(), f);
    fclose(f);

    if (dropped)
        LOG_INFO(LogCategory::Core, "Took an option that no longer exists out of _settings.txt (one of debug, force_reload, no_update_check, refresh_key)");

    for (auto& m : g_markers) {
        if (isDead(m.key)) {
            if (deleteFile(m.path))
                LOG_INFO(LogCategory::Core, "Deleted %s; that option no longer exists", rel(m.path.c_str()));
        }
        else if (deleteFile(m.path))
            LOG_INFO(LogCategory::Core, "Moved %s into _settings.txt as \"%s = %s\" and deleted it",
                     rel(m.path.c_str()), m.key.c_str(), m.value.c_str());
        else
            LOG_WARN(LogCategory::Core, "Copied %s into _settings.txt but could not delete it; delete it yourself or it keeps taking priority",
                     rel(m.path.c_str()));
    }
    g_markers.clear();
}
