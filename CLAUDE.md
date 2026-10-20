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

Repo: github.com/blancodagoat/texoverride (owner blancodagoat). Since 2026-08-25 (PR #17) the
plugin is a `src/` tree (`core/`, `streaming/`, `features/`) with a 48-line `dllmain.cpp`; the
old single file is history at `e638d45`. Latest tag: v0.8.27 (2026-09-12, GTA World collection
folders in the zip plus repo-only work: tests in CI, /W4, Node 24 actions; no plugin code changed).
The last release that changed the plugin is v0.8.26 (2026-09-09, force_reload removed after it
crashed the game; see the 0.8.26 section). Everything through v0.8.27 released and pushed. Only loads on `sv_pureLevel 0` servers (level 1 needs a Cfx signature a
self-built ASI lacks; level 2 disables the ASI loader).

**Sister tools on blancodagoat.dev/dlc-builder (2026-08-25):** "dlc.rpf to tex_overrides"
converts any pack into this plugin's folder layout using a copy of `isAllowedKey` (keep the two
in step; it lives in `dlc-builder/Services/TexOverrideExport.cs`), unlocks NG packs from the
user's own GTA5.exe in the browser, refuses the 23,180 vanilla `.ycd` names, and has a report
button that pings the user on Discord. The GTAW animation fixer marks emotes on vanilla
dictionaries as not replaceable. Forum post: `docs/forum-post-dlc-builder-converter.js`.

## Splitting a pack: rpf for textures, tex_overrides for models (asked 2026-09-07, NOTHING TO BUILD)

A user wanted a `_connection` folder suffix so the plugin would pair `.ydd`s in tex_overrides with
`.ytd`s they had put in `rs_female.rpf`/`rs_male.rpf` inside `update\x64\dlcpacks\patchday28ng`.
The models loaded, the textures never did. All of the below VERIFIED with scratch/pedscan against
the real install, not reasoned from memory:

- **FiveM DOES mount the on-disk dlcpacks.** `dlc_patchDay28NGCRC:/content.xml` appears in every
  CitizenFX log on this machine. I claimed otherwise first and it was wrong: this file's opening
  paragraph scopes "cannot deliver ped textures" to the mods/ folder, pseudo-DLC wrapping and
  caret naming, and says NOTHING about editing the real install. Do not over-read that sentence.
- **The pairing IS the collection folder name, so there is nothing to connect.** In every real
  pack the drawable and its texture sit in the SAME folder: `mp_m_freemode_01_mp_m_heist4/` holds
  both `jbib_000_u.ydd` and `jbib_diff_000_a_uni.ytd`. The streaming key is `<parent>/<file>`,
  exactly the key `slotKeyFor` builds for tex_overrides. A split pack therefore pairs itself as
  long as both halves use the same collection name. A `_connection` suffix would add nothing.
- **TWO candidate causes, not one, and I cannot tell which from here.** (a) An rpf inside a
  dlcpack only mounts if that pack's content.xml lists it by name: patchday28ng's content.xml
  (read with pedscan `cat` mode) has one `<Item>` each for `patchday28ng_female.rpf`,
  `patchday28ng_male.rpf`, `patchday28ng_p.rpf` and the vehicle rpfs, and `rs_female`/`rs_male`
  appear NOWHERE in the game or in this machine's mods folder, so an undeclared copy never mounts.
  (b) The collection folder inside the rpf does not match the one in tex_overrides, which is the
  same failure with the rpf mounting fine. The test that splits them: move ONE texture out of the
  rpf into tex_overrides beside its model. Shows up -> the rpf is not mounting; still missing ->
  the names disagree.
- **NOT verified end to end: nobody has seen a split pack render.** The user's own install has
  zero split cases (checked: every `.ytd` in tex_overrides has its `.ydd` beside it), and I do not
  launch FiveM. The reasoning is sound but it is reasoning.
