# texoverride

Client-side clothing and texture replacement for FiveM. Drop `.ydd` / `.ytd` files into a folder and
your client draws them. No archive edits, no re-encryption, nothing sent to the server.

It exists because FiveM's own client-side mod paths (the `mods/` folder, pseudo-DLC wrapping, caret
naming) cannot deliver ped textures. They drain before the connect handshake, and the pseudo-DLC
mount is done non-overlay. This plugin works at the game's streaming layer instead, doing at runtime
what a server `stream/` folder does: it registers your loose files as overrides for ped component
slots.

## Expect bugs

This is a working proof of concept. It has rendered replacement clothing correctly in live sessions,
and it will still break in ways nobody has hit yet. When it does, the log is built to tell us why.

Found one? [Open a bug report](../../issues/new/choose). The form asks for two things: what you
expected to happen, and the contents of `plugins/texoverride.log`. That is usually enough to fix it.

The log is safe to paste publicly. Override paths are written relative to `tex_overrides/`, so it
never contains your Windows user name or anything else about your machine.

## Install

1. Grab `texoverride.asi` from [Releases](../../releases), or build it yourself (below).
2. Copy it to `%LOCALAPPDATA%\FiveM\FiveM.app\plugins\`.
3. Make a `tex_overrides` folder next to it and drop your files in.
4. Launch FiveM.

It only loads on servers that allow plugins (`sv_pureLevel 0`). Level 1 demands a Cfx signature a
self-built ASI cannot have, and level 2 turns the ASI loader off entirely.

## Folder layout

```
plugins/
  texoverride.asi
  tex_overrides/
    mp_m_freemode_01/                     <- base male freemode
      teef_004_u.ydd
      teef_diff_004_a_uni.ytd
    mp_f_freemode_01/                     <- base female freemode
      lowr_000_r.ydd
    mp_m_freemode_01_mp_m_gunrunning_01/  <- a vanilla DLC collection
      teef_012_u.ydd
```

The folder name must be the exact collection the game requests. [COLLECTIONS.md](COLLECTIONS.md)
lists all 186 valid names. Matching is deliberately exact rather than fuzzy, because matching on
filename alone would cross-wire unrelated items (a dog head into a human head slot, for example).

Only `.ydd` (models) and `.ytd` (textures) are picked up. Only human freemode-ped collections are
ever touched: story peds, animals, vehicles, props, maps and scripts are refused by a safety gate in
the code.

Not sure which collection an item streams from? Launch once and read the log. It lists every
collection the server requests, tagged with whether the plugin can reach it.

## Turning it off

Create an empty file named `_OFF` (no extension) inside `tex_overrides/` and relaunch. The plugin
still loads but registers nothing and redirects nothing.

## Reading the log

Everything goes to `plugins/texoverride.log`, wiped and rewritten on every launch.

| Line | What it tells you |
|---|---|
| `texoverride 0.1.0 loaded (date)` | The plugin is in and running |
| `loaded N override(s)` | The folder scan found your files |
| indented `collection N file(s)` lines | How your files were grouped per collection |
| `registerRawStreamingFile @ ...` | The hook target was found |
| `MH_EnableHook: MH_OK` | The hook is live |
| `OVERRIDE-REG slot <- file` | Your file now owns that slot |
| `REDIRECT name -> file` | A streamed asset was swapped for your file |
| `collection: name [tag]` | A collection the server streams, and whether it is reachable |
| `alive (beat N) ...` | Heartbeat; the plugin survived this long |
| `pattern NOT FOUND` | The game updated and the byte pattern needs re-deriving |

## How it works

For the technically curious, and for server owners deciding whether to allow it.

The plugin hooks one game function, `registerRawStreamingFile`, the same routine FiveM uses to
register loose and server-streamed files. The byte pattern that locates it comes from Cfx's open
source tree (`gta-streaming-five/src/Streaming.cpp`).

It hooks the game module (`GTA5.exe`) only, never FiveM's own DLLs. FiveM's `legitimacy` anti-tamper
terminates the process if you modify Cfx components; a hook in the game module is the same surface
trainers and `PackfileLimitAdjuster.asi` use, and it survives full connected sessions.

Base freemode clothing lives inside `x64v.rpf` and never passes through that function, so waiting to
intercept it would wait forever. Instead the plugin calls `registerRawStreamingFile` itself and
registers your loose file under the base slot name. The archive copy loses to the newer
registration, exactly the way a server `stream/` folder overrides a base asset. Streamed DLC
collections do pass through the hook, so those are redirected on an exact `collection/file` match.

The path handed to the game is a plain absolute path, which FiveM's VFS opens without complaint. The
game reads the whole resource from your file (header, page flags, data), so there is no size or flag
mismatch to manage.

MinHook needed one patch: FiveM blocks `CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD)`, which
MinHook's thread-freeze step relies on, so the vendored copy proceeds without suspending threads.
The patch is a few bytes applied once at startup and is commented in `minhook/src/hook.c`.

Nothing is transmitted anywhere. The plugin reads a local folder and changes what your client draws.
Other players keep seeing whatever the server streams.

## Ban risk, stated plainly

The total write to game memory is one inline hook of about five bytes on a cosmetic asset-routing
function, plus MinHook's trampoline page. Nothing else is patched. The plugin never reads or writes
health, money, weapons, position, entity pools, network events or player state, so there is no
gameplay advantage in it and nothing that changes what other players see.

The residual risk is real and worth stating: a generic code-integrity scan can flag the patch
regardless of intent, and Cfx's tolerance of game-module hooks is practice, not a written guarantee.
It has run full connected sessions without a ban. Keep it to servers that opt in (`sv_pureLevel 0`
is the owner's own setting) and do not spread builds around.

## Build

You need Visual Studio Build Tools 2022 with the "Desktop development with C++" workload. Then:

```
build.bat
```

If the batch file's vcvars auto-detect fails on your machine, run the same thing from an "x64 Native
Tools Command Prompt for VS 2022"; the batch skips detection when the environment is already set up.

### The build stamp

FiveM refuses any `.asi` on game build 2189 or newer that does not claim support for the running
build. The claim is the `FX_ASI_BUILD` resource in `texoverride.rc`, one line per supported game
build:

```
FX_ASI_BUILD 3751 BEGIN "\0" END
FX_ASI_BUILD 3788 BEGIN "\0" END
```

When FiveM moves to a new game build, add a line with the new number and rebuild, or the plugin
silently stops loading. This is why community ASIs go dead after every update.

### CI builds

GitHub Actions builds every push, so you can grab a fresh `texoverride.asi` from the Actions tab
without installing anything. Pushing a tag like `v0.2.0` builds and publishes a release with the
binary attached.

## Limitations

- Proof of concept. It works, and you should still keep an eye on the log.
- Needs a rebuild whenever FiveM bumps the game build (see the stamp above). Major game updates can
  also shift the byte pattern.
- Exact matching means you need the right collection name. Servers that re-stream clothing under
  their own custom DLC collections may not use the base collection for a given menu item. Trust the
  log over the base name.
- Client-side only. Other players and the server see no difference.

## Files

```
dllmain.cpp        the plugin: folder scan, hook, proactive override
build.bat          MSVC build
texoverride.rc     FX_ASI_BUILD stamp
minhook/           vendored MinHook with the Freeze() patch
COLLECTIONS.md     all 186 valid collection folder names
```

MIT licensed. MinHook is copyright Tsuda Kageyu, BSD-2-Clause; the Hacker Disassembler Engine inside
it is copyright Vyacheslav Patkov.
