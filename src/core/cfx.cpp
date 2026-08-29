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
// Nodes are inserted in ascending `order`, and the handler returning false stops the rest of the
// chain from running, so ours always returns true.
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

bool cfxConnect(void* fwEventObject, std::function<bool()> fn, int order)
{
    if (!fwEventObject) return false;
    auto* ev = (CfxEvent*)fwEventObject;

    void* mem = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(CfxEventNode));
    if (!mem) return false;
    auto* node = new (mem) CfxEventNode{ std::move(fn), nullptr, order, 0,
                                         (size_t)InterlockedIncrement64(&ev->nextCookie) };

    // Insert before the first node with a strictly greater order, which is what Cfx's own
    // Connect does. Cfx takes no lock and neither do we, but unlike Cfx we connect long after
    // the game is running, so the main thread IS walking this list while we splice into it.
    // That is safe here and only here: the node's own next pointer is published before the
    // pointer TO the node, both stores are aligned and x86-64 does not reorder stores, so a
    // walker either misses us for one frame or sees a node that is already complete. Nothing
    // is ever removed, so no walker can be left holding a freed node.
    // ponytail: lock-free prepend, single writer. It needs a real lock the day anything in the
    // plugin disconnects, or connects from two threads at once.
    CfxEventNode** slot = &ev->head;
    while (*slot && (*slot)->order <= order) slot = &(*slot)->next;
    node->next = *slot;
    *slot = node;
    return true;
}