- **A THIRD cause, and the one that actually bit (2026-09-08). The dlcpack may ship no ped
  content at all**, in which case neither (a) nor (b) is the question. A user put his `.ytd`s in
  `update\x64\dlcpacks\patchday28ng\...\mp_m_freemode_01\` and his `.ydd`s in
  `tex_overrides/mp_m_freemode_01/`, same collection name on both sides, and the models loaded
  while the textures never did. `docs/ped_collections.tsv`: **patchday28ng sources 0 of the 469
  ped collections**, and `mp_m_freemode_01` is not a dlcpack collection in the first place, it is
  `streamedpeds_mp.rpf` inside x64v.rpf with 5,868 component files. DLC packs never EXTEND
  `mp_m_freemode_01`; they create a sibling (`mp_m_freemode_01_mp_m_2023_02`,
  `..._mp_m_heist4`). So a hand-made `mp_m_freemode_01` folder inside a patchday pack is not a
  path the game reads ped collections from, declared rpf or not, and nothing ever registers those
  names. His models worked for an unrelated reason: the plugin registers them BY NAME straight
  into the store and bypasses the DLC collection machinery entirely. That asymmetry is the whole
  bug report. Answer is always the same: put the `.ytd`s in tex_overrides beside the `.ydd`s.
- **The pairing claim is now quantified, not just asserted.** Of the 142 collections in b3751 that
  hold `jbib_000_u.ydd`, all 142 hold `jbib_diff_000_a*` in the SAME folder (counted from
  `scratch/pedscan/find_jbib_000_u.txt` and `find_jbib_diff_000_a.txt`). There is no separate
  pairing mechanism anywhere.
- **The "cannot even DETECT this" line below is OUT OF DATE as of 0.8.24.** It was true when
  written and is not now: `targetStreamingId` -> `findModuleSlotSafe` asks the GAME's own store
  through `GetStreamingModule(ext)` + `FindSlot`, not `GetStreamingIndexForName`, so it sees a
  DLC-registered name fine and does not care who registered it. The FindSlot vtable index it
  needs is no longer unverified either: it resolved all 682 claims in a real session and
  correctly followed 44 names the server moved off our indices. PR #9 was declined because that
  path was unproven; that reason is gone. A "textures missing for this collection" warning IS
  buildable now (probe the txd store for the `_diff_` names of every `.ydd` we register into a
  collection, report the ones the game does not know). OFFERED 2026-09-08, not built, nobody has
  said yes.
- Historical, kept so the reasoning is not re-derived: Cfx's `GetStreamingIndexForName` only knows
  names FiveM itself registered, so THAT export cannot see a DLC-registered `.ytd`.
- **The split does NOT cut VRAM, but it does cut the plugin's own work**, which is worth saying
  because the motivation was "everything inside tex_overrides would bug your entire game out".
  Resident texture memory is identical either way, both routes land in the same streaming index.
  What differs is per-file: startup registers and journals each file one at a time, and the beat
  walks EVERY override once a second under `g_cs` (`for (auto& ov : g_ovs)` in core.cpp, two
  entries checked per override, written back only when contested). 6,000 loose files is 12,000
  slot checks a second holding a lock that the watcher also wants; a packed rpf costs none of it.
  Nobody has measured it. If a scale report ever arrives with a log, measure that loop first.

## The four override mechanisms (in src/)

1. **Clothing** — `tex_overrides/<collection>/<file>.ydd|.ytd`. The folder names a freemode-ped
   collection (see COLLECTIONS.md, 186 valid names). Registered under `collection/file` and
   re-asserted every second so DLC mounts and FiveM's own loader can't steal the slot back.

2. **Bare-name overlays** — `tex_overrides/<name>.ytd` at the ROOT, no folder. Skin, tattoos,
   facepaint, beards, decals — loose txds that stream by filename alone. Registered under the bare
   filename, exact-name match. `.ydd` is never accepted at root (models always belong to a
   collection). No name whitelist: a scan of every overlay rpf found ~100 unrelated naming
   families plus arbitrary server packs, so the gate is type + exactness (`isAllowedKey`).

3. **Weapon and prop drawables** (0.8.8 weapons, PR #11 by chocomintw; props unreleased,
   2026-08-25) — `tex_overrides/<name>.ydr|.yft` at the ROOT, no folder, ANY name. 0.8.8 took
   only `w_*` `.ydr`; a user copied a prop pack (prop_cs_cardbox_01.ydr, p_notepad_01_s.ydr,
   v_ilev_shrf2door.ydr, onx_pol_m_screen_lspd1.yft ...) into a folder and got a SKIP per file
   with the ped-naming message. A bare exact name IS the slot, so there is nothing to cross-wire,
   same argument as `.ytd`/`.ycd`; the `w_` prefix bought nothing. Folder SKIPs for loose types
   now say "go straight into tex_overrides". Texture dicts (`.ytd`) go through mechanism 2.
   **VERIFIED 2026-08-25: a VANILLA `.ydr` slot CAN be claimed.** `prop_beer_bottle.ydr` and
   `prop_beer_logger.ydr` (both ship with GTA) at the root got `OVERRIDE-REG ... id=244189/244190`
   on b3751, GTAWorld, and the user saw the new models in game. Same store and same startup claim
   as clothing, so vanilla weapons work by the same mechanism; README no longer hedges.

4. **Tattoo placement** — `tex_overrides/<name>.xml` at the root (an edited copy of a pack's
   `overlays.xml`). Moves/resizes/rotates tattoos by patching the parsed values inside the game's
   `PedDecorationManager`. Data writes only, no code touched. See "Placement solver" below.

## Weapon sounds: SETTLED 2026-08-25, the plugin can NEVER take `.awc`. Do not reopen.

Verified against raw Cfx source (ultracode pass, 5 agents, opus re-fetched every citation):
- Wave banks have NO streaming slot. Cfx's only `.awc` code, `PatchAudioWavePackOverlay.cpp`
  (344-392), mounts a custom `fiDevice` at `awc:/<container>/` and passes the game's own
  `AddWavePack` that path. Banks resolve by file device + container name, never by
  name -> id -> handle, so `registerRawStreamingFile` has nothing to claim; a call that looked
  successful would be inert. Its shadow list is exactly `"RESIDENT"` and `"WEAPONS_PLAYER"`,
  which is why the mods-package recipe below works.
- Servers do not put `.awc` in `stream/`; the recipe is `files{}` + `data_file 'AUDIO_WAVEPACK'
  'audio/sfx/resident'` (forum t/71581 #12, t/5157503 #33, quoted verbatim), the mounter route
  that bypasses the hook. The `_debug.txt` probe below is predicted to show zero `.awc` lines.
- Whether GTA5.exe even has a streaming module for `awc` is UNKNOWN from source; handing
  `o_regRaw` such a name is the `.ymt` crash shape. Never add a `.awc` gate carve-out.
Only deliverable left: a dlc-builder converter that wraps a folder of banks into the mods package.

**2026-08-29 addendum, and it does NOT reopen the above.** Everything in this section is about
`registerRawStreamingFile`, and all of it stands. What is new is that Cfx EXPORTS the mounter
route these notes already point at: `gta-streaming-five!ForceMountDataFile(pair<type, path>)`
calls Cfx's own `HandleDataFile` immediately. See the open leads in the exports section. Untested,
and it may still turn out the game has no wavepack mounter registered when we would call it.

### Original notes (kept for the recipe)

The user's working pack is `FiveM.app/mods/ZZ_WeaponSounds_merged.rpf`: assembly.xml with one
top-level `<add source="wepsnd.rpf">updated\dlcpacks\wepsnd\dlc.rpf</add>`, and inside it
`x64/audio/sfx/resident/` (16 banks) + `x64/audio/sfx/weapons_player/` (22 banks), both containers
COMPLETE. That is the shadow-container recipe from the audio section below, and it works.

Why the plugin cannot just take `.awc` files: a `.awc` inside an rpf is registered by the packfile
scan, not by `registerRawStreamingFile`, so the hook never sees vanilla banks, and FiveM's own
audio route (`data_file AUDIO_WAVEPACK` on a folder, `files {}` not `stream/`) goes through the
game's wavepack mounter, also not the raw call. Cfx's LoadStreamingFile registers any extension
that has a streaming module, so a server that puts `.awc` in `stream/` WOULD pass through the
hook; nobody has checked whether GTAW does. Zero-code probe: create `_debug.txt`, play one
session, grep the log for `.awc` under `Server file:`. If they appear, `.awc` at the root is the
same change as props (gate only, name = slot, container/bank key would need the folder form
`resident/weapons.awc`, so the folder rule needs a `.awc` carve-out). If they do not, the only
client route is the mods package, and the right tool is a converter on dlc-builder that wraps a
folder of banks into that package (recipe proven, CodeWalker.Core `RpfFile.CreateFile`, see the
audio section for the re-scan throw). Not built; the user has not said which they want.

## Safety gates (do not weaken without asking)

- **The folder-name whitelist is GONE as of 0.8.5** (user call, 2026-08-22, after a server dog
  named `caninesd` was refused). A slotted key now passes if the collection is freemode/`a_c_` OR
  the FILE is named like a ped part (`isPedComponentFile`: 12 component slots, 11 prop anchors,
  then 3 digits). That is what actually stops a vehicle txd or map file landing on a ped, which is
  what the folder list was really buying. Story/cutscene peds stay refused by prefix
  (`isBlockedCollection`: cs_ csb_ ig_ hc_ player_ non-freemode mp_ ...), so the README promise
  holds. Server peds seen in the field: canine, caninepd, caninesd, caninefd, blackcat, browncat.
- Root files: `.ytd` any name; `.ycd` any name; `.ydr`/`.yft` any name; `.ydd` only `a_c_`;
  `.ymt` any name except the 8 vanilla animal names. Placement `.xml`: fingerprint match required
  (below).
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
fault disables the whole feature (`placementBeatSafe`).

**2026-08-29: the fingerprint only has to succeed ONCE per session.** Every decoration collection
is the same C++ type, so the layout one file solves is the answer for all of them. The first
successful fingerprint publishes it, and every later collection is confirmed by its own preset
name hashes alone (all of them, in order, 32 bits each), consulting no values at all. That retires
both documented limits: a file with one or two presets works, and a file where every value was
edited works, as long as some other file in tex_overrides fingerprinted first. A thin file on its
own still cannot solve, and now says so once instead of being refused at parse time. `joaat`
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

## hide_overlay: FiveM's own overlays, out of screenshots (RELEASED in 0.8.16, 2026-09-01)

`src/features/clean_shot.{h,cpp}` plus `cfxDetachOwned` in `src/core/cfx.cpp`. Setting
`hide_overlay` in `_settings.txt`, default `no`, value a comma list of screenshot keys
(`printscreen, f9`). CONFIRMED IN GAME 2026-09-01 22:29 on the user's localhost server: five
presses, each `screenshot key vk=.. pressed` -> `2 handler(s) taken off the event` -> 1.5 s ->
`put back on`, ShareX shots clean, server UI untouched.

**`always` mode: cut 2026-09-01, private-only patch that evening, PUBLIC in 0.8.18 (2026-09-02,
user call: "fuck it let's add the full hide to the public version").** The 0.8.16 notes had said
there would never be one; the 0.8.18 notes say the reversal out loud. `hide_overlay = always`
sets `g_hideUntil` to INT64_MAX and pushes a 0 placeholder into `g_keys` that the poll skips.
The position now: it is the player's own screen only, the server and other players see
nothing, no code is patched. The old "never re-add it" line is gone with the patch file
(`scratch/private-always.patch` deleted); the user's install runs the public build again.

**Mechanism: two nodes come off `OnPostFrontendRender` and go back.** `rage-graphics-five`
exports `?OnPostFrontendRender@@3V?$fwEvent@$$V@@A`; `citizen-mod-loader-five` (the `%d mod packs
loaded` counter, order -1000) and `font-renderer` (the version watermark, order 1000) both draw
from it. Our node connects in `Setup()` (the 0.8.15 rule) and always returns true; on the render
thread, inside our own handler, `cfxDetachOwned` unlinks the nodes whose handler is owned by
those two DLLs while the gate is shut and re-inserts them by Cfx's own order rule when it opens.
A node is identified by its std::function impl vtable (+0x38 -> vtable -> `GetModuleHandleEx
FROM_ADDRESS`), which the dev-build dump proved for all 13 nodes. The render thread is the only
walker and it reads `next` right before each call, so a single pointer store is always seen
whole. Nothing is freed or copied; a parked node keeps its own `next`.

**0.8.21 (2026-09-03): parking the watermark node broke DUI screens; a stand-in node fixes it.**
Third-party report via the user: a DUI TV drew exactly ONE triangle of its quad (diagonal cut)
with `hide_overlay = always`, whole with `hide_overlay = no` and the plugin otherwise on
(CONFIRMED by the reporter 2026-09-03). Log clean: 72 clothing files, 0 redirects. Read out of
Cfx source (raw files, not summaries): the font-renderer order-1000 handler queues the watermark
and then calls `g_fontRenderer.DrawPerFrame()` (GtaGameInterface.cpp), which (a) is the LAST im
draw of every frame in stock FiveM, (b) is the ONLY flush of every glyph run/rectangle queued
through `TheFonts` by any component (BindNetLibrary reconnect warning, mod-loader counter), and
(c) is the only caller of `FrpSeqAllocatorSwapPage` (three 8 MB bump pages, never reset
otherwise). With the node parked, the frame no longer ends with an im draw and the next frame's
screen quad lost a triangle; the exact game-side state involved was NOT identified (candidates
looked at and cleared: the DUI flip in NUIWindow::UpdateFrame, which saves/restores what it
touches; InvokeRender forces NoCulling + a no-op `grcBegin(3,3)` before the event). Fix:
`cfxDetachOwned` now takes `standIns[]`; `cfxMakeNode` builds a node of ours that is spliced
into the parked node's exact list position (order copied), and comes off when the parked node
goes back. The stand-in (`standInForWatermark`, clean_shot.cpp) calls slot 3 of the exported
`?TheFonts@@3PEAVFontRenderer@@EA` object (checked to point into font-renderer.dll), then does
Cfx's own no-op idiom through rage-graphics-five exports: `PushDrawBlitImShader`, `grcBegin(3,3)`,
three zero vertices, `grcEnd`, `PopDrawBlitImShader` (RenderHooks.cpp does the same before
firing the event "to flush other grcore states"). Mod-loader's node is still just parked; its
handler only queues text. If a DUI report comes back on 0.8.21, the next step is a dev build that
toggles the no-op draw and the flush separately, then disassembly of the game's im topology cache.

**Why not the head gate (return false).** That was the first design and it is WRONG for this
feature: the dev dump showed 13 nodes on the event and `glue.dll` (NUI, order -1000, plus two
more at 0) is one of them. Returning false from the head takes the server's chat and every NUI
HUD out of the shot too, which is not what was asked. The old CLAUDE.md line "NUI and the game
HUD do not render on this event" was wrong for NUI; the game's native HUD (minimap etc) is fine.
Full list from the dump, in walk order: ours(0), loading-screens(INT_MIN), rage-graphics
(INT_MIN+5), glue(-1000), mod-loader(-1000), extra-natives(-500), scripthookv(0), glue(0),
conhost-v2(0), glue(0), debug-net(50), font-renderer(1000), loading-screens(5000). Cfx's
`ConnectInternal` inserts after every node with order <= the new one (verified in EventCore.h),
so a node connected AFTER Setup() with a negative order would land in front of ours; only
matters for the head-gate design, which is gone.

**Why polling and NOT a low-level keyboard hook (a whole evening, do not redo).** The first
build used `WH_KEYBOARD_LL` from its own thread, blocking in the hook until a clean frame had
been decided, on the theory that a screenshot is taken from the frame already on screen. In the
field: the hook installed, saw every letter, alt-tab, Esc, and NEVER saw F9, and saw only the
release half of PrintScreen. An outside probe (python LL hook, scratchpad `llprobe.py`) saw both
keys perfectly with the game closed, and behaved exactly like ours with the game window in
front. Not ShareX (its hotkeys are `RegisterHotKey`, which the probe proved does not hide keys
from LL hooks; its `KeyboardHook.cs` class is unused), not hook age (re-installing ours once a
second changed nothing), not a foreground-pid mistake (18620 IS `FiveM_b3751_GTAProcess`, the
game window; the one rejected press had ShareX's region overlay in front). Whatever swallows
those keys while the game is focused was never identified and does not matter, because
`GetAsyncKeyState` polled from `framePumpTick` (the refresh_key mechanism) sees them every time.
Bonus: the keyboard hook was the one NEW capa signature (`log keystrokes via application hook`);
polling adds none, `log keystrokes via polling` was already there for refresh_key, and the
release build's capa capability list is byte-identical to 0.8.15's.

Residual limit, documented in the file: polling is a frame race. ShareX and friends grab pixels
tens of ms after the key on their own thread, and the overlays are gone from the next frame.
A capture that happens inside the keystroke itself (Windows' own PrintScreen-to-clipboard, done
in win32k) has no frame to hide in and will still show them. Acceptable; nobody asked for that.

Shipped as 0.8.16 (commit 573ca92): CHANGELOG section, README section + table row, version bump.
The notes tell existing installs to add `hide_overlay = ...` by hand, since `_settings.txt` is
only ever created. `refresh_key` was already in the README.
`tools/test_settings.cpp` is at 12 groups. The build wrapper that works from the Bash tool on
this machine is in the memory note `build-from-bash-tool`.

## Live reload (v0.5.0)

**PROVEN IN THE FIELD 2026-08-29, and the old "never really worked" is resolved.** On a live
GTAWorld session with the dev build, the user overwrote a shirt mid-session and the watcher caught
both halves with no keypress: `LIVE-UPDATE: mp_m_freemode_01/jbib_001_u.ydd reread from disk` and
the same for `jbib_diff_001_a_uni.ytd`. The fix was the pump: `OnMainGameFrame` instead of the
PeekMessageW IAT shim, measured at 1,300 to 1,400 ticks per 10s (about 140 fps). The old shim
assumed the game's main loop pumps messages AND that we were the first caller of that import,
which is the most likely reason the user's 2026-08-25 statement was true at the time. What still
needs a restart is taking over a name the server already streams; that is by design, below.

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
the pump drains them on the game thread only. Never move register/re-stat back onto the
watcher thread.

**2026-08-29: the pump is now FiveM's `OnMainGameFrame`**, with the PeekMessageW shim kept as the
fallback when that export is missing. See the exports section: the event IS the main thread, so
the old "first caller of PeekMessageW" guess is gone, and with it the case where the main loop
stops pumping messages and live reload silently does nothing. That guess is the most likely
explanation for "live reload never really worked", but it is a hypothesis until a session proves
it. The watcher's log line names which pump it got.

**Changing a loaded file NEVER shows without a restart. SETTLED 2026-09-08, do not re-propose.**
User, verbatim: "Live swap will never work, its false. U change a file, u need restart. Thats it."
I proposed dropping the resident object after the live re-stat (`forceReloadSlot` from 0.8.24
called from `drainOps`, which is already the game thread), shipped it as a local 0.8.25, and it
was rejected and reverted the same hour. The mechanism is not the problem: `RemoveObject` does
free the object. The problem is that nothing then RE-REQUESTS it. A component already on a ped is
held by the ped, and the game does not stream it again until the ped is rebuilt, which no
client-side call can force (`SET_PED_COMPONENT_VARIATION` and friends are networked, rejected in
the natives section). So the drop buys nothing on the case people actually hit. This is the same
ground the refresh key was removed over one commit earlier; treat both as closed. The 0.8.23
log line and README paragraph telling people to restart are CORRECT and must stay.

**Refresh key: REMOVED in 0.8.23 (2026-09-07, user call: "doesn't work at all, refreshing it dead").** The watcher already rescans on every folder event, so the key only ever found "nothing has changed", and no key can make the game redraw a file it already holds. vkFromName stays for hide_overlay. Removing an option means removing it from EXISTING installs too, since _settings.txt is never rewritten wholesale: kDeadKeys + stripDeadKeys in settings.cpp drop the line and the note above it during migrateSettings, which now runs its rewrite for a dead key as well as for a marker file. Test group 13 covers it. Historical notes follow.

**Refresh key (2026-08-29, local only).** `refresh_key` in `_settings.txt`, default `f11`: the SA
modloader habit, press it and the folder is read again immediately. `GetAsyncKeyState` plus a
check that the foreground window belongs to this process, rather than `InputHook::IsKeyDown`,
because it needs no export a FiveM update could rename and the foreground check is the only thing
IsKeyDown would have bought. The key sets an event; the watcher waits on that and the folder
notification together. It ALWAYS logs, including "nothing has changed since the last look": a key
that reports nothing is otherwise indistinguishable from a key that does not work. (Historical: rage-allocator-five's ThreadAttachment.cpp stamps allocator TLS into
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
  `src/streaming/rsc.cpp` is where the refusal goes. Unreadable files are still refused, since there is
  nothing to load. The startup crash saver is what replaces it: whatever the bad
  file is, oversized or not, it costs one launch instead of every launch.
- **Cost audit (0.5.2)**: every .ytd/.ydd on disk is an RSC7 resource; header dwords 2/3
  (system/graphics flags) encode the exact resident memory via CodeWalker's `GetSizeFromFlags`
  (`rscSizeFromFlags` in `src/streaming/rsc.cpp` — pages sum x (0x200 << shift)). scanDir totals it, logs
  `pack cost when fully loaded` + `HEAVY` lines for files >= 8 MB (catches 4K anything and 2K
  uncompressed; vanilla clothing txds are under 2 MB). This is an on-disk estimate and undercounts:
  a drawable is charged nothing for the texture dictionaries it shares. Since 2026-08-29 the first
  LOADED line per override carries the REAL figure from `devtools-five!CountDependencyMemory`.
- **Pool usage (2026-08-29)**: `grcResourceCache::GetUsedPhysicalMemory` says how much of the
  ceiling the game has actually spent, which is the number every "my textures went missing" report
  needed and never had. One-shot WARN at 90% full. `GetTotalPhysicalMemory` is NOT called (it calls
  into engine code that updates state); the ceiling comes from the budget table already read.
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

**A quarantined file is a suspect, not a culprit, and on the live path barely even that.** The
startup loop journals ONE key at a time, so a startup quarantine really does name the file that
was in the call. A live batch journals all of them, so a crash tars every file in the batch and
the guilty one may not be in the list at all. Read the count first: more than one name means the
live path, which means treat all of them as bystanders until proved otherwise.

Worked example, the user's own install, 2026-08-29. `_quarantine.txt` held
`w_pi_sns_pistol.ytd` and `mp_f_freemode_01/head_000_r_1.ydd`. Both valid RSC7, 7.00 MB and
1.50 MB of texture, nowhere near the 32 MB warning, and every sibling in the same pistol pack
(`+hi.ytd`, `.ydr`, `_hi.ydr`, both mags) registered cleanly in the same session. Two names, so:
live batch, probably innocent. Cleared them; if the next session survives that is the proof.

**Only two log generations are kept** (`texoverride.log` + `.old`), so a quarantine more than two
launches old can never be explained from the log. That crash was from 2026-08-27 and both logs
were from 2026-08-29. If a quarantine matters, copy the log BEFORE relaunching.

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

## FiveM's own DLLs export a usable API (2026-08-29, big one)

The Cfx components in `FiveM.app` are already loaded in our process and they EXPORT most of what
this plugin has been pattern-scanning for. Resolve by name with `GetModuleHandle` +
`GetProcAddress` (`src/core/cfx.cpp`), NEVER as a static import: a static import on a FiveM DLL
turns a renamed export into "Couldn't load texoverride.asi" with no log at all, which is issue
#16's dialog. A missing export must only switch off the feature that wanted it.

A full export dump of all 80 DLLs is reproducible with the minimal PE parser kept in the
scratchpad (`pe.py`), not the repo. Highlights that matter here:

- `rage-nutsnbolts-five`: `OnMainGameFrame`, `OnBeginGameFrame`, `OnEndGameFrame`,
  `OnFirstLoadCompleted`, `g_mainThreadId`.
- `rage-graphics-five`: `OnPostFrontendRender`, `GetGameResolution`, the whole im-mode draw path,
  `grcResourceCache::GetInstance/GetUsedPhysicalMemory/GetTotalPhysicalMemory`.
- `gta-streaming-five`: `RegisterRawStreamingFile`, `Manager::GetInstance/RegisterObject/
  ReleaseObject/RequestObject`, `GetStreamingModule`, `GetStreamingIndexForName`,
  `GetPgRawStreamerEntries`, `CfxCollection_AddStreamingFileByTag/RemoveStreamingTag/
  BackoutStreamingTag`, `AddDataFileToLoadList`, `RemoveDataFileFromLoadList`,
  `ForceMountDataFile`, `AddMetaToLoadList`, `sf::AddMinimapOverlay`.
- `rage-device-five`: `fiDeviceRelative` ctor + `SetPath` + `Mount`, `fiPackfile::OpenPackfile`,
  `fiDevice::GetDevice/MountGlobal/Unmount`. The note under the failed ASI route saying the
  fiDeviceRelative vtable and object size "are not published" is WRONG: they are exported.
- `gta-core-five`: `GetStructureDefinition` (hash and string overloads), `AddCustomText`,
  `ActivateStatusText`, `GetPools`/`GetPoolBase`, `OnKillNetwork`, `OnKillNetworkDone`,
  `g_gameInit`, `TryDisconnect`, the whole `OnInitFunction*` family.
- `devtools-five`: `CountDependencyMemory(streaming::Manager*, index)`.
- `font-renderer`: `TheFonts`. `rage-input-five`: `InputHook::IsKeyDown/IsMouseButtonDown`.
- `citizen-resources-client`: `CachedResourceMounter::MountOverlay(resource, path, error*)`.
- `rage-scripting-five`: `scrEngine::GetNativeHandler(hash)`. See the rejection below.

Answering the question that started this: `%d mod packs loaded` is drawn by
`citizen-mod-loader-five.dll` (draw function at RVA `0x1b4f0`, wide string at `0x30880`) through
`font-renderer!TheFonts` on `OnPostFrontendRender`, right-aligned with `GetGameResolution`. The
FiveM version watermark in the other corner is `font-renderer.dll` itself: it reads
`citizen/release.txt` and `CitizenFX.ini`'s `UpdateChannel`, then assembles `FiveM` + `" (b%d)"` +
`" [e%d]"` + `" (SDK)"`/`" (Canary)"`/`" (Beta)"` via `"Ver. %d%s"`. It also reads a
`ui_customBrandingEmoji` convar.

### fwEvent: how to subscribe from outside Cfx's build

`fwEvent<>` is a header-only template, so `Connect` is inlined into every component and exported by
none; the exported symbol is the event OBJECT. Layout read out of `citizen-mod-loader-five.dll`'s
own inlined Connect (the call behind its `OnPostFrontendRender` subscription, `0x18000e190` on the
b3788 client), NOT guessed:

```
fwEvent  { callback* head; size_t nextCookie; }        cookie bumped with lock xadd
callback { std::function<bool()> fn;   +0x00, 0x40 bytes
           callback* next;             +0x40
           int order;                  +0x48
           size_t cookie; }            +0x50, node is 0x58 total
