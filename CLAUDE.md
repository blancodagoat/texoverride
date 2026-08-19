# CLAUDE.md — texoverride

Working notes for Claude on this repo. Everything load-bearing that is not obvious from the code.

## What this is

A FiveM client ASI plugin (C++, MinHook) that draws the user's own `.ydd`/`.ytd` files on their
ped. Files go in a `tex_overrides/` folder next to the plugin; the plugin registers them at the
game's streaming layer, doing at runtime what a server `stream/` folder does. It never edits or
re-encrypts game archives, sends nothing to the game server, and other players see no change.

It exists because FiveM's own client mod paths (mods/ folder, pseudo-DLC wrapping, caret naming)
cannot deliver ped textures — they drain before the connect handshake, and the pseudo-DLC mount is
non-overlay. This works at the streaming layer instead.

Repo: github.com/blancodagoat/texoverride (owner blancodagoat). Single-file plugin: `dllmain.cpp`.
Latest tag at time of writing: v0.4.1. Only loads on `sv_pureLevel 0` servers (level 1 needs a Cfx
signature a self-built ASI lacks; level 2 disables the ASI loader).

## The three override mechanisms (all in dllmain.cpp)

1. **Clothing** — `tex_overrides/<collection>/<file>.ydd|.ytd`. The folder names a freemode-ped
   collection (see COLLECTIONS.md, 186 valid names). Registered under `collection/file` and
   re-asserted every second so DLC mounts and FiveM's own loader can't steal the slot back.

2. **Bare-name overlays** — `tex_overrides/<name>.ytd` at the ROOT, no folder. Skin, tattoos,
   facepaint, beards, decals — loose txds that stream by filename alone. Registered under the bare
   filename, exact-name match. `.ydd` is never accepted at root (models always belong to a
   collection). No name whitelist: a scan of every overlay rpf found ~100 unrelated naming
   families plus arbitrary server packs, so the gate is type + exactness (`isAllowedKey`).

3. **Tattoo placement** — `tex_overrides/<name>.xml` at the root (an edited copy of a pack's
   `overlays.xml`). Moves/resizes/rotates tattoos by patching the parsed values inside the game's
   `PedDecorationManager`. Data writes only, no code touched. See "Placement solver" below.

## Safety gates (do not weaken without asking)

- Clothing folders: only `mp_m_freemode_01*` / `mp_f_freemode_01*` collections. Everything else
  (animals, story peds, vehicles, props, maps, scripts) is refused at load and skipped at runtime.
  This is what stops a "dog head on a human" mistake.
- Root files: `.ytd` only, exact-name. Placement `.xml`: fingerprint match required (below).
- The safety gate exists because filename-alone matching would cross-wire unrelated items. Keep it.

## How the hook works

Hooks ONE game function, `registerRawStreamingFile`, in the GAME module (`GTA5.exe`) only, never
FiveM's DLLs (Cfx `legitimacy` anti-tamper kills the process if you touch Cfx components; a
game-module hook is the same surface trainers use). Byte pattern from Cfx's open source
(`gta-streaming-five/src/Streaming.cpp`). Flags passed to it are constants `(true, false)`.

Base freemode clothing lives in `x64v.rpf` and never passes through that function, so the plugin
CALLS `registerRawStreamingFile` itself to claim slots, then re-asserts the handle once a second:
a streaming slot is name → id → handle, last handle-writer wins. FiveM's loader overwrites handles
directly (bypassing the hook), and `.ytd`s go through FiveM's own `RegisterObject`, which is why
the re-assert loop is mandatory — a one-shot claim never sticks. Same mechanism Cfx uses in
`LoadStreamingFile.cpp`.

## Freeze-free install (important, was a real fix)

MinHook's thread-freeze (SuspendThread via CreateToolhelp32Snapshot) NEVER worked under FiveM —
FiveM blocks the snapshot. The vendored `minhook/src/hook.c` has a patch to proceed without
freezing. That was safe only by luck until v0.3.0.

v0.3.0 made it safe by construction via TIMING: `Setup()` runs synchronously in `DllMain`. FiveM
loads plugins in `LauncherInterface::PostLoadGame`, which returns the game entry point to the
launcher — the entry point has NOT run yet, so no thread is executing game code during the patch.
Same window FiveM uses for its own HookFunction patches. Do not move Setup() off the DllMain path.

A hookless redesign (call the found function directly, delete MinHook) was OFFERED and DECLINED by
the user 2026-08-18 — keeping the hook preserves the collection-map logging (how players find
custom server collection names) and provable registration timing. Do not re-propose unless
something changes.

