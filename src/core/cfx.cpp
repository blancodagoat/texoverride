#include "core/cfx.h"
#include "core/logger.h"
#include <windows.h>
#include <new>

// Resolved by NAME at runtime, never as a static import. A static import on a FiveM DLL would
// make the whole ASI fail to load the day a component is renamed or missing, and that failure
// shows up as the bare "Couldn't load texoverride.asi" dialog with no log at all (issue #16).
// Missing export -> the feature that wanted it stays off, and the plugin carries on.
void* cfxSymbol(const char* dll, const char* mangled)
{
    HMODULE m = GetModuleHandleA(dll);      // never LoadLibrary: if FiveM has not loaded it, we do not want it
    void* p = m ? (void*)GetProcAddress(m, mangled) : nullptr;
    LOG_DEV(LogCategory::Core, "cfx symbol %s!%s -> %s", dll, mangled, !m ? "DLL NOT LOADED" : (p ? "ok" : "MISSING"));
    return p;
}

// fwEvent<> is a header-only template in Cfx, so Connect is inlined into every component and
// exported by none — the exported symbol is the event OBJECT. The layout below was read out of
// citizen-mod-loader-five.dll's own inlined Connect (the call site behind its OnPostFrontendRender
// subscription, 0x18000e190 on the b3788 client), not guessed:
//
//   fwEvent  { callback* head; size_t nextCookie; }   cookie bumped with lock xadd
//   callback { std::function<bool()> fn;              0x00, 0x40 bytes
//              callback* next;                        0x40
//              int order;                             0x48
//              size_t cookie; }                       0x50, node is 0x58 total
//
// Cfx inserts in ascending `order`; we always prepend with order 0, because a single atomic
// write is the only splice that cannot destroy a node FiveM added at the same moment (see
// cfxConnect). A handler that returns false stops the rest of the chain from running, so ours
// always returns true.
//
// Two things make this safe to do from outside Cfx's build:
//   - the node holds a std::function whose copy/delete run through ITS OWN vtable, which is our
//     code and our allocator, so the functor never crosses a CRT boundary;
//   - the node itself is freed by Cfx (only in ~fwEvent, at process exit, since we never
//     disconnect), so it is allocated on the process heap, which is what the UCRT's free() has
//     used since VS2015 for both the static and dynamic CRT.
// If a future MSVC changes std::function's layout the static_asserts below stop the build rather
// than shipping a silent corruption.
struct CfxEventNode
{
    std::function<bool()> fn;
    CfxEventNode* next;
    int order;
    int pad;
    size_t cookie;
};

static_assert(sizeof(std::function<bool()>) == 0x40, "std::function layout no longer matches FiveM's fwEvent node");
static_assert(sizeof(CfxEventNode) == 0x58, "fwEvent callback node is 0x58 bytes in FiveM");

struct CfxEvent
{
    CfxEventNode* head;
    volatile LONG64 nextCookie;
};

bool cfxConnect(void* fwEventObject, std::function<bool()> fn)
{
    if (!fwEventObject) return false;
    auto* ev = (CfxEvent*)fwEventObject;

    void* mem = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(CfxEventNode));
    if (!mem) return false;
    auto* node = new (mem) CfxEventNode{ std::move(fn), nullptr, 0, 0,
                                         (size_t)InterlockedIncrement64(&ev->nextCookie) };

    // Prepend with one atomic write, never Cfx's ordered walk. Cfx's own Connect takes no lock
    // and splices with plain stores, so if we walk and store too, a component connecting at the
    // same moment can have ITS node dropped on the floor - and a FiveM handler that quietly goes
    // missing during load is a crash somewhere else entirely, several seconds later, with
    // nothing of ours anywhere near the stack. A compare-exchange on the head can only ever lose
    // OUR node, never theirs. Callers connect from Setup(), where nothing else is inserting yet,
    // so in practice it never even retries.
    for (;;) {
        CfxEventNode* head = ev->head;
        node->next = head;   // published before the pointer TO the node; x86-64 keeps that order
        if (InterlockedCompareExchangePointer((void* volatile*)&ev->head, node, head) == head)
            return true;
    }
}
