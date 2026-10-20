#pragma once

#include <string>

// The one filter both scans run a file through: ignored types, the types we load, the slot key
// rule, the quarantine. Returns the key, or "" for a file that is not ours. announce: write the
// SKIP / IGNORED lines (the live rescan passes false for files the startup scan already logged).
std::string overrideKeyFor(const std::string& name, const std::string& childRel, bool announce);

// Beat thread, NOT DllMain: walks tex_overrides, reads every header, publishes g_ovs and g_bySlot.
void scanOverrides();
