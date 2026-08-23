# CLAUDE.md — texoverride

Working notes for Claude on this repo. Everything load-bearing that is not obvious from the code.

## What this is

A FiveM client ASI plugin (C++, MinHook) that draws the user's own `.ydd`/`.ytd` files on their
ped, and replaces weapon drawables and animation dictionaries at the streaming layer. Files go in
a `tex_overrides/` folder next to the plugin; the plugin registers them at the game's streaming
layer, doing at runtime what a server `stream/` folder does. It never edits or re-encrypts game
archives, sends nothing to the game server, and other players see no change.

It exists because FiveM's own client mod paths (mods/ folder, pseudo-DLC wrapping, caret naming)
cannot deliver ped textures — they drain before the connect handshake, and the pseudo-DLC mount is
non-overlay. This works at the streaming layer instead.

Repo: github.com/blancodagoat/texoverride (owner blancodagoat). Single-file plugin: `dllmain.cpp`.
Latest tag at time of writing: v0.8.7 (0.8.8 is in the changelog, not tagged yet). Only loads on `sv_pureLevel 0` servers (level 1 needs a Cfx
signature a self-built ASI lacks; level 2 disables the ASI loader).

## The four override mechanisms (all in dllmain.cpp)

1. **Clothing** — `tex_overrides/<collection>/<file>.ydd|.ytd`. The folder names a freemode-ped
   collection (see COLLECTIONS.md, 186 valid names). Registered under `collection/file` and
   re-asserted every second so DLC mounts and FiveM's own loader can't steal the slot back.

2. **Bare-name overlays** — `tex_overrides/<name>.ytd` at the ROOT, no folder. Skin, tattoos,
   facepaint, beards, decals — loose txds that stream by filename alone. Registered under the bare
   filename, exact-name match. `.ydd` is never accepted at root (models always belong to a
   collection). No name whitelist: a scan of every overlay rpf found ~100 unrelated naming
   families plus arbitrary server packs, so the gate is type + exactness (`isAllowedKey`).