```

Nodes are inserted before the first node with a strictly greater `order`. A handler that returns
false stops every subscriber after it, so ours always return true.

Two things make this safe from our own build, and both are load-bearing:

- the std::function's copy and delete run through ITS OWN vtable, which is our code and our
  allocator, so the functor never crosses a CRT boundary. `static_assert`s on
  `sizeof(std::function<bool()>) == 0x40` and `sizeof(node) == 0x58` break the BUILD if a future
  MSVC changes the layout, rather than corrupting FiveM's list at runtime;
- the node itself is only ever freed by Cfx (in `~fwEvent`, at process exit, since we never
  disconnect), so it is allocated with `HeapAlloc(GetProcessHeap())`, which is what the UCRT's
  `free()` has used for both the static and dynamic CRT since VS2015.

We connect long after the game is running, so the main thread is walking the list while we splice
into it. That is safe here and only here: the node's `next` is published before the pointer TO the
node, both stores are aligned, x86-64 does not reorder stores, and nothing is ever removed. It
needs a real lock the day anything disconnects or two threads connect at once.

### What now uses it (LOCAL ONLY, 2026-08-29, NOT released, no version bump)

- **Live reload runs on `OnMainGameFrame`** instead of the PeekMessageW IAT shim, which stays as
  the fallback when the export is missing. Same thread by definition, once per frame, no patch
  into the game at all. The watcher's log line says which of the two it got.
- **Texture pool usage** (`grcResourceCache::GetUsedPhysicalMemory`, `poolBeat` in
  `src/streaming/budget.cpp`). A plain field read (`mov rax,[rcx+0x20168]; ret`).
  `GetTotalPhysicalMemory` is deliberately NOT used: it calls the game's
  `_getAndUpdateAvailableMemory`, a call INTO engine code that updates state, and the ceiling is
  already known from the budget table. First reading at INFO, once a minute at DEBUG, one-shot
  WARN at 90% full.
- **Real per-slot memory** (`devtools-five!CountDependencyMemory`, `slotMemoryCost` in
  `src/streaming/streaming.cpp`). The scan's cost audit reads RSC7 page flags off disk, so a
  drawable is charged nothing for the texture dictionaries it shares. This walks the dependency
  tree in the live streamer instead. Reported on the first LOADED per override, plus a capped
  `HEAVY IN MEMORY` line at 8 MB.
- **`OnFirstLoadCompleted`** as a second trigger for starting the watcher. The old condition was
  `g_idsReady`, which is only set inside the hook, so a server that streams nothing at all left
  the watcher (and every claim) permanently unstarted. It also warns when the first load finishes
  with nothing claimed.

### Found and REJECTED: calling natives (`scrEngine::GetNativeHandler`)

Exported, takes a native hash, hands back the handler. It would let the plugin refresh the ped
after a live reload instead of telling the player to re-equip. **Do not use it.**
`SET_PED_COMPONENT_VARIATION` and everything else worth calling is NETWORKED: the server and other
players would see it, which breaks the two promises the project rests on and makes the ban-risk
section dishonest. `CheckNativeScriptAllowed` and `GetBlockedNatives` sit beside it, so Cfx gates
this somewhere; that gate was not traced, because the answer is no either way.

### Open leads, in order

1. **`OnKillNetworkDone` to retry lost claims.** A file whose name the server already streams
   cannot be claimed mid-session, and today that means restarting FiveM. On disconnect the
   server's streaming files are torn down (`CfxCollection_RemoveStreamingTag` /
   `BackoutStreamingTag` are the calls that do it), so the names go free. Retrying the failed
   claims there turns "restart FiveM" into "F8, `disconnect`, reconnect". `disconnect` is a real
   client callback (string in `glue.dll`, beside `resizeWindow`/`ui_premium`) and
   `TryDisconnect@FiveGameInit` is exported. UNTESTED, and worthless if the user cannot reach a
   disconnect on their server.
2. **`ForceMountDataFile(pair<type, path>)`** for the audio/`.awc` and `.meta` cases. This does NOT
   reopen the `registerRawStreamingFile` question in the weapon-sounds section; it is the OTHER
   route, the mounter one those notes already point at. `AddDataFileToLoadList` is the wrong call:
   it appends to a deferred vector only processed at session load, and special-cases
   `GTXD_PARENTING_DATA`. `ForceMountDataFile` builds a one-entry vector and calls Cfx's own
   `HandleDataFile` immediately, which logs `%s of %s in data file mounter %s`. That wrapper is
   exactly the "FiveM does something more when mounting a loose folder" the 2026-08-21 attempt was
   missing. Both take `const std::string&`, and the disassembly shows the classic MSVC layout
   (`cmp [rcx+0x18], 0xf`, size at +0x10), so ours pass straight in. Must run on the main thread.
   Three things only a live session can answer: whether the game has an `AUDIO_WAVEPACK` mounter
   registered at the moment we would call, whether it works outside a resource context, and
   whether bank naming resolves from a loose folder.
3. **`CachedResourceMounter::MountOverlay(resource, path, error*)`.** Mounts an `.rpf` over a
   cached resource (it formats `%s.rpf`). If that works against a live server's resource it is the
   only known route to override a file the SERVER streams, which is the cause of every
   "restart to claim" line in the log. It is the FxDK live-edit path, so it may only work before
   the resource mounts. One experiment, not more disassembly.

### `GetStructureDefinition` was NOT used, and why

`gta-core-five` exports both overloads; the string one computes a case-SENSITIVE joaat and
tail-jumps into the game's `parManager` lookup, found by pattern. Using it needs the layout of
`parStructure` itself to read member offsets, and that is in no FiveM binary (gta-core-five only
forwards). Not guessed, so not used. If it is ever wanted, the safe shape is: propose the offsets
from parStructure, then VERIFY them with the existing fingerprint before writing anything, so a
wrong layout guess falls back instead of corrupting presets.

## 0.8.14 crashed on startup, 0.8.15 is the fix (2026-08-30)

Reported by the user and by chocomintw: with 0.8.14 the game dies during loading and has to be
launched one, two or three times before it will start. Note the rate - it is MOST launches, not
the occasional one, which is what a splice that lands at the same point in every load looks like.
0.8.13 is clean. chocomintw's dump (`CfxCrashDump_2026_08_29_22_20_15`):
`bravo-kilo-oklahoma`, `GTA5_b3751.exe+137FE74`, 27.6s in, on a late-created worker thread, and
the faulting instruction disassembles out of the dump as a pool free-list pop with `this == 0`
(`mov rax,[rcx+0x20]` where the object is +0x00 storage, +0x08 capacity, +0x10 free count,
+0x18 element size, +0x20 list head, +0x28 owns-memory). No texoverride frame anywhere on that
thread; our beat thread was parked in ntdll. Same hash as the unexplained dev-build crash in the
notes above, so that one was this.

Deduction, not proof: the plugin loads at ~24.1s (FiveM's log ends `pre-gameload component
adhesive`), and by 27.6s the only 0.8.14 code that has run is `connectFirstLoad()` at the top of
`BeatLoop`. That is an unsynchronised splice into a Cfx `fwEvent` list from a worker thread, at
the exact moment Cfx components are subscribing to the same events from the main thread. Cfx's
own `Connect` walks and stores with no lock, so two ordered-walk inserts can drop a node, and the
dropped node need not be ours. A FiveM handler that quietly goes missing during load is a crash
later, inside the game, with nothing of ours on the stack, on launch after launch until one gets
through.

0.8.15: both connects move into `Setup()`, on the loader thread, in the window that already makes
the MinHook patch safe, and `cfxConnect` prepends with one `InterlockedCompareExchangePointer`
instead of walking by `order`. A CAS can only ever lose OUR node, never theirs. `poolBeat()` moves
from the beat loop to `framePumpTick()`, because `grcResourceCache::GetInstance` is a jump into
GTA5.exe and calls into game code belong on the game thread. Verified NOT the cause and left
alone: `CountDependencyMemory` (disassembled, devtools-five+0x332A0, pure reads plus a virtual
`GetDependencies` into a stack buffer, no allocation) and the placement layout sharing.
First 0.8.15 launch on the user's machine (2026-08-30 01:46): clean, 755 held / 0 contested,
0 faults, watcher `on FiveM's own frame event`, `Texture pool: 1.1 GB of 8.8 GB in use`. Given
0.8.14's failure rate that is real evidence, though chocomintw's machine is the one that matters.
If 0.8.15 still crashes, the next suspects in order are `poolBeat`'s call into the game, then
`slotMemoryCost`, then the placement solver.

