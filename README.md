# texoverride

texoverride changes how clothes, tattoos and other textures look in GTA V on FiveM. Only you see
the change. You put files in one folder, and the game shows your versions instead of the
originals. It never edits the game's own files, and it never sends anything to the server.

Why this exists: FiveM's built-in ways of loading client mods cannot replace character clothing
textures. This plugin does it the same way servers do when they add their own clothes. It just
does it on your computer.

## Expect bugs

This is a working proof of concept. It has worked in real play sessions, and it will still break
in ways nobody has hit yet. When it breaks, the log file is built to tell us why.

If something goes wrong, [open a bug report](../../issues/new/choose). The form asks for two
things: what you expected to happen, and the contents of `plugins/texoverride.log`. That is
usually enough to fix it.

The log is safe to paste publicly. It never contains your Windows user name or anything else about
your computer.

## Install

1. Download `texoverride.asi` from [Releases](../../releases), or build it yourself (see
   [Build](#build)).
2. Open File Explorer, paste `%LOCALAPPDATA%\FiveM\FiveM.app\plugins` into the address bar, and
   press Enter. Copy `texoverride.asi` into that folder.
3. In the same folder, create a new folder named `tex_overrides`. Your files go in there.
4. Start FiveM.

The plugin only works on servers that allow plugins. Some servers block them with a setting called
"pure mode". On those servers the plugin does nothing at all.

## Replacing clothes

Every clothing item in the game belongs to a named group. The game calls this group a
*collection*. To replace an item, make a folder with the collection's exact name and put your file
inside it:

```
plugins/
  texoverride.asi
  tex_overrides/
    mp_m_freemode_01/                     <- male character, base game clothes
      teef_004_u.ydd
      teef_diff_004_a_uni.ytd
    mp_f_freemode_01/                     <- female character, base game clothes
      lowr_000_r.ydd
    mp_m_freemode_01_mp_m_gunrunning_01/  <- male, Gunrunning DLC clothes
      teef_012_u.ydd
```

The plugin reads two file types. A `.ydd` file is a 3D model. A `.ytd` file holds the textures,
which are the images painted on the model.

The folder name has to be exactly right. [COLLECTIONS.md](COLLECTIONS.md) lists all 186 valid
names. This strictness is on purpose. Matching by file name alone could put the wrong thing in the
wrong place, for example a dog's head on a human. For the same reason, the plugin only ever
touches player-character (freemode) clothing. It refuses to touch story characters, animals,
vehicles, props and maps.

If you do not know which collection an item belongs to, start the game once and read the log. It
lists every collection the server uses and marks whether the plugin can reach it.

## Replacing tattoos, skin and other overlays

Tattoos, skin textures, face paint, beards and similar body textures do not belong to any
collection. Each one is a single `.ytd` file with its own unique name. To replace one, put your
`.ytd` straight into `tex_overrides`, without any folder:

```
  tex_overrides/
    mp_gr_tat_027_m.ytd          <- a tattoo
    mp_fm_skin_m_up_whi.ytd      <- a skin texture
```

The file name is the whole match. Your file replaces the one texture with that exact name and
nothing else. Custom server tattoo packs work the same way, so any name is accepted here. Model
files (`.ydd`) are never accepted outside a folder.

To find the right file name for a tattoo, open
[docs/overlay_index.tsv](docs/overlay_index.tsv) in a spreadsheet app or a text editor and search
for the tattoo. The `txd` column is the file name to use.

## Moving tattoos (position, size, rotation)

The texture does not decide where a tattoo sits on the body. That comes from numbers in a game
file called `overlays.xml`. The plugin can change those numbers for you. Three steps:

1. **Find the file that owns your tattoo.** Look the tattoo up in
   [docs/overlay_index.tsv](docs/overlay_index.tsv). The `source` column names the exact
   `overlays.xml` inside the game files. The other columns show the tattoo's current position
   (`uvX`, `uvY`), size (`scaleX`, `scaleY`) and rotation.
2. **Copy that file out and edit it.** Use [OpenIV](https://openiv.com/) to open the path from the
   `source` column and save the `.xml` to your computer. Open it in any text editor and find your
   tattoo by name. Change its numbers: `uvPos` is the position, `scale` is the size, `rotation` is
   the angle. Only change the tattoos you want moved. Leave the rest of the file alone.
3. **Put the edited `.xml` into `tex_overrides`**, next to your `.ytd` files, and start the game.

One file to leave alone: `shop_tattoo.meta`, which sits next to `overlays.xml` in the game files.
It holds shop prices and unlocks, not looks, so the plugin does not read it. Everything about how
a tattoo looks and where it sits is covered by the `.ytd` and the `overlays.xml`.

The plugin is careful with these files. Before changing anything in the running game, it checks
that the entries you did not touch still match the game exactly. If they do not line up, because
it is the wrong file or the game has updated, it changes nothing and says so in the log. This
check is also why you should leave most of the file unedited. If nearly every entry is changed,
there is nothing left to check against, and the plugin skips the file.

## Update check

At startup the plugin asks GitHub one question: what is the newest release number? If a newer
version is out, a small popup tells you and points to the download page. That is the plugin's
only network use. It sends nothing about you, your game or your files, and if you are offline it
quietly does nothing.

To turn the check off, create an empty file named `_NO_UPDATE_CHECK` inside `tex_overrides`.

One honest limit: when FiveM moves to a new game build, old plugin versions stop loading at all
(see [The build stamp](#the-build-stamp)). A plugin that does not load cannot show a popup, so
after a big game update, check the releases page yourself.

## Turning it off

Create an empty file named `_OFF` (no file extension) inside `tex_overrides` and restart FiveM.
The plugin stays installed but does nothing, including the update check.

## Reading the log

Everything the plugin does is written to `plugins/texoverride.log`. The file starts fresh on every
launch.

| Line | What it means |
|---|---|
| `texoverride x.y.z loaded (date)` | The plugin is in and running |
| `loaded N override(s)` | Your files were found |
| indented `collection N file(s)` lines | How your files were grouped |
| `placement: collection ... N preset(s)` | Your edited `.xml` was read |
| `placement: ... layout solved` | The `.xml` matched the game; changes can be applied |
| `streaming manager @ ...` | Internal: found what it needs to keep overrides in place |
| `registerRawStreamingFile @ ...` | Internal: found the function it works through |
| `MH_EnableHook: MH_OK` | Internal: ready |
| `OVERRIDE-REG slot <- file` | Your file took over that item |
| `RECLAIM slot (old -> ours)` | The game tried to take an item back; the plugin re-took it |
| `REDIRECT name -> file` | A server file was swapped for yours |
| `PLACEMENT ...` | A tattoo position change was applied |
| `collection: name [tag]` | A collection the server uses, and whether it is reachable |
| `update check: ...` | Whether you have the newest version |
| `alive (beat N) ...` | Heartbeat; the plugin is still running |
| `pattern NOT FOUND` | The game updated; the plugin needs an update |

## How it works

For the technically curious, and for server owners deciding whether to allow it.

The plugin hooks one game function, `registerRawStreamingFile`, the same routine FiveM uses to
register loose and server-streamed files. The byte pattern that locates it comes from Cfx's open
source tree (`gta-streaming-five/src/Streaming.cpp`).

It hooks the game module (`GTA5.exe`) only, never FiveM's own DLLs. FiveM's `legitimacy`
anti-tamper terminates the process if you modify Cfx components; a hook in the game module is the
same surface trainers and `PackfileLimitAdjuster.asi` use, and it survives full connected
sessions.

Base freemode clothing lives inside `x64v.rpf` and never passes through that function, so waiting
to intercept it would wait forever. Instead the plugin calls `registerRawStreamingFile` itself and
registers your loose file under the base slot name. That claim alone is not enough: a streaming
slot maps name → id → handle, and whoever writes the handle last owns the slot. Vanilla DLC mounts
re-point claimed slots when they load, and FiveM's loader overwrites handles of already-registered
slots directly, without calling the hooked function at all. So the plugin remembers the handle its
claim produced and re-asserts it once a second: if anything re-pointed the slot, it writes its own
handle back. Last writer wins, and the plugin is always the last writer. This is the same
handle-overwrite mechanism Cfx's own override path uses in `LoadStreamingFile.cpp`; the plugin
just repeats it. Streamed files that pass through the hook under a claimed name are also
redirected to the local file on an exact `collection/file` match.

Bare-name `.ytd` files at the root of `tex_overrides` are registered the same way, under the file
name alone. This is the same trust model as a server `stream/` folder: an exact-name match
replaces exactly that texture dictionary and nothing else.

Tattoo placement works on data, not code. The game parses each `overlays.xml` into its
`PedDecorationManager`. The plugin locates that manager with the pattern Cfx itself publishes
(`PatchTattooSort.cpp`) and rewrites the position floats of the presets you edited. It never
hardcodes struct offsets. Instead it fingerprints your file's preset name hashes and unedited
values against memory, and writes only after at least 70% of the presets match exactly. Applied
values are re-asserted once a second, like the handles.

The hook is installed without suspending any threads, and the timing is what makes that safe:
FiveM loads `.asi` plugins in `LauncherInterface::PostLoadGame`, before the game's entry point has
ever run, so no thread can be executing game code during the patch. FiveM applies its own startup
patches in the same window for the same reason. MinHook's usual thread-freeze step cannot work
under FiveM anyway, since `CreateToolhelp32Snapshot` is blocked; the vendored copy is patched to
skip it, which is commented in `minhook/src/hook.c`.

The path handed to the game is a plain absolute path, which FiveM's VFS opens without complaint.
The game reads the whole resource from your file (header, page flags, data), so there is no size
or flag mismatch to manage.

The plugin makes exactly one network request: at startup it asks GitHub for the newest release
number (see [Update check](#update-check)). Nothing else is transmitted anywhere, and nothing is
ever sent to the game server. The plugin reads a local folder and changes what your client draws.
Other players keep seeing whatever the server streams.

## Why FiveM allows this

The plugin loads because FiveM's own loader is built to load third-party ASIs, not to block them.
Four things from Cfx's own source and docs, strongest first:

- **The `FX_ASI_BUILD` stamp is Cfx's API, not a workaround.** The loader looks up an
  `FX_ASI_BUILD` resource for the running game build, and when a plugin has none it tells you to
  add `FX_ASI_BUILD <build> BEGIN "\0" END` to the `.rc` file when building the plugin, or to
  contact its maintainer if you do not have the source. That is Cfx documenting how to ship a
  supported ASI. You do not build a versioning contract for software you want gone.
  ([asi-five Component.cpp](https://github.com/citizenfx/fivem/blob/master/code/components/asi-five/src/Component.cpp))
- **The loader is deny-by-exception.** It loads every `.asi` in the plugins folder except a short
  hardcoded blacklist (`openiv.asi`, `scripthookvdotnet.asi`, `fspeedometerv.asi`), an outdated
  `Gears.asi`, and .NET/CLR assemblies. Everything not named loads. An allowlist would be the
  design if the intent were to restrict.
  ([asi-five Component.cpp](https://github.com/citizenfx/fivem/blob/master/code/components/asi-five/src/Component.cpp))
- **The docs say so.** The client manual states FiveM "allows the use of certain plugins," placed
  in the plugins folder, where you can put "many types of .asi scripts you would typically use in
  singleplayer," and that servers "have the option to disallow the use of plugins."
  ([Client Manual](https://docs.fivem.net/docs/client-manual/))
- **Pure mode is opt-in.** The server-commands reference documents two pure mode levels, 1 and 2.
  There is no level 0 because level 0 is just a server that has not turned pure mode on, which is
  the default.
  ([Server Commands](https://docs.fivem.net/docs/server-manual/server-commands/))

All four settle one question: whether a plugin is allowed to load. None of them say anything about
what a plugin does in memory once loaded. That is a separate question, covered honestly in
[Ban risk, stated plainly](#ban-risk-stated-plainly) below.

## Ban risk, stated plainly

The total write to game code is one inline hook of about five bytes on a cosmetic asset-routing
function, plus MinHook's trampoline page. Beyond that the plugin writes data, not code: the handle
words of its own claimed slots in the streaming info table (the same words Cfx's loader writes
when a server overrides a file), and the position floats of tattoo presets the user edited.
Nothing else is touched. The plugin never reads or writes health, money, weapons, position, entity
pools, network events or player state, so there is no gameplay advantage in it and nothing that
changes what other players see.

The residual risk is real and worth stating: a generic code-integrity scan can flag the patch
regardless of intent, and Cfx's tolerance of game-module hooks is practice, not a written
guarantee. It has run full connected sessions without a ban. Keep it to servers that opt in
(`sv_pureLevel 0` is the owner's own setting) and do not spread builds around.

## Build

You need Visual Studio Build Tools 2022 with the "Desktop development with C++" workload. Then:

```
build.bat
```

If the batch file's vcvars auto-detect fails on your machine, run the same thing from an "x64
Native Tools Command Prompt for VS 2022"; the batch skips detection when the environment is
already set up.

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
- Needs a rebuild whenever FiveM bumps the game build (see the stamp above). Major game updates
  can also shift the byte patterns.
- Exact matching means you need the right collection name. Servers that re-stream clothing under
  their own custom DLC collections may not use the base collection for a given menu item. Trust
  the log over the base name.
- A reclaim changes what loads next, not what is already on screen. If an item was visible at the
  moment its slot was taken back (a server re-streamed it mid-session), take it off and put it
  back on once.
- Placement `.xml` files need at least 3 presets, and most of them must be unedited, or the safety
  check cannot verify the file and skips it.
- Client-side only. Other players and the server see no difference.

## Files

```
dllmain.cpp             the plugin: folder scan, hook, overrides, tattoo placement
build.bat               MSVC build
texoverride.rc          FX_ASI_BUILD stamp
minhook/                vendored MinHook with the Freeze() patch
COLLECTIONS.md          all 186 valid collection folder names
docs/overlay_index.tsv  every vanilla tattoo and overlay: name, file, position, texture
CHANGELOG.md            what changed in each version
```

MIT licensed. MinHook is copyright Tsuda Kageyu, BSD-2-Clause; the Hacker Disassembler Engine
inside it is copyright Vyacheslav Patkov.