## Placement solver (the overlays.xml feature)

Never hardcodes struct offsets. At startup it parses each root `.xml` (minimal string parser, not
a real XML lib) into presets (name hash + uvX/uvY/scaleX/scaleY/rotation). At runtime each beat it:
1. Locates `PedDecorationManager` via Cfx's published pattern (`PatchTattooSort.cpp`).
2. Finds the collection by name hash (0xA0-byte struct, name hash at +0x10, "never changed" — Cfx).
3. SOLVES the preset array layout by fingerprinting: preset name hashes give base+stride, and
   >=70% of the file's uv/scale/rot values must match memory before ANY byte is written.
4. Applies edited floats and re-asserts them each beat.

Faults are SEH-shielded: a solve fault retires ONE collection (`placementSolve` wrapper), an apply
fault disables the whole feature (`placementBeatSafe`). Needs >=3 presets, most unedited. `joaat`
is GTA's case-insensitive hash — verified against known hashes.

## shop_tattoo.meta — researched, WILL NOT be built (settled 2026-08-19)

Users may have `shop_tattoo.meta` (one per dlcpack, no dlc name in the filename). Final decision:
the plugin does NOT apply it and never will; it just logs dropped `.meta` files as "ignored"
(v0.4.1). The user confirmed on 2026-08-19 they do not care about it. Do not propose building it
again. The research below is kept only so the "why" is not re-derived, not as a to-do.