**Never re-upload a release's notes by hand after CI publishes them.** CI appends the `---`
footer with `<sha256> *texoverride.asi`, and the updater refused to install anything without it.
That is what happened to v0.8.14 (its body is byte-identical to the CHANGELOG section at
`b144acd`, footer and all), so every 0.8.13 install was stuck. 0.8.15's updater reads
`"digest":"sha256:..."` off the GitHub asset instead, bounded by that asset's own
`browser_download_url`, and only falls back to the notes line.

## 0.8.26 (2026-09-09): force_reload crashed the game, and is GONE. Do not bring it back.

Two players on the same busy server died with `ERR_GEN_PAGE_1` at `GTA5_b3751.exe+13EC17E` the
day after 0.8.25 auto-installed. Mechanism, from the diff plus Cfx's raw LoadStreamingFile.cpp:
0.8.25 landed claims on REAL slots for the first time, which made the 0.8.24 `force_reload` drop
path fire for the first time ever (zero `FORCED-RELOAD` in any log before). That path cleared the
dependent count and called `RemoveObject` on clothing and `.ycd` dictionaries a ped was still
using. Cfx runs that exact sequence ONLY at resource teardown (`~3455`) and on shutdown
(`FlushCustomAssets`, comment: "if this breaks next reload, so be it"). The user's own session
was clean because their pack has no clothing, so nothing contested a slot and no drop queued. The
whole drop path is deleted (forceReloadSlot, DropReq, g_dropQ, drainDrops, the setting). The
real-slot takeover from 0.8.25 STAYS: Cfx's override path (~1925) is the same bare handle
overwrite with no status check and no unload. If a 0.8.26 log still shows this crash, the takeover
itself is the next suspect and the fix would be reverting `storeAnswersNames`.

