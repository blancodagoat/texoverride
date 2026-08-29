#pragma once

#include <string>

// One settings file instead of a folder of oddly named marker files. tex_overrides\_settings.txt
// is written on first run with every option listed, explained in plain English and switched off,
// so a user changes a word rather than guessing at a filename Explorer will not let them type.
// The old marker files (_off, _debug, _budget, _auto_update, _no_update_check, with or without
// .txt) still work and still turn their option ON, so nothing breaks for anyone who has them.
struct Settings
{
    bool off            = false;
    bool debug          = false;
    bool autoUpdate     = false;
    bool noUpdateCheck  = false;
    std::string budget;   // raw value; resolved in readBudgetFile, which can actually log
};

extern Settings g_set;

void loadSettings();           // runs in Setup(), under the loader lock, so it never logs
void writeDefaultSettings();   // runs on the beat thread; creates the file if it is missing