Why not applied (from GameSource + Rockstar's own .psc parser schema):
- It's a DLC data file, `fileType TATTOO_SHOP_DLC_FILE`, declared in each pack's `content.xml`,
  loaded via `CDataFileMgr` → `CExtraMetaDataFileMounter` →
  `CExtraMetadataMgr::AddTattooShopItemsCollection(filename)` into `m_tattooShopsItems`.
- `TattooShopItem` parses ONLY: `m_lockHash/m_cost/m_textLabel` (BaseShopItem) +
  `m_id/m_collection/m_preset/m_updateGroup/m_eFacing/m_eFaction`. There is **no `m_zone` field** —
  a `<zone>` in shop_tattoo.meta is DISCARDED on load. Real zone/uv/scale/rotation live in
  overlays.xml (already patched). `eFacing` is Rockstar-labeled "camera facing of the item" (shop
  preview camera); `updateGroup` is preview apparel. Both appear only in shop/debug code, not the
  render path.
- So shop_tattoo.meta only matters to consumers of the game's NATIVE tattoo shop (native UI or the
  `GET_TATTOO_SHOP_DLC_ITEM_DATA` native). It is INERT for custom menus like rcore_tattoos (the
  user's server), which carry their own list and apply ink via `ADD_PED_DECORATION_FROM_HASHES`.
- If ever built: clean route = a FiveM mod-package wrapper whose `content.xml` registers the file
  (game loads it natively, update-proof); fragile route = call `AddTattooShopItemsCollection` at
  runtime (no published pattern, must be located by hand). Add-only avoids the Remove + per-pack
  CRC-name problem; a true replace needs Remove(originalName) first.

## Live reload (v0.5.0)

A watcher thread (spawned from BeatLoop once g_idsReady) sits on FindFirstChangeNotification over
tex_overrides — event-driven, no polling; 500ms debounce, then one rescan. Three change kinds:
edited root .xml re-parses and merges into g_pl (solved layout carried over when preset hashes
match, so live tattoo tuning never re-fingerprints against already-patched memory); overwritten
.ytd/.ydd gets its pgRawStreamer entry re-statted (timestamp = 0 + GetEntry — Cfx's own trick,
LoadStreamingFile.cpp ~1968); brand-new files register mid-session.

THREADING (the load-bearing part): RAGE's RegisterObject → module->Register inserts into name
tables with NO lock (verified in GameSource streaminginfo.cpp:4274), and the game main thread
reads them constantly — which is why Cfx does mid-session registration on OnMainGameFrame. We
reach the same thread via an IAT shim on the game exe's PeekMessageW import (the main loop pumps
it every frame; Cfx's ZOffThreadWindowing IAT-hooks the same import). Watcher queues LiveOps,
h_peekMsg drains them on the first-caller thread only. Never move register/re-stat back onto the
watcher thread. (Historical: rage-allocator-five's ThreadAttachment.cpp stamps allocator TLS into
every new thread on DLL_THREAD_ATTACH once the game boots — was the fallback plan, not needed.)

CRASH SAVER: batches journal to tex_overrides\_inflight.txt before the game thread touches them;
journal persists 30s after apply. Crash inside the window → next launch appends those keys to
_quarantine.txt, and scanDir/rescanTree refuse them until the user deletes that file. Orderly
exit deletes the journal in DLL_PROCESS_DETACH (a real crash never runs it). New patterns used:
getRawStreamer + pgRawStreamer::GetEntry (both from Cfx LoadStreamingFile.cpp).

## The overlay index (docs/overlay_index.tsv)

3,921 vanilla presets: preset → collection → source overlays.xml path → zone/type/gender/uv/scale/
rotation/txd. This is the "which file owns this tattoo" solver the tutorials point users to.
Generated by a throwaway CodeWalker.Core scanner + a python parser (in Claude's scratchpad, not the
repo). To regenerate: decrypt every rpf with CodeWalker.Core (fork at
`D:\Projects\codewalker\CodeWalker-fork`, net9.0), extract every `*overlay*`/`*tattoo*` xml,
parse `<PedDecorationCollection>` presets. Game install: `D:\Steam\steamapps\common\Grand Theft Auto V`.

## Conventions (user rules — follow exactly)

- **One version bump per push, not per feature.** All unpushed work collapses into a single bump
  above the last pushed version. (Earlier this session, three logical versions collapsed to 0.3.0.)
- **CHANGELOG.md is part of the release.** The build workflow (`.github/workflows/build.yml`) takes
  release notes from the top `##` section of CHANGELOG.md (`awk '/^## /{n++} n==1'`). Keep a dated
  section per release; newest on top.
- **Humanize all doc text.** Run new README/CHANGELOG lines through the humanizer skill: no
  AI-writing tells, and NO em/en dashes (—/–). Check with grep before committing.
- **Tutorials in the plainest possible English.** Short sentences, everyday words, jargon explained
  on first use. Technical sections (How it works, Ban risk, Why FiveM allows this) may stay
  technical. The audience is ordinary FiveM players, not developers.
- **No AI attribution in commits/PRs** (global user rule): never add Co-Authored-By, Claude-Session,
  or any AI trailer. Plain commit messages.
- The "Ban risk, stated plainly" README section stays — the 5-byte patch lives in game code all
  session and a scan can flag it; the honest disclosure is what makes the project credible.

## Build & release

- `build.bat` — MSVC (VS Build Tools 2022, "Desktop development with C++"). `/MT /EHsc /std:c++17`,
  no /clr. The user's shell prints `fastfetch`/`vswhere` noise and exits nonzero even on success;
  trust the `Built texoverride.asi` line, not the exit code.
- `.asi` is gitignored — CI builds it and attaches it to the GitHub Release. Never commit the .asi.
- Release: bump `TEXOVERRIDE_VERSION` in dllmain.cpp, date the CHANGELOG section, commit, push,
  `git tag vX.Y.Z`, push the tag. CI builds both the push and the tag; the tag build makes the
  release with the changelog notes + the .asi.
- **The FX_ASI_BUILD stamp** (`texoverride.rc`): FiveM refuses ASIs on game build 2189+ that don't
  claim the running build. One `FX_ASI_BUILD <build> BEGIN "\0" END` line per supported build
  (currently 3751, 3788). New game build → add a line or the plugin silently stops loading.
- Installing over a running FiveM: the loaded .asi is locked. Rename the old one aside
  (`mv ...asi ...asi.old`) then copy the new one in; delete the .old after FiveM closes.

## FiveM source facts (verified this session, for future work)

- `registerRawStreamingFile` pattern + constant flags: `gta-streaming-five/src/Streaming.cpp`.
- Handle-overwrite override path: `gta-streaming-five/src/LoadStreamingFile.cpp`.
- Plugin load timing: `LauncherInterface::PostLoadGame` in `code/client/citigame/Launcher.cpp`,
  before the game entry point runs. asi loader: `components/asi-five/src/Component.cpp`
  (deny-by-exception blacklist: openiv.asi, scripthookvdotnet.asi, fspeedometerv.asi, Gears.asi,
  .NET assemblies).
- Mod packages: `components/citizen-mod-loader-five/src/` (ModPackage.cpp, ModVFSDevice.cpp) —
  OpenIV assembly.xml format, pure level 0 only, can register data files and pseudo-DLCs.
- PedDecorationManager pattern + struct: `gta-streaming-five/src/PatchTattooSort.cpp`.
- Tattoo shop data structures: `TheeBabyGoat/GameSource` `source/Scene/ExtraMetadataMgr.{h,cpp}`
  and `ExtraMetadataDefs.psc` (partial/leaked source — absence there is not proof of absence in the
  real binary).
