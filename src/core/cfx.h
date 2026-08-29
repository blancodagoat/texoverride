#pragma once

#include <cstddef>
#include <functional>

// FiveM's own component DLLs export a usable API, and they are already loaded in our process.
// Everything the plugin borrows from them is resolved here, so a FiveM update that renames a
// symbol breaks in one place instead of five.
void* cfxSymbol(const char* dll, const char* mangled);

// Subscribe to one of Cfx's exported fwEvent objects. Returns false if the event pointer is
// null or the node could not be allocated; the caller is expected to have a fallback.
bool cfxConnect(void* fwEventObject, std::function<bool()> fn);
