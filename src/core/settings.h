#pragma once

#include <string>

// One settings file. tex_overrides\_settings.txt is written on first run with every option
// listed, explained in plain English and switched off, so a user changes a word rather than
// guessing at a filename Explorer will not let them type.
//
// The old marker files (_off, _debug, _verbose, _budget, _auto_update, _no_update_check, with or
// without .txt) are still READ, but only once: the first launch that finds one folds its value
// into _settings.txt and deletes it. So an install converges on exactly one settings file and
// nothing else, and nobody ever has to be told to rename anything.
struct Settings
{
    bool off            = false;
    bool debug          = false;
    bool autoUpdate     = false;
    bool noUpdateCheck  = false;
    std::string budget;   // raw value; resolved in readBudgetFile, which can actually log
    // Default lives here rather than only in the file text, so the installs that already have a
    // _settings.txt (which is never rewritten) get the key without having to add a line.
    std::string refreshKey = "f11";
    std::string hideOverlay;   // comma separated screenshot keys, or "always"; empty = leave FiveM's overlays alone
};

extern Settings g_set;

int  vkFromName(const std::string& raw);   // "f11" -> VK_F11; 0 = off, -1 = not a key

void loadSettings();       // Setup(), under the loader lock: reads only, never writes, never logs
void migrateSettings();    // beat thread: create the file if missing, absorb markers, delete them