VERIFIED from the decrypted bytes (2026-09-09). The on-disk exe is Arxan-encrypted at that RVA
(entropy 7.95 bits/byte, static disassembly is garbage), so I read it out of the RUNNING process
instead: a `TEXOVERRIDE_DEV`-only `devDumpCodeWindow` in live_reload.cpp does a partial-safe
`ReadProcessMemory` of a window around `GetModuleHandle(nullptr)+0x13EC183` off the frame pump
once streaming is live, writes `_codedump.bin` + a header with the load base, and `scratch/disasm.py`
(capstone) disassembles it. b3751 is `GTA5.exe_e5fe7bd...`, base 0x140000000. The dev capture
route is the ONLY way to disassemble any Arxan-protected address; keep those two throwaway pieces
in mind for the next one (both were reverted after use, the tree is clean at the 0.8.26 commit).

What the bytes show at the fault (return address `GTA5+0x13EC183`, so the fault is in the call at
`0x13EC17D`):
```
13EC148  mov  rax,[r14]                 ; r14 = rdi+0xb0 + i*8, walking an array of [rdi+0xa8] entries
13EC154  movzx ecx,byte[rax]            ; type tag at +0
13EC157  mov  r15d,[rax+0x34]           ; the entry's id
13EC15B  cmp  ecx,6 / jne              ; type 6 = the streaming-object branch
13EC16D  mov  rcx,[r8 + (rbx*0x2a+r15)*8 + 0x2d7e1f0]   ; r8 = image base; global pointer table
13EC175  test rcx,rcx / je             ; guards NULL only
13EC17A  mov  rax,[rcx]                 ; rcx's vtable  <- rcx is a FREED-but-non-null pointer
13EC17D  call [rax+0xe0]                ; virtual dispatch -> AV, return addr 0x13EC183
```
So it is a per-frame loop that virtual-calls (vtable slot 0xe0) every live entry through a global
table at `imagebase+0x2d7e1f0`, indexed by `id*0x2a + subid`. The loop null-checks the table slot
but NOT a freed-non-null pointer, which is exactly what `RemoveObject` leaves behind. `force_reload`
called `ReleaseObject`/`RemoveObject` WITHOUT the `FreeArchetypes` + `RemoveSlot` that Cfx always
pairs it with (LoadStreamingFile.cpp ~3455, we deliberately skipped RemoveSlot), so the object was
freed while this table kept its pointer, and the next frame dispatched through the dangling entry.
This is why "not freeing" (0.8.26) is the fix and why the handle-overwrite takeover is safe: the
takeover frees nothing, so it can never dangle this table. Confirmed alongside it that the 0.8.26
dev build ran a live GTAWorld session: 194 held, 0 contested, no RECLAIM and no crash.

Also settled by the user the same day, both in code: **the log is always DEBUG** (`debug` key
dead, `g_minLogLevel` starts at Debug, the 500 cap on overridable server files is gone), and
**update messages always appear** (`no_update_check` dead; an auto-install now shows a box
saying what it installed, because "I never turned updates on" reports came from silent installs;
`auto_update = no` still prompts Yes/No/Cancel). `kDeadKeys` strips all three from existing
`_settings.txt` files; markers `_debug`, `_verbose`, `_no_update_check` are deleted without
writing a line (`isDead` in settings.cpp). Test group 14 covers it.

Release mishap worth remembering: `git add` with an ignored path (`scratch/`) fails the whole
`&&` chain, and a `git tag` on the NEXT line then tags the old HEAD and pushes it. CI started a
v0.8.26 build from 0.8.25 source; caught within two minutes, tag deleted remotely, run cancelled,
no release or announcement had fired. Never put `git tag`/`git push` on a separate line from the
commit they depend on.

**`git push --follow-tags` does NOT push these tags (2026-09-12, v0.8.27).** It only pushes
ANNOTATED tags, and `git tag vX.Y.Z` makes a lightweight one. The push reported `main -> main`,
looked completely normal, and the tag stayed on this machine, so no tag build and no release.
Caught with `git ls-remote --tags origin | grep vX.Y.Z`, which is the check to run every time.
Push it explicitly: `git push origin main && git push origin vX.Y.Z`.

## What a claim ACTUALLY does (0.8.24, 2026-09-08). Four defects, all shipped fixed.

Read out of two real debug logs (335 and 682 files) plus disassembly. None of it was guessable
from the source, and three of the four made the log actively lie, which is why "335 held, 0
contested, 0 rejected" had been believed for months.

- **EVERY claim mints a NEW streaming index. Not one takes over a vanilla slot.** Proof: with
  `debug = yes` all claims are logged, and the ids are strictly `+1` in REGISTRATION order,
  335/335 and 682/682, zero gaps, per extension. `mp_eye_colour.ytd` (a loose overlay txd) landed
  in the same contiguous run as `mp_f_freemode_01/head_diff_008_a_pak.ytd` (a collection txd from
  x64v.rpf); those live in different vanilla archives and cannot have adjacent real indices in our
  own filesystem walk order. After the mint, the NAME resolves to the minted index, which is why
  the plugin works at all: everything that looks the name up afterwards gets our file.
- **`slotMemoryCost` measured the wrong half.** `devtools-five!CountDependencyMemory`
  (+0x332A0) seeds its accumulator from `gta-streaming-five!StreamingDataEntry::ComputeVirtualSize`
  (resolved through the IAT, not guessed) and then recurses over `GetDependencies` adding more of
  the same. VIRTUAL only. Texture pixels are PHYSICAL, so every `.ytd` reported ~0 and
  `HEAVY IN MEMORY` could never fire for a texture, the one type every texture-loss report is
  about. Verified against decoded RSC7 flags: `mp_fm_skin_f_up_whi.ytd` audits 16.0 MB physical /
  0.0 virtual and reported exactly 0.0. `?ComputePhysicalSize@StreamingDataEntry@@QEAA_KI@Z` is
  exported beside it, same this-is-the-entry shape; both are read now. The dependency recursion is
  still virtual-only (`GetDependencies` sits behind a build-dependent vtable offset we have no
  pattern for), so a drawable is charged for its own vertex buffers but not for pixels of txds it
  shares. Noted with a `ponytail:` comment in `slotMemoryCost`.
- **The alias / "pin both" check could never fire.** It called `targetStreamingId` AFTER
  `o_regRaw`, and our own registration re-points the name at the minted index, so the answer was
  always the id we had just been handed. `aliased = 0` on every file of every log ever seen. The
  lookup now runs BEFORE the claim. This was wrong since `2c6342a` (2026-08-22), not a recent
  regression.
- **`MOVED` could never fire on an index nobody else wrote.** The 0.8.20 name check sat behind
  `if (e.handle == ov.handle) continue;`, so a slot the store had re-pointed elsewhere looked
  identical to a healthy one forever. Now a round-robin slice (32 per beat, `inSweepSlice` in
  `utils.h`, `tools/test_reval.cpp` asserts nothing is ever skipped) revalidates everything.
  **It fired 44 times on the first real 0.8.24 session**, all `.ycd`, where the server registered
  the same animation dictionary names and moved them off our minted indices. Those 44 had been
  silently orphaned every session before this.
- Also: `LOADED ... from your file` sampled the handle at first OBSERVATION, so a reclaim landing
  between the load and the beat that noticed it was reported as a win. It now samples while the
  load is in flight and says so when it cannot tell.

**`force_reload` (0.8.24, default ON).** Drops a resident object the game loaded from its own file
so the next request reads through our handle: Cfx's own sequence from LoadStreamingFile.cpp ~3455,
clear the dependent count (bits 2..15, or RemoveObject refuses), `ReleaseObject(id, 0xF1)`
(ClearRequiredFlag), `ReleaseObject(id)` (RemoveObject). `RemoveSlot` is deliberately NOT called,
since freeing the index is the corruption the 0.8.20 check exists to catch. Game thread only
(drained in `framePumpTick`), copied out from under `g_cs` first because the drop enters GTA5.exe
and our hook takes that lock, journalled so a fault quarantines one file, once per slot, and no
SEH around the calls per the standing rule. **Untested where it matters: it has fired ZERO times
in every log so far, because `0 from the GAME file` in all of them. It may well be dead code for
the common case.** Do not claim it fixed anything until a `FORCED-RELOAD` line appears in a real
log. It does NOT cover the live-swap case, deliberately: see the settled note in Live reload.

## 2026-09-08: the claim was minting phantom indices, and what a claim still cannot reach

Two separate things, found in one session off a user log. The first is a real bug that affected
EVERY file in EVERY session ever logged. The second is a hard limit that no amount of slot work fixes.

**The claim ran before the store could answer, so pin-both was dead code.** The base claim happens
inside the hook on FiveM the FIRST `registerRawStreamingFile` call. At that instant the game store
answers nothing: the log shows `Slot lookup for .ytd/.ydd/.ydr/.yft: the module does not know that
name` at 21:52:27 and the identical lookups succeeding at 21:52:30. So `before = targetStreamingId()`
was always invalid, `ov.altId` was never set, and the "pinning both" path 0.8.24 added has NEVER
fired in any log. Consequence: `o_regRaw` minted a fresh index for names the game already owned,
our handle landed in a slot nothing looks up, and the log read perfectly while nothing changed in
game. The tell is in the ids: 758 files, ids strictly +1 in registration order, zero gaps.
FIXED by `storeAnswersNames()` in core.cpp, which gates the claim on two base-game names
(`mp_f_freemode_01/head_diff_000_a_whi.ytd`, `mp_f_freemode_01/head_000_r.ydd`) resolving, with a
20s deadline so an unpinned claim still beats no claim. Proof it works, same machine same pack:
`758 direct, 0 occupied-slot takeover` became `10 direct, 220 occupied-slot takeover`, and the ids
went from one contiguous run to 7 scattered clusters spanning 492,607 indices, grouped the way
vanilla registered them per rpf. Cost: the claim lands ~30-60s later than it used to. Watch
`contested` on a heavy server.

**Body skin and face overlays cannot be delivered from tex_overrides. SETTLED by the user in game,
do not reopen.** Even with the claim on the real slot, our handle in the entry, and the log saying
`LOADED ... from your file`, the ped draws stock. The user tested it directly and split their pack:

- NEEDS a FiveM mods folder package: `mp_fm_skin_*` (both genders, all 6 tones, fe/lo/up),
  `mp_fm_faov_*` (beard, eyebrowf, eyebrowm, flan, lips, lipsm, lips_g, mole), `ng_hip_faov_*`,
  `mp_eye_colour.ytd`, and the caret forms `mp_f_freemode_01^uppr_*.ydd`.
