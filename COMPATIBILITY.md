# Compatibility

## Current release candidate

- Fork version: `0.7.3-emk.1`
- Upstream base: `v0.7.3` at `73f09ba`
- Declared FiveM game build: `b3751`
- Architecture: Windows x64
- Runtime: GTA V Legacy through FiveM

Only b3751 is stamped in the ASI resource table. New GTA game builds must pass pattern, manager,
raw-streamer, budget-table, live-reload and connected-session checks before they are added.

## Verification completed

- MSVC 19.44 release build succeeds with `/O2 /MT /EHsc /std:c++17`.
- The first-party source compiles cleanly with `/W4 /sdl`.
- `/Brepro` produces byte-identical binaries across consecutive clean builds.
- All 1,098 resources in the local Balanced Grape pack have complete RSC7 headers.
- The active upstream log reports a 0.1 second scan for that pack.

## Runtime checks still required for a public tag

- Launch, connect and outfit validation on GTA World FiveM b3751.
- Ten-minute route comparison with the upstream ASI and the emK build.
- Active-resource overwrite/delete refusal, live safe-add, and placement XML tests.
- Sixty-minute populated-server soak while watching frame time, texture loss and collision loading.
- A separate GPU/Windows configuration before claiming broad compatibility.

This plugin uses an inline hook and writes selected streaming handles. Server policy and FiveM
pure mode still decide whether plugins are permitted. Read the ban-risk section in
[README.md](README.md) before distributing it.
