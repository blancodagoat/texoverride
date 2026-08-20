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
It is the shop catalog (price, menu label, unlock, and which shop slot points to which tattoo),
not the tattoo's looks or position. A useful thing to know: the game's own reader for that file
has no field for `zone`, so a `<zone>` line inside `shop_tattoo.meta` is thrown away on load. The
zone that actually places a tattoo, along with its size, angle and texture, lives in the `.ytd`
and the `overlays.xml`. So when you replace or move a tattoo, nothing in `shop_tattoo.meta` needs
to change, and the plugin does not apply it; if you drop one in anywhere, the log says it was
ignored. If you keep it for your own reference, put it in a folder named after its pack
(`tex_overrides/mplowrider/shop_tattoo.meta`), matching the game's own layout.

A note on why that file has no pack name in it: the game connects each `shop_tattoo.meta` to its
DLC through the DLC's own content list (`content.xml`), not through the file name. That is also
the answer for *adding* whole new tattoos, where a shop entry does matter: a new tattoo needs its
texture, overlay entry and shop entry loaded together as a pack. FiveM loads such packs client
side as mod packages in `FiveM.app\mods` (this is how server tattoo packs are built). texoverride
stays out of that; it replaces and moves what exists.

The plugin is careful with these files. Before changing anything in the running game, it checks
that the entries you did not touch still match the game exactly. If they do not line up, because
it is the wrong file or the game has updated, it changes nothing and says so in the log. This
check is also why you should leave most of the file unedited. If nearly every entry is changed,
there is nothing left to check against, and the plugin skips the file.

## Changing files while the game runs

You do not have to restart FiveM after every change. The plugin watches the `tex_overrides`
folder while you play and reacts on its own when something in it changes.

- Save an edited `overlays.xml` and the tattoo moves on your ped within a second or two. This
  makes tuning easy: nudge a number, save, look, repeat.
- Overwrite a `.ytd` or `.ydd` the plugin already uses and the new picture shows the next time
  the game reloads that item. Take the clothing or tattoo off and put it back on to force that.
  This is the one you want while you are working on a texture, and it always works.
- Drop in a file with a name nothing else uses and it is picked up right away.

The one thing that cannot happen live is taking over a name the server or a DLC has already
loaded. Once the game holds a name it will not hand it over until it restarts, so the log says so
and asks you to restart. On the next launch the plugin claims the name early, before the server
mounts, and from then on editing that file applies live like everything else.

There is also a safety net. If the game crashes right after a live change, the plugin remembers
which files were involved. On the next launch it refuses to load them, and the log tells you.
That way one broken file cannot crash the game again and again. When you have fixed or replaced
the file, delete `_quarantine.txt` from `tex_overrides` and it loads normally again.

## Update check

At startup the plugin asks GitHub one question: what is the newest release number? If a newer
version is out, a small popup tells you and asks if you want the download page opened. Click
Yes and it opens in your browser. That is the plugin's
only network use. It sends nothing about you, your game or your files, and if you are offline it
quietly does nothing.

To turn the check off, create an empty file named `_NO_UPDATE_CHECK` inside `tex_overrides`.

