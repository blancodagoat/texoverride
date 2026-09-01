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

// Take the handlers owned by the given DLLs (HMODULEs) off the event, or put them back. Call it
// ONLY from the thread that walks the event, i.e. from inside a handler of that same event.
// Returns how many nodes moved.
int cfxDetachOwned(void* fwEventObject, void** owners, int nOwners, bool detach);

#ifdef TEXOVERRIDE_DEV
// Dev build only: one line per node on the event (order, cookie, which DLL owns the handler).
void cfxDumpEvent(void* fwEventObject, const char* name);
#endif