- WORKS from tex_overrides: `head_*_r.ydd` and `head_diff_*.ytd` inside a collection folder.

So the line is not root-vs-collection and not model-vs-texture: it is that the ped is BUILT from the
skin and face overlay txds before anything a streaming slot claim reaches. README now has a
"What this plugin cannot do" section and `scanFinish` logs a WARN naming the count and the four
prefixes, because the log otherwise reads perfectly while nothing changes.

**Verified along the way, keep:**
- Vanilla ships `mp_fm_skin_f_up_whi.ytd` at least 6 times, and the two inside x64v.rpf are
  DIFFERENT images: `ped_mp_overlay_txds.rpf` (116191 bytes, Rockstar blue nipple markers) vs
  `strm_peds_mp_overlay_txds.rpf` (108098 bytes, flat wash). Also pixel-identical to
  `mp_f_freemode_01/uppr_diff_000_a_whi.ytd`.
- Despite that, there is only ONE slot for the name. `containerAliasId` in streaming.cpp probes
  `ped_mp_overlay_txds/<stem>`, `strm_peds_mp_overlay_txds/<stem>`, `streamedpeds_mp`,
  `streamedpeds_players` (both separators) and NEVER fired. Kept because it is cheap and it is the
  standing answer to "maybe there is a second slot", but it has proved there is not.

**Audited 2026-09-12, and "cheap" is an understatement: on the user's install it cannot run at
all.** `containerAliasId` is called only from `recoverOccupiedSlot`, i.e. only when `o_regRaw`
REFUSED the claim, and its first line bails on any key containing `/`. The last session was
336 claims, 336 of them collection keys, 0 root keys, and 0 root files on disk. So it returned
on line one every time and never reached a store lookup. Do not describe it as a per-claim cost.

**`altId` is NOT dead, and it has two setters, not one.** The primary is `core.cpp:135`
(`ov.altId = before`, the 0.8.25 pin-both path); `streaming.cpp:298` (containerAliasId) is the
fallback. Deleting containerAliasId would leave the mechanism intact. Both were unfired in the
2026-09-12 logs (`0` lines saying `pinning both`, summary `aliased = 0`), and that is CORRECT,
not a bug: the 336 claims split 152 direct + 184 takeover, where the 152 are names vanilla does
not ship (packs add component numbers past the vanilla range: `teef_008_u.ydd`,
`..._female_apt01/lowr_002_r.ydd`) so no slot exists and minting one is right, and the 184 are
names vanilla DOES ship so the register call refuses and the takeover path writes the handle in.
The pin-both path is for a third case, a claim that SUCCEEDS on a name the store already resolves
elsewhere, which is the 0.8.24 phantom-index bug and which `storeAnswersNames` appears to have
removed. The gate provably ran (`Slot lookup for .ydd: the module keys on "..." (folder/name)`,
zero `never answered a name lookup in 20s`), so "unknown name" was a real answer, not a timing
artifact. If it is ever deleted, the honest framing is "39 lines of unfired hedge", not "dead
code costing lookups", and the input that would actually exercise it (a pack with root `.ytd`
overlays landing on an occupied slot) has never been tested either way.

**Dead ends from this session, do not redo:** replacing `mp_f_freemode_01/uppr_diff_000_a_whi.ytd`
instead (the working SP packs touch `uppr_diff_000` zero times); anything involving GTA own
`mods/` folder, which FiveM does not read; diffing the user texture against vanilla, which only
ever confirms what the user already knows.

**`scratch/pedscan` gained four modes** for this: `dump` (extract by path tail, print texture
name/size/format/mips, export .dds), `tex` (same for loose ytd files), `retex` (copy a ytd renaming
the texture inside it), `fromdds` (build a ytd from a .dds, for flat-colour probe textures).
README in that folder is updated.

## Skills and plugins: what to reach for here, without being asked

Installed on this machine (`~/.claude/plugins/cache/`, plus `~/.claude/skills/`). Use these
automatically on this repo when the trigger below matches; do not wait to be told.