One honest limit: when FiveM moves to a new game build, old plugin versions stop loading at all
(see [The build stamp](#the-build-stamp)). A plugin that does not load cannot show a popup, so
after a big game update, check the releases page yourself.

## Textures gone, everything stuck on low detail

On busy servers GTA sometimes gets stuck like this: buildings turn into grey blobs, textures
vanish, and only a game restart fixes it. That happens when the game's texture memory runs out.
The game never frees memory ahead of time, so once the budget is full it stays full. Heavy
servers can hit this on their own, with no mods at all.

Big override files make it worse. A texture saved at 4K, or saved without compression, can cost
the game 20 to 90 MB where the original cost 1 MB. A few of those on screen and the budget dies.

The plugin now measures this for you. At startup the log prints a line like
`pack cost when fully loaded: 240.0 MB of texture memory`, and below it a `HEAVY` line for every
file that costs 8 MB or more. Those files are the ones to fix: open them in a tool like
OpenIV or CodeWalker, resize the textures to what the original used (clothing is usually
512 to 1024 pixels), and save them DXT compressed. Smaller files look nearly identical on a
character and leave the rest of the game room to breathe.

If it still happens with a light pack, it is the server, not you. Set Extended Texture Budget to
the highest value your video card allows (Settings, Graphics) and lower Texture Quality one step.

### The budget, and why a good graphics card does not save you

The Extended Texture Budget slider does not set a size. It multiplies a fixed 3 GB base, and even
at maximum it lands around 6 GB, the same on every card. There is no video memory anywhere in that
sum. A 24 GB card gets the same ceiling as a 4 GB card, which is why this bug shows up on expensive
builds too and why maxing the slider often is not enough.

The plugin fixes that for you. On startup it asks Windows how much video memory it is willing to
give the game right now, holds back a quarter of that (or 1.5 GB, whichever is more) for the parts
of the game that are not textures, and raises the ceiling to whatever is left. You do not have to
set anything. The log line looks like this:

```
budget: sized to this PC - 17.0 GB, up from the 2.8 GB the game gives every machine
        (card 24.0 GB, Windows is offering this process 23.2 GB right now)
```

If your card has nothing to spare, the plugin says so and leaves the budget alone rather than
pushing past what the card holds, which would make the game stutter instead of helping.

To pick the number yourself, put a file named `_budget.txt` into `tex_overrides` containing just a
number of GB, for example:

```
8
```

Put a `0` in that file instead to switch the whole thing off and leave the game's budget exactly as
it was. Either way, restart FiveM after changing it.

A bigger ceiling buys headroom before the bug hits. It does not remove the bug, which lives inside
GTA itself, and it cannot make a pack fit that is simply too big. Shrinking the files in the
`HEAVY` list is still the fix that always works.

## Turning it off

Create an empty file named `_OFF` (no file extension) inside `tex_overrides` and restart FiveM.
The plugin stays installed but does nothing, including the update check.

## Reading the log

Everything the plugin does is written to `plugins/texoverride.log`. The file starts fresh on every
launch, and the previous session's log is kept next to it as `texoverride.log.old`, so if the game
crashed, the log from the crashed session is still there.

| Line | What it means |
|---|---|
| `texoverride x.y.z loaded (date)` | The plugin is in and running |
| `loaded N override(s)` | Your files were found |
| indented `collection N file(s)` lines | How your files were grouped |
| `pack cost when fully loaded: ...` | What your files cost the game in memory |
| `HEAVY x MB file` | That file is oversized; shrink it to avoid texture loss |
| `TOO BIG file — x MB` | Over 32 MB; not loaded because files that big crash the game |
| `budget: sized to this PC ...` | The texture budget was raised to fit your card |
| `texture budget: a -> b GB` | The raise was written into the game |
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

## Why your antivirus may call it a trojan

It happens, and the honest answer is that the plugin does the things antivirus software watches
for. Not by accident, and not hidden: it is what a game mod that changes what the game draws has
to do.

- It writes five bytes into the running game to redirect one function. That is the same technique
  every trainer, overlay and mod loader uses, and scanners class it as code injection.
- It allocates a small piece of memory that is both writable and executable, to hold the original
  copy of that function. Generic detections weigh this heavily on its own.
- It scans the game's memory for byte patterns to find the functions it needs.
- It is an unsigned file, loaded into another program, that almost nobody has run yet. Microsoft
  Defender scores new unsigned files partly on how many people have seen them, so a fresh release
  starts with a bad score no matter what is in it.

Names like `Wacatac`, `Injector`, `HackTool` or `Trojan:Win32/Wacatac.B!ml` mean a heuristic fired,
not that something was found. The `!ml` on the end literally means a machine learning guess.

What you can do:

- Check it yourself. Upload the file to [VirusTotal](https://www.virustotal.com). A handful of
  engines flagging it while the majority do not is what a false positive looks like.
- Compare the file. Every release is built by GitHub Actions from the source in this repository,
  and the release notes list the SHA-256 of the file so you can check the one you downloaded is
  the one that was built. You do not have to take that on trust either. Each release is signed
  with build provenance, so with [GitHub CLI](https://cli.github.com) installed you can ask for
  proof that this exact file came out of this repository:

  ```
  gh attestation verify texoverride.asi --repo blancodagoat/texoverride
  ```

  If someone hands you a `texoverride.asi` from anywhere else and that command fails, do not run
  it. That is the check worth doing, because a tampered copy is the one real risk here.
- Build it yourself. `build.bat` needs only the free Visual Studio Build Tools. Then the file on
  your disk is one you made.
- Report it. If Defender flagged it, submitting it at
  [Microsoft's false positive form](https://www.microsoft.com/en-us/wdsi/filesubmission) usually
  gets it cleared within a few days, for everyone.
- Add an exclusion for your FiveM `plugins` folder, if you are comfortable doing that and you
  trust where you got the file.

What this project will not do is obfuscate, pack, or otherwise dress the file up to slip past
scanners. That is what actual malware does, it makes detections worse rather than better, and it
would destroy the one thing that makes a mod like this trustworthy: that you can read every line
of what it does.

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