3. **Weapon drawables** (0.8.8, PR #11 by chocomintw) — `tex_overrides/<name>.ydr` at the ROOT, no folder. The file name must
   start with `w_`. Every GTA V weapon — base game and DLC — uses this prefix (w_pi_, w_ar_,
   w_sg_, w_sr_, w_sm_, w_mg_, w_ex_, w_me_, w_lc_, etc.), and server-added weapons follow the
   same convention. The prefix is what separates weapon drawables from vehicle parts, prop
   drawables, building pieces and any other `.ydr` in the game. Texture dicts (`.ytd`) for weapons
   go through mechanism 2 and already worked before 0.8.8.
   **UNVERIFIED: whether a VANILLA weapon slot can be claimed.** The PR's README text asserted it
   could, citing the animations rule, which actually says the opposite (a file that shipped with
   GTA never passes through `registerRawStreamingFile`, which is why the plugin CALLS it itself for
   base clothing). Server-streamed weapons are fine by the same argument as `.ycd`. The README now
   states both and asks for a log; replace that wording the moment one shows up either way.

4. **Tattoo placement** — `tex_overrides/<name>.xml` at the root (an edited copy of a pack's
   `overlays.xml`). Moves/resizes/rotates tattoos by patching the parsed values inside the game's
   `PedDecorationManager`. Data writes only, no code touched. See "Placement solver" below.

## Safety gates (do not weaken without asking)

- **The folder-name whitelist is GONE as of 0.8.5** (user call, 2026-08-22, after a server dog
  named `caninesd` was refused). A slotted key now passes if the collection is freemode/`a_c_` OR
  the FILE is named like a ped part (`isPedComponentFile`: 12 component slots, 11 prop anchors,
  then 3 digits). That is what actually stops a vehicle txd or map file landing on a ped, which is
  what the folder list was really buying. Story/cutscene peds stay refused by prefix
  (`isBlockedCollection`: cs_ csb_ ig_ hc_ player_ non-freemode mp_ ...), so the README promise
  holds. Server peds seen in the field: canine, caninepd, caninesd, caninefd, blackcat, browncat.
- Root files: `.ytd` any name; `.ycd` any name; `.ydr` name must start with `w_`; `.ymt` any name
  except the 8 vanilla animal names. Placement `.xml`: fingerprint match required (below).
- The safety gate exists because filename-alone matching would cross-wire unrelated items. Keep it.
- **Log levels are a user contract, not a tidiness knob** (0.8.8, chocomintw's framework in #12,
  combined with our collection-map fix). `logMessage(level, category, ...)` behind LOG_INFO/WARN/
  ERROR/DEBUG, one lock (`g_logCs`) because the hook runs on more than one thread, and DEBUG off
  unless `_debug.txt` exists in tex_overrides. The rule: **anything the README's log table
  documents has to be INFO**, because the log is a support tool for players, not a dev trace. #12
  had REDIRECT, PLACEMENT and RECLAIM at DEBUG; those are the three "did my file actually get
  used" confirmations, one per override mechanism, and behind a debug switch nobody knows about
  the default log stops answering the only question people ask. Promoted back. Caps announce
  themselves (REDIRECT at 100) instead of truncating in silence.
- **The `collection:` map must keep logging names it REFUSES** (`OTHER - never touched`). PR #11
  cut those lines as noise, which is backwards: a refused name that is not logged looks identical
  to one the server never streamed, and a user's log is the only channel through which a naming
  family we do not know about ever reaches us. `caninesd` arriving that way is what killed the
  folder whitelist in 0.8.5. What WAS broken is the label, and that is fixed: the tag now comes
  from the collection (isPedCollection / isBlockedCollection / "depends on the file names inside")
  instead of from `isAllowedKey` on whichever file streamed first, root files are listed under a
  `file:` heading instead of being pushed through `collectionOf`, and the 500 cap announces itself
  rather than truncating in silence.
- **`.ymt`: refused for the 8 vanilla animal names, allowed for everything else** (0.8.5).
  SETTLED, do not reopen. The mechanism is finally known: a `.ymt` whose name the game has NEVER
  seen registers fine (a live server pushes ~12 of its own clothing-pack `.ymt`s through the same
  call every session, logged as `collection: ... .ymt`), while a `.ymt` whose name the game ALREADY
  owns faults three frames inside `registerRawStreamingFile` (ink-island-iowa,
  `GTA5_b3751.exe+16765E0`). The `.yft` beside it survives because a fragment and a metadata file
  land in different stores, and only the metadata store dies this way. All 8 animals ship a vanilla
  `.ymt` (verified, docs/ped_collections.tsv), so an animal `.ymt` always collides and can never
  win. Timing does not save it: 0.8.4 deferred them to the main-thread pump a minute into the
  session and it still crashed, and that deferral was reverted in 0.8.5.
  Dead ends, do not redo: a WebFetch summary claiming "no streaming module for .ymt" INVENTED its
  quotes; Cfx's `GetStreamingIndexForName`/`GetStreamingIndexForLocalHashKey` exports only know
  names FiveM itself registered, so they cannot answer "does the game own this slot".

## How the hook works

Hooks ONE game function, `registerRawStreamingFile`, in the GAME module (`GTA5.exe`) only, never
FiveM's DLLs (a game-module hook is the same surface trainers use). The rule stands but the
REASON was misattributed until 2026-08-21: `legitimacy` is Discord/Discourse/Steam/ROS auth, not
anti-tamper. The real gate is the component manifest mismatch fatal in `DllGameComponent.Win32.cpp`. Byte pattern from Cfx's open source
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

A file added mid-session whose name the CONNECTED SERVER already streams cannot be claimed:
`registerRawStreamingFile` refuses a slot that already holds a handle, so `liveRegister` gets
id=0xFFFFFFFF. Cfx hits the same wall and answers it by writing pgRawStreamer handles straight into
the entry (LoadStreamingFile.cpp: `handle == 0` -> register, else overwrite + handle stack). We have
no `pgRawStreamer::RegisterFile` pattern to mint a handle with, so the log tells the user to restart
(startup claims the slot before the server mounts). Investigated and DECLINED as not worth a new
pattern 2026-08-20; do not re-diagnose this as a bug.

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

CRASH SAVER: covers BOTH paths since 0.8.1. Startup registration journals ONE key at a time
(`journalOne`) around each `o_regRaw` call, so a fault inside the game's own register function
quarantines exactly the file it was holding; the journal is deleted when the loop finishes. Live
batches journal to tex_overrides\_inflight.txt before the game thread touches them;
journal persists 30s after apply. Crash inside the window → next launch appends those keys to
_quarantine.txt, and scanDir/rescanTree refuse them until the user deletes that file. Orderly
exit deletes the journal in DLL_PROCESS_DETACH (a real crash never runs it). New patterns used:
getRawStreamer + pgRawStreamer::GetEntry (both from Cfx LoadStreamingFile.cpp).

## Streaming-cost audit + budget raiser (v0.5.2 / v0.6.0)

Texture loss ("stuck low LOD, black walls, restart needed") = the game's VRAM budget running dry;
eviction inside GTA5.exe is passive-only, so the pool saturates at any ceiling (open cfx issue
#3874). Two answers in the plugin:

- **Size warning (was a hard gate, 0.5.3 to 0.8.0)**: over 32 MB in either resource segment now
  logs a `HUGE` line and loads anyway. It used to refuse. The user reversed that 2026-08-22: a
  refusal makes a pack silently half apply, which reads as a broken plugin and costs more support
  than the crash. The crash it guarded is real (summer-maine-steak, twice: 64 MB graphics
  segments, then 77+ MB virtual), so if oversized-file crashes come back, `cannotLoad` in
  dllmain.cpp is where the refusal goes. Unreadable files are still refused, since there is
  nothing to load. The startup crash saver is what replaces it: whatever the bad
  file is, oversized or not, it costs one launch instead of every launch.
- **Cost audit (0.5.2)**: every .ytd/.ydd on disk is an RSC7 resource; header dwords 2/3
  (system/graphics flags) encode the exact resident memory via CodeWalker's `GetSizeFromFlags`
  (`rscSizeFromFlags` in dllmain.cpp — pages sum x (0x200 << shift)). scanDir totals it, logs
  `pack cost when fully loaded` + `HEAVY` lines for files >= 8 MB (catches 4K anything and 2K
  uncompressed; vanilla clothing txds are under 2 MB).
- **Budget raiser (0.6.0 opt-in, AUTO by default since 0.7.0)**: the budget is a data table in GTA5.exe — 20 rows x 4 uint64
  (half / 1.5th / full / full per texture-quality tier). FiveM fills it in
  PatchExtendedBudgeting.cpp (3 GB x slider multiplier, slider = vid_budgetScale, console-locked
  in production) and rewrites it on settings changes. `_budget.txt` in tex_overrides (a number of
  GB) -> pattern `4C 63 C0 48 8D 05 ? ? ? ? 48 8D 14` (address at +6, Cfx's own), sanity check
  the row shape before any write (`vramTableSane`, SEH), clamp to DXGI dedicated VRAM, then
  re-assert each beat (`budgetBeat`). Aligned 8-byte data writes only; fault disables just this
  feature.
  **The "no silent default" rule was reversed by the user 2026-08-20** after reports that texture
  loss hits high-end PCs identically. It does, and the reason is that FiveM's ceiling has NO VRAM
  term: `SetGamePhysicalBudget(3 * GB)` x `(vid_budgetScale/12 + 1)`, where **`GB` in that file is
  `1000 * 1024 * 1024`**, not 1e9 and not 1<<30. Slider defaults to 0 -> 3145728000 = 2.93 GiB;
  maxed at 20 -> 8388608000 = 7.81 GiB (both verified against real logs, do not re-derive). The
  slider IS user-settable in Settings > Graphics; only the convar is console-locked. A 24 GB card
  and an 8 GB card get the identical ceiling at every setting. The old objection (past real VRAM, D3D11 demotes and stutters)
  is now answered by the OS instead of a guess: `probeVram()` reads
  `IDXGIAdapter3::QueryVideoMemoryInfo(0, LOCAL).Budget` — WDDM's own per-process figure, which
  already subtracts everything else on the GPU, and which MSDN says is exactly the line past which
  a process gets paged out and stutters. `autoBudget()` holds back `max(12.5%, 2 GiB)` of that (a quarter was double-counting: the
  figure DXGI returns is ALREADY this process's share, and on a real 11.7 GB machine it produced a
  raise of 0.2 GB over what the game had set),
  quantizes to 256 MB, and returns 0 (no change) when it would not beat what FiveM already set.
  **`probeVram()` MUST NOT run from `Setup()`**: that is inside DllMain, under the loader lock, and
  DXGI factory creation there comes back empty. 0.7.0 shipped that way and every log said "cannot
  read this card's memory"; the 0.6.x opt-in path had the same latent bug, invisible because a zero
  only skipped a clamp. Worse in the field: one 0.7.0 player logged `FAULT during startup
  C0000005` with the log stopping exactly at the budget block, and another crashed with a
  crash_hash of `gtavupscaler.asi+6616FB`, an unrelated ASI that also hooks DXGI. That player kept
  the upscaler, moved to 0.7.1, and stopped crashing. Forcing DXGI to initialise under the loader
  lock while another plugin wires up its own DXGI hooks is the mechanism for both, and Cfx blames
  whichever module the fault address lands in, not the trigger. `decideBudget()` now runs it from
  the top of `BeatLoop`, the first code off the lock, and is SEH-wrapped since 0.7.2 (out there a
  fault kills the game, not just the plugin). Probed ONCE there, not per beat. See the `ponytail:` note on `autoBudget` for the upgrade
  path if alt-tabbing turns into stutter reports. `_budget.txt` still overrides; `0` in it disables.

## Paths handed to the game MUST be UTF-8 (0.8.3, a total-failure bug)

Cfx's `ToWide` (`code/client/shared/Utils.cpp`) runs `utf8::replace_invalid` over EVERY narrow
path FiveM is handed, so FiveM reads our paths as UTF-8. The scan builds them with the ANSI Win32
APIs. For an ASCII path those are the same bytes, which is why this survived eight releases. One
non-ASCII letter in the Windows username and the ANSI bytes are invalid UTF-8, get replaced with
U+FFFD, the path stops existing, and EVERY claim returns `id=4294967295 handle=00000000` with a
log that looks perfectly healthy. Reported in issue #2 on a Turkish username, 424/424 files
failing; the reporter (akaloi) proved it with a second Windows account with an ASCII name.
`Ov::gfile` holds the UTF-8 form and is the ONLY form `o_regRaw` is ever given; `Ov::file` stays
ANSI for our own file I/O, which is correct on the user's code page. Pure-ASCII paths share one
string. Anything new handed to the game takes `gfile`. Hits any username with á é ö ü ł ñ or CJK.

## The crash saver, and why 0.8.1's version never fired

Two halves, both needed:
- `journalOne` writes the key to `_inflight.txt` around each `o_regRaw` in the STARTUP loop;
  `journalWrite` APPENDS live batches (0.8.6: it used to open `"w"`, so a second batch inside the
  first's 30s window truncated the first out and a crash then quarantined the wrong file or none).
- `DLL_PROCESS_DETACH` deletes the journal, and 0.8.1 guarded that with nothing but a comment
  claiming "a real crash never reaches this line". FALSE: FiveM catches the fault, shows its crash
  dialog, uploads, then tears the process down, and that path runs detach. The plugin was erasing
  the name of the file that had just killed the game, so the next launch crashed identically
  forever. `g_journalHot` blocks the delete while the startup loop owns the journal (0.8.2).
CONFIRMED WORKING IN THE FIELD: a player's 0.8.4 log opens with
`QUARANTINED a_c_shepherd.ymt`, then boots and runs. Do not "simplify" the detach guard away.

## docs/ped_collections.tsv + scratch/pedscan (the "is this folder real" oracle)

469 ped collections in b3751, with component/prop counts and source rpf, scanned out of the game
with CodeWalker.Core. `scratch/pedscan` is the throwaway tool, kept, with a README: it also has a
substring-find mode which is how we proved `canine` appears NOWHERE in GTA V and that all eight
animals ship a root `.ymt`. 120 freemode + 8 animal + the rest story/cutscene. Regenerate after a
game update.

## Server-added peds (0.8.5, why the folder whitelist died)

Servers name their own peds anything. One real server: dogs `canine`, `caninepd`, `caninesd`,
`caninefd`; cats `blackcat`, `browncat`. None exist in GTA V. Confirmed working in game on
`caninesd` with a Belgian Malinois head. A server pet-list screenshot is the fastest way to get
the names; the log's `collection:` lines are the other way.

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
- **The user ships under the handle `blancodagoat` only. Never put their real name, email, or
  local paths in the repo, the version resource, LICENSE, or anything shipped.** Stated 2026-08-20:
  doxxing is a real risk in the FiveM community. If code signing ever comes up, an OV/EV cert on an
  individual publishes their legal name in every signature; signing under a registered entity puts
  the entity name there instead. Verified clean 2026-08-20: no email or user paths in tracked files,
  no build paths in the binary (no /Zi, so no PDB path), commits use the GitHub noreply address.
- **AV false positives are expected and are never worked around.** RWX trampoline memory
  (minhook buffer.c), a 5-byte inline patch into GTA5.exe, module pattern scanning, an unsigned
  low-prevalence PE: every generic-trojan heuristic at once. `texoverride.rc` carries a version
  resource since 0.7.0 (its absence was itself a Defender ML weighting factor) and the README has a
  "Why your antivirus may call it a trojan" section pointing at VirusTotal, the public CI build,
  self-building, and Microsoft's FP submission form. NEVER obfuscate, pack, or otherwise evade
  detection: it makes scores worse and it destroys the readable-source argument the project rests
  on. Code signing is the only real fix and costs money; not proposed unless the user raises it.
- The "Ban risk, stated plainly" README section stays — the 5-byte patch lives in game code all
  session and a scan can flag it; the honest disclosure is what makes the project credible.

## Build & release

- **Builds are reproducible (0.8.6).** `/Brepro` on the link line, and CI builds twice and compares
  SHA-256 before publishing. Verified: two clean local builds are byte-identical. This is
  load-bearing for the AV story, because "read the source, build it yourself and compare" is only
  worth something if the binary is actually checkable. Do not drop the flag or the CI step.
  **NEVER `sed -i` build.bat**: it strips the CRLF line endings that batch `for /f` needs and the
  build dies with "'M' is not recognized". Edit it byte-wise (python, `open(p,'rb')`).
- `build.bat` — MSVC (VS Build Tools 2022, "Desktop development with C++"). `/MT /EHsc /std:c++17`,
  no /clr. The user's shell prints `fastfetch`/`vswhere` noise and exits nonzero even on success;
  trust the `Built texoverride.asi` line, not the exit code.
- `.asi` is gitignored — CI builds it and attaches it to the GitHub Release. Never commit the .asi.
- Release: bump `TEXOVERRIDE_VERSION` in dllmain.cpp AND `FILEVERSION`/`PRODUCTVERSION`/the two
  version strings in `texoverride.rc` (they must match), date the CHANGELOG section, commit, push,
  `git tag vX.Y.Z`, push the tag. CI builds both the push and the tag; the tag build makes the
  release with the changelog notes + the .asi.
- **The FX_ASI_BUILD stamp** (`texoverride.rc`): FiveM refuses ASIs on game build 2189+ that don't
  claim the running build. One `FX_ASI_BUILD <build> BEGIN "\0" END` line per supported build
  (currently 3751, 3788). New game build → add a line or the plugin silently stops loading.
- Installing over a running FiveM: the loaded .asi is locked. Rename the old one aside
  (`mv ...asi ...asi.old`) then copy the new one in; delete the .old after FiveM closes.

## Contributors and PR handling (2026-08-22)

`chunguscodes` is an active outside contributor who forked and sends real fixes. One 1052-line PR
was declined with a list of what to split; they came back with four small ones and all four were
good. That worked, so keep asking for splits.
Applied in 0.8.6 (cherry-picked, not merged, because 0.8.3-0.8.5 had rewritten the same
functions): MinHook failure handling + thread handle leaks (#8), the live-reload journal/queue
fixes (#6), `/Brepro` + CI compare (#7).
Still open, both with review comments: **#5** loader-lock work, good, but it moves `decideBudget()`
behind the whole scan; asked for it to sit right after `locateRuntimePatterns` instead. **#9**
occupied-slot recovery via `GetStreamingModule` export + FindSlot vtable; its constants check out
(`moduleMgr` at 0x1B8 matches Cfx's `Streaming.h` and its own `NumPendingRequests == 0x1E0`
assert), it fixed the entry-zero write I flagged, but the bug it was written for turned out to be
the UTF-8 path bug. Declined pending a post-0.8.3 log showing claims still returning -1.
Review lesson: BOTH of my first-pass diagnoses this session were wrong, and a WebFetch summary
INVENTED source quotes. Pull the raw file and grep it; never cite a summarizer.

## FiveM source facts (verified this session, for future work)

- `registerRawStreamingFile` pattern + constant flags: `gta-streaming-five/src/Streaming.cpp`.
- Handle-overwrite override path: `gta-streaming-five/src/LoadStreamingFile.cpp`.
- Plugin load timing: `LauncherInterface::PostLoadGame` in `code/client/citigame/Launcher.cpp`,
  before the game entry point runs. asi loader: `components/asi-five/src/Component.cpp`
  (deny-by-exception blacklist: openiv.asi, scripthookvdotnet.asi, fspeedometerv.asi, Gears.asi,
  .NET assemblies).
- Mod packages: `components/citizen-mod-loader-five/src/` (ModPackage.cpp, ModVFSDevice.cpp) —
  OpenIV assembly.xml format, pure level 0 only. **It CANNOT register data files or pseudo-DLCs**
  (that claim was wrong here until 2026-08-21). Its entire parser vocabulary, by string dump, is
  `package / Five / content / archive / path / add / source`: insert files into archives that
  ALREADY exist, nothing else. No `dlclist`, no `dlcpacks`, no `setup2.xml`, and
  `createIfNotExist` is parsed by nobody. DLC mounting lives in `citizen-level-loader-five`, a
  different component, for maps. Consequence: any dlcpack has to be converted by merging its data
  into the vanilla files it extends (done once for a vehicle-audio pack: CodeWalker `RelFile`
  round-trips `.rel` byte-identically, so `RelDatas` concat + dedupe + `Save()` works, and pack
  entries must OVERRIDE vanilla on hash collision or the replaced vehicles keep stock audio).
- PedDecorationManager pattern + struct: `gta-streaming-five/src/PatchTattooSort.cpp`.
- Tattoo shop data structures: `TheeBabyGoat/GameSource` `source/Scene/ExtraMetadataMgr.{h,cpp}`
  and `ExtraMetadataDefs.psc` (partial/leaked source — absence there is not proof of absence in the
  real binary).

## Ped overlays on REMOTE peds (researched 2026-08-21): the ASI is NOT the fix

A player reported overlays/tattoos rendering on their own ped but missing or flickering for a few
seconds at certain camera angles on OTHER players, server-wide, while other FiveM servers are fine.
16-agent research pass with adversarial verification. Do not re-derive any of this.

**The actual fix is a server convar.** `setr game_enableStableOverlaySort true` (ConVar_Replicated,
default false, undocumented on docs.fivem.net). The game sorts decoration collections with a
comparator that SUBTRACTS two unsigned hashes, which is not a valid total order, so the result depends on
mount order and shifts on every resource restart. Cfx replaces the comparator at
`location + 0x1E` behind that convar (`PatchTattooSort.cpp`). Being undocumented and replicated is
exactly why one server breaks and others do not.

**Cheapest discriminator, zero code:** while the bug is visible, change Texture Quality to force a
D3D device reset (`CPedDamageSetBase::DeviceLost/DeviceReset` rebuilds the decoration render-target
set). Tattoos return => data IS on this client, residency problem. Nothing changes => the data
never arrived and no client-side code can help.

**Why the obvious ASI fix does not work:** fivem#2010 already tried force-requesting the tattoo
`.ytd` on the observing client. The ytd loads and the tattoo still does not appear. Also
`GET_PED_DECORATIONS` is documented by Cfx as returning undefined data for a remote player ped, so
there is NO client-side source of truth to re-apply from. Decorations reach remote peds as packed
32-bit words in `CPlayerAppearanceDataNode`, and the word count for b3751/b3788 is UNVERIFIED
(56 is `#if 0` dead code, 60 is one Stand-OSS snapshot; never write a byte on that).

**Verified facts worth keeping (Rockstar `streamingdefs.h`, cross-checked against Cfx's live
`ReleaseObject(idx, 0xF1)`):**
- `StreamingDataEntry` = `{ u32 handle; u32 status:2, dependentCount:14, flags:16; }`.
  So Cfx's `flags &= ~0xFFFC` clears the DEPENDENT COUNT, not the STRFLAG field. STRFLAGs are the
  top 16 bits. Status: 0 NOTLOADED, 1 LOADED, 2 LOADREQUESTED, 3 LOADING.
- STRFLAG bits: DONTDELETE 1<<0, FORCE_LOAD 1<<1, PRIORITY_LOAD 1<<2, LOADSCENE 1<<3, MISSION 1<<4,
  CUTSCENE 1<<5, INTERIOR 1<<6, ZONEDASSET 1<<7; `STR_DONTDELETE_MASK` = 0xF1.
- **NEVER raw-OR a STRFLAG bit into a live entry.** `SetRequiredFlag`/`ClearRequiredFlag` also move
  the entry between the loaded and persistent lists and maintain `m_numPriorityRequests`; a hand
  write desyncs that and produces a DELAYED intrusive-list crash. Rockstar asserts against it.
  The sanctioned pin is `RequestObject(globalId, 7)` + `ClearRequiredFlag(id, 1)` on teardown,
  which is what GTA V's own MeshBlendManager and Cfx both use. `FORCE_LOAD` self-clears on load.
- Patterns (game module): RequestObject = `get_call(get_pattern("41 B8 14 00 00 00 03 D3 E8", 8))`,
  LoadObjectsNow = same pattern at 0xF, ClearRequiredFlag = `"8B CA 4D 8B 11 45 0F B7 5C CA 06 45"`
  at -0xB. Must run on the update thread (our PeekMessageW pump).
- Preset struct IS published (alexguirre/rage-parser-dumps, struct `0xB8C1BF6F`, size 160,
  consistent b2189..b3442): collection `presets` atArray +0x00 (ptr, u16 count +0x08, u16 cap
  +0x0A), `nameHash` +0x10, `bRequiredForSync` +0x9C. Preset stride 0x40: uvPos +0x00, scale +0x08,
  rotation +0x10, nameHash +0x14, **txdHash +0x18**, txtHash +0x1C, zone +0x20, type +0x24.
  Earlier searches missed it because those dumps use CASE-SENSITIVE joaat of member names.
  No dump exists for b3751/b3788, but our own fingerprint solver re-derives these every session.

**Dead, do not retry:** raising the VRAM budget or TxdStore pool (ruled out upstream AND by the
user's own 7.8 -> 10 GB test); pre-requesting the observer's ytds; re-applying decorations locally;
widening the packed bitfield (wire format, both peers must agree); `gameconfig.xml` (no decoration,
damage-set or blend pool exists in b3751's 193 pools); ScriptHookV (FiveM ships its own, and every
`nativeCall` is refused unless the server sets `sv_scriptHookAllowed`, default false);
PackfileLimitAdjuster / HeapAdjuster / PedProp Limit Adjuster (none touch decorations); static
analysis of the on-disk GTA5.exe (protected, zero rip-relative xrefs; only a runtime dump works).
Client-side stable sort is trivial but MUST NOT ship: the convar is replicated because every client
must agree, so fixing one client swaps missing tattoos for wrong tattoos on wrong body zones.
UNVERIFIED numbers nobody should re-derive: NUM_DECORATION_BITFIELDS, the sorted index array
stride, kMaxCompressedTextures / kMax*BloodRenderTargets / kInvalidPedDamageSet, FindSlot absolute
vtable indices (the +6 shift on builds >= 2802 is verified, the absolute index is not).

## Client-side audio DLCs: SOLVED, and my first conclusion here was WRONG

**CORRECTION (2026-08-21, same day).** An earlier version of this section said client-side audio
DLCs were impossible on FiveM. That was wrong, and the error was mine: I only ever tested `<add>`
NESTED INSIDE `<archive>`, concluded from that the mods folder could only insert into archives that
already exist, and never tried the bare form. A publicly distributed pack proved otherwise.

**THE WORKING RECIPE.** A mods-folder package with a top-level `<add>`, no `<archive>` parent,
whose target is a game-relative path under dlcpacks:

```xml
<content>
<add source="ragengineports.rpf">update\x64\dlcpacks\mods\dlc.rpf</add>
</content>
```

Package layout: `<pkg>.rpf` (OPEN encryption) containing `assembly.xml` plus `content/<dlc>.rpf`,
where `source` is relative to `content/`. `<content>` takes MANY top-level `<add>` entries, so
several DLCs ship as one mods-folder file, each needing its own slot name; later slot wins a clash.
Building one with CodeWalker.Core: `RpfFile.CreateFile` writes the nested rpf and THEN re-scans it,
and that scan throws on an embedded NG-encrypted dlc.rpf even though the bytes are already complete.
Catch it and verify by reopening the package instead. Player-facing writeup:
`docs/client-side-dlc-packs.md`. No vanilla dlcpack is named `mods`, so this CREATES a slot
rather than replacing one, and it needs no dlclist edit. Drop the package in `FiveM.app/mods/`.
Requires pure mode off, same as every other mods-folder package.
Known limitation reported by the pack author: some VANILLA vehicles cannot take custom sound this
way; DLC and add-on vehicles work fine. Consistent with the dlcpack mounting after the base
game.dat has already defined those vehicles.

So: the dlcpack route is NOT closed the way the notes below claim. What IS closed is the Arxan
archive-count limit, dlclist edits, and hash validation of the REAL `update/x64/dlcpacks/` folder on
disk. None of which this touches, because it all happens inside FiveM's virtual view.

**Replacing BUILT-IN audio (weapons, impacts, engines) also has a client-side route, found
2026-08-21 after three failures.** A DLC wave pack whose container folder is named the same as a base
container SHADOWS it, so vanilla config resolves onto your banks with no `.rel` authoring. Ship the
container COMPLETE (the shadow is per container, not per file) or the banks you did not include go
silent. Confirmed working in game. What does NOT work: a mods `<archive>` targeting
`x64\audio\sfx\*.rpf` (not a mappable archive path, dropped silently), and an `addons/` overlay of
`platform/audio/sfx/*.rpf` (mounts, changes nothing). Also note `mods/*.rpf` must be OPEN while
`addons/*.rpf` must be ENCRYPTED, and tool-generated RPFs may need ArchiveFix before the game accepts
them.

The rest of this section is the failed ASI attempt. Its findings about the game's data file mounters
are still accurate and reusable, but it is no longer the route anyone should take.

## The failed ASI route (attempted 2026-08-21): do not retry

Goal: play a vehicle-audio DLC (132 cars) client-side on FiveM. Everything below was proven by
experiment on b3751; the conclusion is negative but most of the machinery is correct and reusable.

**What I wrongly believed at the time.** That FiveM cannot mount a client-side dlcpack at all. The
Arxan count limit, the dlclist refusal and the `dlcpacks/` hash validation are all real, but they
apply to the on-disk game folder, NOT to a file placed through the mods-folder VFS. See the
correction above. The parser vocabulary really is only
`package/Five/content/archive/path/add/source`, but `add` works at the top level of `content` with a
game-relative target, which is the whole trick and which I never tested. Converting the DLC by merging into vanilla fails on
name tables: Dat54 entries reference banks by index into their OWN file's table, so concatenating
`RelDatas` silently mis-points every entry. FiveM never merges; it registers each file separately.

**What was built.** Register the audio files individually with the game's own data file mounters,
the way FiveM's `data_file` lines do, so `audMetadataDataFileMounter` / `audWavePackDataFileMounter`
parse the .rel and wave packs natively. All anchors are published in Cfx LoadStreamingFile.cpp.

**PROVEN CORRECT (reuse freely):**
- `g_dataFileTypes` = data pattern `61 44 DF 04 00 00 00 00`, array of `{uint32 hash, uint32 index}`.
  **It lives in .rdata**, so a scanner that only walks IMAGE_SCN_MEM_EXECUTE sections will never find
  it, hence `scanModuleData()`.
- The table is keyed by the **case-SENSITIVE** joaat of the UPPERCASE type name (`joaatCS`). Our
  ordinary `joaat()` case-folds and does NOT work here. Cfx uppercases before hashing, which is the
  tell. Verified: `AUDIO_WAVEPACK` -> index 144, `type[0] hash=04DF4461`.
- `sm_Interfaces` = `imagebase + *(int32_t*)(pattern "48 63 82 90 00 00 00 49 8B 8C C0 ? ? ? ? 48" + 11)`.
  The instruction is `mov rcx,[r8+rax*8+disp32]`, so this only works because r8 == image base there;
  on b3751 it does. Confirmed by the audio mounters appearing at the right indices.
- The table is filled by the GAME progressively during session init: entirely zero for ~15s after
  the plugin loads, audio mounters present around try 14-24 of a 1Hz poll. Poll for the SPECIFIC
  types needed, not for "any mounter".
- `CDataFileMgr::DataFile` layout and vtable slot 1 for `LoadDataFile` are right: the call reaches
  the real function and runs four frames deep into game code.
- `fiDevice::GetDevice` pattern `41 B8 07 00 00 00 48 8B F1 E8` at -0x1F. NOTE: it returns the
  DEVICE for a path prefix, it does NOT test file existence. Cfx uses
  `device->GetFileAttributes(name) != -1` for that. Do not mistake "RESOLVES" for "file is there".
- Paths must be RAGE paths. `citizen:/` is mounted by FiveM via fiDeviceRelative to
  `FiveM.app/citizen`, so dropping files there makes them addressable with zero new patterns,
  no need for the fiDeviceRelative vtable/object size, which are not published.

**WHERE IT DIES.** `LoadDataFile` on a loose-folder path crashes inside the game's audio loader:
`GTA5_b3751.exe+1308CDD <- +1308CB5 <- +1302979 <- +130284D <- +36FD68 <- texoverride`.
Control experiment that settles it: mounting `lambov10_game.dat151.rel` from the Aquaphobic pack,
a file that demonstrably works as a FiveM audio resource on live servers, produces the IDENTICAL
crash signature. So the files, the merge, the missing .nametable sidecars and the DLC extraction are
all innocent. A loose folder is not a resource; FiveM does something more when mounting one, and
that gap is past where its published source goes.

**Two rules learned the hard way:**
- Data file mounting MUST run on the game's main thread (our PeekMessageW pump). The beat thread has
  no RAGE allocator TLS. This is the same rule already written above for live reload.
- **NEVER wrap a call INTO game code that mutates engine state in SEH.** Catching the access
  violation aborts the game's mounter half way through, the plugin carries on as if fine, and the
  game dies minutes later looking like somebody else's bug. One attempt caught 66 faults, wedged
  loading at 70%, then CTD'd the machine. SEH is for READING memory that might be unmapped. A wrong
  call should crash at the call site, immediately and honestly.

Deliverable if this is ever wanted again: it is a FiveM SERVER RESOURCE, which is a supported path
and about an hour of work (`data_file AUDIO_GAMEDATA/AUDIO_SOUNDDATA/AUDIO_SYNTHDATA/AUDIO_WAVEPACK`,
one wave pack folder per container, configs kept separate and never merged). Singleplayer installs
the dlc.rpf as-is. RAGE MP also loads it as-is, which is why the pack works there and not here.