- **`humanizer`** (personal skill). MANDATORY on every README, CHANGELOG, COLLECTIONS.md,
  docs/*.md, forum post and release note before it is committed. This is already a rule in
  Conventions below; it is repeated here so the skill gets INVOKED rather than approximated from
  memory. Check the result with a grep for em/en dashes, which must be zero.
- **`ponytail:ponytail`** is ON by default via a SessionStart hook (level: full), so it is
  already governing what gets built. Worth invoking by hand only to re-assert it after a long
  session. `ponytail:ponytail-audit` and `ponytail:ponytail-debt` also exist and have never been
  used here.
- **`i-have-adhd:i-have-adhd`** shapes OUTPUT, not code: next action first, numbered steps,
  restate state every turn, no preamble or closer, concrete time estimates. The user invoked it
  2026-09-12 and it persists for the session. It has an explicit carve-out for "explain" and
  "walk me through" requests, which run at full length with headers. Ponytail and this one
  compose: ponytail decides what to build, this decides how the answer reads.
- **`slopsec`** is security audit for vibe-coded SaaS (Next.js, Supabase, Firebase, Express).
  It does NOT apply to this repo and never will: there is no server, no auth, no secrets and no
  network surface here. The security story for texoverride is the AV false positive section, not
  an app audit.
- **`agricidaniel-claude-seo` and `cloudflare`** belong to blancodagoat.dev, not to this repo.

**jeffallan/claude-skills (67 skills) was evaluated 2026-09-12 and REJECTED. Do not install it
here.** It is a full-stack web collection. Going through the list against a Windows C++17 ASI:
only C++ Pro is even partly relevant, and its MUST-DO list (C++20 concepts, RAII universally,
smart pointers, CMake, AddressSanitizer) fights this codebase on purpose. The plugin manipulates
memory inside GTA5.exe that it does not own, so there is nothing for a smart pointer to own; the
build is a fixed MSVC batch at `/std:c++17`; and you cannot run ASan on a DLL injected into a
protected game process. It also collides with the ponytail hook, which forbids unrequested
abstractions. The one thing worth taking from it, `/W4`, was taken on its own the same day and
found two cosmetic warnings in 4,336 lines. If a second opinion mechanism for design calls is
ever wanted, copy its "The Fool" pattern into `~/.claude/skills/` rather than pulling 67.

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

## The download zip is generated out of COLLECTIONS.md

`tools/make-zip.ps1` scrapes COLLECTIONS.md for lines matching ``^- `([a-z0-9_]+)`$`` and makes
one empty folder per match, so the documented list and the shipped folders can never disagree.
The catch: a name only ships if it is ALONE on its bullet. Three names on one bullet, or a bullet
with trailing prose, is skipped in silence and the zip builds happily without them. 0.8.27 added
the nine GTA World clothing collections (`mp_m_freemode_01_mp_m_gtawclothes1..3`, the `_p_` prop
forms, same for `mp_f_`) and seven server pets (canine, caninepd, caninesd, caninefd, caninesp,
blackcat, browncat) that way: 194 folders became 210. The names came out of the `collection:`
lines of a real GTAW log, not the game files, so the server can change them at any time.

## Tests (moved out of scratch/ 2026-09-12, and CI runs them now)

Five plain assert/printf harnesses in `tools/`, one runner: `cmd //d //c "call
.\tools\run_tests.bat"`. It does vcvars once, builds each test against one shared source
list, and exits nonzero if any fails. The workflow runs it BEFORE the build, so a red test
never reaches a release.

- `test_gate_keys.cpp` isAllowedKey, `test_slotkey.cpp` slotKeyFor (nested folders),
  `test_settings.cpp` 14 groups, `test_reval.cpp` the sweep, `test_ctlpath.cpp` ctlPath.

Four of the five used to live in `scratch/`, which is gitignored, so they were not in the
repo and CI had never run one. The fifth WAS tracked and had been failing since 0.8.8: it
still expected a root `.ydr`/`.yft` to be refused, which the weapon and prop work
deliberately changed. Nobody saw it because nothing ran it. That is the whole argument for
keeping them in the repo: a test outside CI rots into a lie about the code.

`/W4` is on since the same day. minhook is vendored and warns about its own style, so
build.bat compiles it separately at `/W0` and the warnings the build prints are only ever
ours. The two it found on the way in (a shadowed counter in scanner.cpp, a LOG_DEV-only
variable in clean_shot.cpp) were cosmetic: the .asi came out byte-identical to the one
0.8.26 shipped, which is the proof that nothing in that pass changed behaviour. `/WX` was
deliberately NOT added: a future MSVC inventing a new warning would then break the build at
tag time, in CI, for nobody.

## Build & release

- **Builds are reproducible (0.8.6).** `/Brepro` on the link line, and CI builds twice and compares
  SHA-256 before publishing. Verified: two clean local builds are byte-identical. This is
  load-bearing for the AV story, because "read the source, build it yourself and compare" is only
  worth something if the binary is actually checkable. Do not drop the flag or the CI step.
  **NEVER `sed -i` build.bat**: it strips the CRLF line endings that batch `for /f` needs and the
  build dies with "'M' is not recognized". Edit it byte-wise (python, `open(p,'rb')`).
- `builddev.bat` — the same compile plus `/DTEXOVERRIDE_DEV`, which turns on `LOG_DEV` tracing
  (`src/core/logger.h`): one line per new mechanism, so a local session can prove the frame pump
  is ticking, which symbols resolved, when OnFirstLoadCompleted fired, and every refresh keypress.
  `LOG_DEV` compiles to `((void)0)` without the define, so dev lines can never reach a release
  build. NEVER ship the .asi builddev.bat produces.
- `build.bat` — MSVC (VS Build Tools 2022, "Desktop development with C++"). `/MT /EHsc /std:c++17`,
  no /clr. The user's shell prints `fastfetch`/`vswhere` noise and exits nonzero even on success;
  trust the `Built texoverride.asi` line, not the exit code.
- `.asi` is gitignored — CI builds it and attaches it to the GitHub Release. Never commit the .asi.
- Release: bump `TEXOVERRIDE_VERSION` in `src/core/version.h` AND `FILEVERSION`/`PRODUCTVERSION`/the two
  version strings in `texoverride.rc` (they must match), date the CHANGELOG section, then one
  chain: `git add <files> && git commit -m ... && git tag vX.Y.Z && git push origin main && git
  push origin vX.Y.Z`. NOT `--follow-tags` (see the gotcha in the 0.8.26 section). Verify with
  `git ls-remote --tags origin`. CI builds both the push and the tag; the tag build makes the
  release with the changelog notes + the .asi + the zip.
- A version bump CHANGES the binary, because the version string is compiled in and the .rc
  version resource moves. Do not write "byte identical to the last release" in release notes
  without building first: 0.8.27 was identical to 0.8.26 right up until the bump, and the draft
  notes said so until the hashes were actually compared.
- **The FX_ASI_BUILD stamp** (`texoverride.rc`): FiveM refuses ASIs on game build 2189+ that don't
  claim the running build. One `FX_ASI_BUILD <build> BEGIN "\0" END` line per supported build
  (currently 2189 up to 3889; 3889 = mp2026_01, The Kortz Center Heist, added 2026-09-08). New game build → add a line or the plugin silently stops loading.
- **Workflow actions are pinned to the LOWEST major that runs on Node 24** (2026-09-12):
  `checkout@v5`, `upload-artifact@v6`, `attest-build-provenance@v3`. Two traps, both checked in
  the real `action.yml` rather than assumed (`gh api repos/<action>/contents/action.yml?ref=<tag>`
  then read `runs.using`): `upload-artifact@v5` is STILL node20, so the obvious one-major bump
  fixes nothing; and `attest-build-provenance` is a composite action with no runtime of its own,
  so it never appeared in the deprecation warning, but v2 pins `actions/attest@v2.4.0` which IS
  node20. That step only runs on tags, so it would have failed at RELEASE time, after the build,
  before `gh release create`. Deliberately not the newest majors: checkout v7 changes
  pull_request_target fork checkout, upload-artifact v7 moves to ESM, attest v4 is a thin wrapper
  over actions/attest. The workflow carries a comment saying all of this.
- **Tagged releases announce themselves on Discord** (2026-08-29). The last workflow step POSTs a
  `tool_announce` job to `blancodagoat.dev/api/enqueue-discord-job` (Bearer `ANNOUNCE_TOKEN`, a
  repo secret); the API queues it on Redis and BlancoBot posts the embed. Nothing new was built on
  the site side: `tool_announce` already existed as the generic non-CM announcement type and
  texoverride already had its row in `links-embed.js`. The embed is a HEADLINE LIST, not the
  changelog: first sentence of each top-level entry, sub-paragraphs dropped, every line a bullet.
  Two earlier versions were rejected, first for the changelog's 100-column editor wrapping
  breaking sentences mid-line, then as too long and hard to read. 2,187 chars became 370. If a
  sub-paragraph survives to column zero it renders as loose text and half the list loses its dots.
  To send one by hand: write the payload to JSON and run `node scripts/announce-tool.js <file>`
  from `D:\Projects\blancodagoat.dev`. `gh run rerun` on an old tag will NOT announce, since it
  uses the workflow file from that tag's commit.
- Installing over a running FiveM: the loaded .asi is locked. Rename the old one aside
  (`mv ...asi ...asi.old`) then copy the new one in; delete the .old after FiveM closes.

## Settings: one file, and how the marker files went away (0.8.12 to 0.8.13, issue #20)

`tex_overrides\_settings.txt` is written on first run (`src/core/settings.cpp`): every option
listed, switched off, with a plain English note above it. `off`, `debug`, `texture_budget`,
`auto_update`, `no_update_check`, and since 2026-08-29 `refresh_key` (default `f11`; f1-f12, a
letter, a digit, or `off`). `yes`/`on`/`true`/`1` mean on. The refresh_key DEFAULT lives in the
`Settings` struct as well as in the file text, because the file is never rewritten and existing
installs would otherwise never get a new option. `vkFromName` lives in `settings.cpp` rather than
beside its caller so the existing test harness can reach it; group 11 of
`tools/test_settings.cpp` covers it. **Only ever created, never
rewritten**, so a user's edits and their own notes survive updates.

chocomintw filed #20 saying the `_` files were unstandardized and some worked outside
`tex_overrides`. The location half was wrong: every one of them already resolved against
`g_overrideDir`, and only `texoverride.log` sits outside, because it is opened before
`tex_overrides` is ever read. The real problem was the naming. Explorer hides known extensions,
so `_OFF` becomes `_OFF.txt` for some people and `_debug.txt` becomes `_debug.txt.txt` for
others, and no amount of renaming fixes a trap the file manager sets.

0.8.12 shipped that as "accept both spellings and keep the markers forever", which the user
rejected on the obvious ground that two ways to set every option is not standardising anything.
**0.8.13 migrates instead** (`migrateSettings`): the first launch that finds a marker file copies
its value into `_settings.txt`, deletes it, and logs what it moved. THIS launch still behaves as
the last one did, because `loadSettings` has already read the markers before anything is deleted.

Load-bearing details:
- `loadSettings` runs in `Setup()` under the loader lock, so it READS only, never writes, never
  logs. `migrateSettings` runs from `backgroundStartup` on the beat thread, which is both where a
  write is safe and where the log exists to say what happened. Do not merge them.
- The file is rewritten LINE BY LINE, so the notes in it and anything the user added survive. A
  key whose line the user deleted is appended rather than silently lost.
- A player running with `_off` never reaches migration, because `Setup()` returns before the beat
  thread starts. That is correct, not a gap: `_off` promises the plugin does nothing at all, and
  the moment they delete `_off` to turn it back on there is no marker left to migrate.
- `ctlPath()` in `src/core/utils.cpp` still accepts bare AND `.txt` names, because migration has
  to find whichever form the user actually created. `_quarantine.txt` and `_inflight.txt` are
  plugin-WRITTEN and keep fixed names.

Checks live in `tools/test_settings.cpp` (10 groups: comments, junk lines, spacing, capitals,
every truthy spelling, markers still applying on the launch that migrates them, markers deleted
afterwards, a contradicting line overwritten, a deleted key appended, and migration being
idempotent). Run every test with `cmd //d //c "call .\tools\run_tests.bat"`.

Two more 0.8.12 changes worth not re-deriving:
- **`debug` lifts the 500 cap** on the `Server file:` list. Looking up the exact name of one
  server prop is the whole reason anyone reads that list, and the cap blocked the one job it has.
- **A file with no `RSC7` header is refused** in `cannotLoad` (`src/streaming/rsc.cpp`). A raw
  dump kills the game inside its own loader with "Invalid fixup, address is neither virtual nor
  physical". `.ymt` is exempt, since a metadata file is not always an RSC7 resource.

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

### 2026-08-29 round

Shipped 0.8.12: the settings file (section above), the RSC7 gate, `debug` lifting the 500 cap,
a README section on replacing an existing vehicle, and the Discord release announcement.

Then 0.8.13, because 0.8.12 had not actually standardised anything: it added a settings file and
left all six marker files working beside it, so every option had two ways to set it. The user
caught that ("i mean we standardized no?"). Marker files now migrate and delete themselves. The
v0.8.13 tag build is also the first release where the Discord announcement fired on its own,
`enqueue-discord-job: HTTP 200`, with no hand-holding.

- **Issue #16 closed** with the McAfee answer. **#19 CLOSED 2026-08-29: Smart App Control.**
  Same dialog, different reporter, on b3258. The build stamp was ruled out first by walking the
  shipped PE's resource directory (16 `FX_ASI_BUILD` types, 2189 to 3788, so b3258 is there).
  They had Defender only, and their screenshot showed correct exclusions on the .asi, `mods\` and
  `plugins\`, which is what ruled out the McAfee answer and left Smart App Control. It is a
  Windows 11 feature, separate from the antivirus, it blocks unsigned binaries with no notice
  anywhere, and antivirus exclusions do not apply to it. **So there are now TWO confirmed causes
  of "Couldn't load texoverride.asi", McAfee and Smart App Control**, and the fastest question to
  ask is Smart App Control, because it is one settings screen.
- **#20 closed 2026-08-29.** The comment on it had described the 0.8.12 design ("every old marker
  file still works"), which 0.8.13 made false; the re-post noted here earlier never actually
  landed, so it was corrected and closed. Worth remembering that chocomintw files an issue saying
  a PR is coming, so reply with the design BEFORE they build against a premise that turns out to
  be wrong.
- **No open issues, no open PRs** as of 2026-08-29.

### The README split (2026-08-29, pushed: 1bcc80e, 50bcbd6, 44debac, 761f9bb)

The README was 823 lines and users were not reaching the answers in it, which is what turned #16
and #19 into three-round interrogations. Now ~320 lines:

- Six sections moved to `docs/`, each left behind as a short summary plus a link:
  `replacing-files.md` (the eight "Replacing X" sections, now one 8-row table in the README),
  `reading-the-log.md`, `textures-and-budget.md`, `how-it-works.md`, `antivirus.md`, `build.md`.
- New top-level section **`## It says "Couldn't load texoverride.asi"`**, placed right after
  Install: file size, Smart App Control, third-party antivirus, then a PowerShell `LoadLibrary`
  test that reports the real Windows error code. Anchor is
  `#it-says-couldnt-load-texoverrideasi`, and `docs/antivirus.md` and the issue form both link to
  it, so do not rename that heading.
- Full grouped table of contents at the top, 21 links, every section covered.
- Credits now name chocomintw (PRs #11 weapons, #12 log levels, #17 the `src/` split, #18 the
  updater, plus issue #20) above chunguscodes.

**The real lever was the issue form, not the README.** `bug_report.yml` REQUIRES
`texoverride.log`, which is the one thing a will-not-load reporter cannot produce. Added
`.github/ISSUE_TEMPLATE/wont_load.yml` ("Plugin will not load") with version, exact file size,
antivirus dropdown and Smart App Control dropdown as REQUIRED fields, so the report arrives with
the three answers already in it. `config.yml` turns blank issues off and puts two doc links above
the template list. Templates only take effect on the default branch.

**Nested folders: DECLINED 2026-08-29, then BUILT 2026-09-02 (0.8.17) after a user asked for
`clothingpack1/<collection>/...`.** The rule is `slotKeyFor` in `src/streaming/gate.cpp`, shared
by `walkDir` and `rescanTree`: a `.ydd`/`.ytd`'s collection is its immediate parent folder and
everything above is ignored; loose types and non-ped-part `.ytd` load by bare name from any
depth; a ped-part-named file whose parent is not a collection is refused with a reason. Flat
layouts produce identical keys (test group 1). Checks: `tools/test_slotkey.cpp`
(7 groups).

**`_override` folders (0.8.22, 2026-09-06).** Duplicate keys used to be "first copy the walk
reached wins", i.e. FindFirstFile order, which a user cannot steer. Now a folder whose name ends
in `_override` (ANY segment of the path, matched as the substring `_override/` in the lowercased
forward-slashed rel, `isOverridePath` in gate.cpp) wins the duplicate. `g_seenKeys` in `walkDir`
went from a set to `key -> index into out`, so a later priority copy REPLACES the held `Cand` in
place rather than depending on walk order; `Cand::prio` records which one is holding. Two
`_override` copies of the same key are back to first-wins. `slotKeyFor` also strips a trailing
`_override` off the COLLECTION segment, so `mp_m_freemode_01_override/uppr_013_r.ydd` keys as
`mp_m_freemode_01/uppr_013_r.ydd` instead of inventing a collection that nothing streams (that
name would have passed `isPedCollection`'s prefix test and registered under a dead slot).
Startup only: live reload cannot re-point an already-registered slot at a different file, so a
new file whose key is already known now logs "restart FiveM to use this copy" instead of doing
nothing in silence. Log line is `PREFERRED` (Scan, INFO), deliberately NOT `OVERRIDE`, which is
already the Claim category's `OVERRIDE-REG/-TAKEOVER/-WAIT/-FAILED` family.

### 2026-08-29, second round: the exports session (LOCAL, NOTHING RELEASED)

Started from "which FiveM dll draws `mod packs loaded` and the version in the other corner", which
turned into the exports section above. Working tree state, all UNCOMMITTED and UNTESTED in game:

- new `src/core/cfx.{h,cpp}` (symbol resolution + fwEvent connect)
- live reload on `OnMainGameFrame`, PeekMessageW shim demoted to fallback
- `refresh_key` (F11) and the manual rescan path through the watcher
- placement layout solved once per session and shared
- texture pool usage reporting, real per-slot memory on LOADED
- `OnFirstLoadCompleted` as a second watcher trigger
- `builddev.bat` + `LOG_DEV`

**Before any of this is released:** bump `src/core/version.h` AND the four fields in
`texoverride.rc`, write the CHANGELOG section, and add README lines for the refresh key (it is
user-facing and currently documented nowhere but `_settings.txt`). CHANGELOG.md was deliberately
NOT touched: the build workflow takes the top `##` section as the release notes, so a placeholder
section there would ship as the notes for whatever is tagged next. Draft for when it is time:

    - Live reload now runs on FiveM's own per-frame event instead of a patch into the game's
      message pump. One less patch, and the work always lands on the game thread.
    - New refresh key. Press F11 in game to read tex_overrides again straight away. Set
      refresh_key in _settings.txt to any f1-f12 key, a letter, a digit, or off.
    - Tattoo placement files with one or two presets now work, and so do files where every
      value was edited. The layout only has to be worked out once per session.
    - The log now says how much of the texture budget the game is actually using, warns once
      when it passes 90 percent, and reports what each file really costs in memory including
      the textures it shares.

**TESTED IN GAME 2026-08-29, live GTAWorld session, b3751. Everything works.**

- frame pump: `DEV frame pump: 1344-1424 tick(s) in the last 10s`, so the fwEvent splice into
  `OnMainGameFrame` is real and runs every frame on the main thread. The layout was re-verified
  from both sides afterwards: Cfx's own inlined Connect does `operator new(0x58)` with impl at
  +0x38, next +0x40, order dword +0x48, cookie +0x50, `lock xadd [ev+8]`; `DoGameFrame` walks it
  reading exactly +0x38 and +0x40 with head as the event object's first qword.
- live reload: confirmed, see the section above.
- refresh key: `DEV refresh key vk=0x7A pressed` then `Refresh key: nothing has changed since the
  last look`, correct because the watcher had already picked the file up 12 seconds earlier.
  **BUT the user pressed twice and only one press was logged.** The DEV line fires on the key edge
  before anything else, so the second press was never seen. Prime suspect is the foreground-window
  check in `refreshKeyTick`. Not fixed yet; log the REJECTED edges at DEV so "not seen" and
  "seen but not foreground" stop looking identical.
- texture pool usage: 0.1 -> 2.89 GB of 8.75 GB as the world loaded. `grcResourceCache::
  GetUsedPhysicalMemory` disassembles to `mov rax,[rcx+0x20168]; ret`, a pure field read, verified
  in `rage-graphics-five.dll`; `GetTotalPhysicalMemory` right beside it DOES call into the engine,
  which is why it stays unused.
- real per-slot memory: 11 `LOADED` lines carrying the figure, e.g. `w_pi_pistol.ydr from your
  file (0.9 MB in memory with dependencies)`.
- 769 claims, 0 rejected, 0 errors, 0 faults.

**`OnFirstLoadCompleted` never fired in either session.** It resolves fine and connects fine, and
the watcher started off `g_idsReady` instead, so nothing depended on it. But it is the safety net
for a server that streams NOTHING, and a net that never fires is not a net. Most likely it had
already fired before we connect. Unresolved; check before release.

**One crash, not reproduced.** First launch of the dev build died at ~26.4s during load,
`GTA5_b3751.exe+137FE74`, hash `bravo-kilo-oklahoma`, seven frames of pure game code with nothing
of ours on the stack. No `_inflight.txt`, so nothing was mid-registration, and the plugin log is
clean to the last beat. Two later launches were fine. Cannot be pinned on the plugin and cannot be
cleared either. FiveM's log shows five `OpenArchive: only non-encrypted RPF7 is supported` lines
about 1.8s before it, which is the user's mods folder, not us.

Testing setup on this machine: dev build installed at
`D:\FiveM_Client\FiveM.app\plugins	exoverride.asi`, previous release build beside it as
`.prev`, `_settings.txt` written with `debug = yes` and `no_update_check = yes` so nothing
replaces the build under test. 771 files in tex_overrides. Look for the `DEV` lines; `DEV frame
pump: N tick(s) in the last 10s` is the one that proves the fwEvent connect took.

### 2026-08-25 round (all handled, nothing open but issue #16)

- **#14** `_OFF` hook-free and **#13** live-queue bound/coalesce (chunguscodes): built clean,
  reviewed, **merged** (`22751fe`, `e638d45`). `gh pr merge` is blocked by the permission
  classifier; merge with `git merge --no-ff` + push instead, same history shape.
- **#17** modularize into `src/` (chocomintw): first declined, then the user said "I want the
  refactor". **Merged** as `8fc86a7` on top of #14/#13; both were re-applied by hand onto the new
  files (`_OFF` early return in `src/core/core.cpp` `Setup()` + `SetupSafe() && !g_off` in
  `dllmain.cpp`; the 10 ms pump throttle and coalescing `submitBatch` in
  `src/features/live_reload.cpp`). Faithfulness check: all 120 log format strings identical
  before/after the split; SEH/pattern/crash-saver markers match. The PR's CLAUDE.md had wrong
  paths (`src/rsc.cpp`, `src/version.h`), fixed to `src/streaming/rsc.cpp`, `src/core/version.h`.
  CI (build twice + compare) green on the merge.
- **#15** fail-closed manifest mode: **declined** (a second install model, restart-only, server
  specific name; #13 is the right-sized answer to the copy-in-progress problem it cites).
- **Issue #10** closed (reporter confirmed 0.8.8 fixed b3407). **Issue #16 closed 2026-08-29**
  with the McAfee answer below. ("Couldn't load
  texoverride.asi", no log): that dialog is `LoadLibrary` returning NULL in Cfx's
  `asi-five/Component.cpp` line ~251, AFTER the FX_ASI_BUILD check (a build mismatch only prints
  to the F8 console). The ASI's static imports are only KERNEL32/USER32/SHELL32/WINHTTP (plus
  bcrypt.dll since 0.8.11, for the updater's SHA-256; verified by dumping the shipped PE), DllMain
  always returns TRUE and SetupSafe swallows faults, so the file never ran: most likely AV
  quarantine/block of the fresh download, or a damaged file. Asked for the Save-information text,
  Windows Security protection history, and the file size (0.8.8 = 348,160 bytes). Waiting.
  **SOLVED 2026-08-26 on a second report: McAfee.** Intact file (0.8.10 = 357,888 bytes), one
  copy, Smart App Control off, Exploit Protection empty, `LoadLibrary` from PowerShell returned a
  handle with error 0, no `texoverride.log` ever written, CitizenFX log shows `asi:five` then the
  error 750 ms later. McAfee's driver vetoes unsigned DLL loads into the game process with no
  notice anywhere. Uninstalling it fixed it. README has the note. The 750 ms gap is the tell.
- **End-to-end on the merged main, live GTAWorld session, b3751 (2026-08-25 02:03):** patterns
  found, hook installed, budget 8.8 GB, 746 overrides, 12 collections, 32 server collections
  mapped (canine peds included), 500-cap on overridable server files with the "counted, not
  listed" line, 10 OVERRIDE-REG, 71 reclaims through the DLC-mount contest, back to 746 held / 0
  contested by beat 5, 0 errors, 0 faults. NOT tested: the `_OFF` A/B (two restarts, user's
  call) and anything live-reload. The build under test is `plugins/texoverride.asi` (352,768
  bytes); the previous one is beside it as `texoverride.asi.prev`.
- **Process rule learned the hard way:** never kill, restart or launch FiveM yourself; the user
  is often mid-session. Swap the .asi (rename aside + copy) and ask them to restart.
- `build.bat` from a script: `cmd /d /c 'cd /d D:\Projects\texoverride && call ...\build.bat'`
  (`/d` skips the user's cmd AutoRun, which otherwise changes directory and breaks it).

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

**2026-08-29: two of the dead ends here are not dead.** Cfx exports `ForceMountDataFile`, which is
the wrapper this attempt was missing when it called the game's mounter directly and crashed even
on a known-good file. And `fiDeviceRelative`'s ctor, `SetPath` and `Mount` ARE published: they are
exported by `rage-device-five`, so the "not published" line above is wrong. Neither is tested.

Deliverable if this is ever wanted again: it is a FiveM SERVER RESOURCE, which is a supported path
and about an hour of work (`data_file AUDIO_GAMEDATA/AUDIO_SOUNDDATA/AUDIO_SYNTHDATA/AUDIO_WAVEPACK`,
one wave pack folder per container, configs kept separate and never merged). Singleplayer installs
the dlc.rpf as-is. RAGE MP also loads it as-is, which is why the pack works there and not here.
