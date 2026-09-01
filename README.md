# texoverride

texoverride changes how clothes, tattoos, weapons and other textures look in GTA V on FiveM. Only you see
the change. You put files in one folder, and the game shows your versions instead of the
originals. It never edits the game's own files, and it never sends anything to the server.

Why this exists: FiveM's built-in ways of loading client mods cannot replace character clothing
textures. This plugin does it the same way servers do when they add their own clothes. It just
does it on your computer.

**Start here:** [Install](#install) &middot; [What you can replace](#what-you-can-replace) &middot; [Settings](#settings) &middot; [Expect bugs](#expect-bugs)

**Something is wrong:** [It will not load](#it-says-couldnt-load-texoverrideasi) &middot; [Textures gone or stuck blurry](#textures-gone-everything-stuck-on-low-detail) &middot; [Your antivirus flagged it](#why-your-antivirus-may-call-it-a-trojan) &middot; [Reading the log](#reading-the-log)

**While you play:** [Changing files as the game runs](#changing-files-while-the-game-runs) &middot; [Keeping FiveM's text out of screenshots](#keeping-fivems-corner-text-out-of-screenshots) &middot; [Update check](#update-check) &middot; [Turning it off](#turning-it-off)

**About:** [How it works](#how-it-works) &middot; [Ban risk](#ban-risk-stated-plainly) &middot; [Limitations](#limitations) &middot; [Build](#build) &middot; [Credits](#credits) &middot; [Files](#files)

**Longer guides, in [docs/](docs/):** [replacing files, step by step](docs/replacing-files.md) &middot; [reading the log](docs/reading-the-log.md) &middot; [textures and the memory budget](docs/textures-and-budget.md) &middot; [how it works](docs/how-it-works.md) &middot; [antivirus](docs/antivirus.md) &middot; [building it yourself](docs/build.md)

## Expect bugs

This is a working proof of concept. It has worked in real play sessions, and it will still break
in ways nobody has hit yet. When it breaks, the log file is built to tell us why.

If something goes wrong, [open a bug report](../../issues/new/choose). The form asks for two
things: what you expected to happen, and the contents of `plugins/texoverride.log`. That is
usually enough to fix it.

The log is safe to paste publicly. It never contains your Windows user name or anything else about
your computer.

## Install

1. Download `texoverride.zip` from [Releases](../../releases), or build it yourself (see
   [Build](#build)).
2. Open File Explorer, paste `%LOCALAPPDATA%\FiveM\FiveM.app\plugins` into the address bar, and
   press Enter.
3. Unzip the download into that folder. You get `texoverride.asi` and a `tex_overrides` folder
   that already has a folder made for every collection you can use, so you never have to guess a
   name or spell one. Empty folders you do not use cost nothing, so leave them or delete them.
4. Start FiveM.

Upgrading later? Download `texoverride.asi` on its own instead and replace just that one file. The
zip would not delete anything you put in `tex_overrides`, but there is no reason to unpack 194
folders a second time.

The plugin only works on servers that allow plugins. Some servers block them with a setting called
"pure mode". On those servers the plugin does nothing at all.

## It says "Couldn't load texoverride.asi"

That message means Windows refused the file before a single line of the plugin ran, so there is no
`texoverride.log` to look at. Your game build is not the cause. Work down this list.

1. **Check the file size.** Right click `texoverride.asi`, open Properties, and compare it with the
   size printed in the release notes. If it does not match, the download is damaged and a fresh one
   fixes it.
2. **Check Smart App Control.** Windows Security, then App and browser control, then Smart App
   Control. It blocks unsigned files with no warning at all, it is separate from the antivirus,
   and antivirus exclusions do not apply to it. This has been the answer before. Be aware it can
   only be switched off, never back on without reinstalling Windows.
3. **Check for antivirus other than Windows Defender.** McAfee, Norton, Avast, AVG and Kaspersky
   all block unsigned files from loading into a game, silently, with nothing shown anywhere.
   McAfee has been the answer before too, and a trial version that came with the PC counts. Add
   `texoverride.asi` to its exclusions, or remove it, then restart FiveM.
4. **Test the file on its own.** Paste this into PowerShell:

   ```powershell
   Add-Type -Name W -Namespace N -MemberDefinition '[DllImport("kernel32", SetLastError=true)] public static extern IntPtr LoadLibrary(string p);'
   $p = "$env:LOCALAPPDATA\FiveM\FiveM.app\plugins\texoverride.asi"
   [N.W]::LoadLibrary($p)
   [Runtime.InteropServices.Marshal]::GetLastWin32Error()
   ```

   A first number that is not zero means the file loads fine on its own and something only blocks
   it inside the game, which points back at steps 2 and 3. A first number of zero means the second number
   is the Windows error code, and that names the cause.

Still stuck after all four? [Open a report](../../issues/new/choose), pick "Plugin will not load",
and bring your answers.

## What you can replace

Everything goes in the `tex_overrides` folder. Most files go straight in under their own name.
Clothes are the exception: they go in a subfolder named after the character they belong to.

You can put your own folders around all of that to keep packs apart. `clothingpack1/mp_f_freemode_01_female_heist/uppr_013_r.ydd`
loads the same as `mp_f_freemode_01_female_heist/uppr_013_r.ydd`; only the folder the file sits
in directly has to be the collection name. Weapons, props, animations and tattoo textures can go
in folders too. If two packs contain the same file, the first one found is used and the log says
`DUPLICATE` for the other.

| What | Where it goes | File types |
|------|---------------|------------|
| Clothes | a subfolder, such as `mp_m_freemode_01/` | `.ydd` `.ytd` |
| Animals | a subfolder, such as `a_c_husky/` | `.ydd` `.ytd` `.ymt` |
| Tattoos, skin, face paint, beards | straight in | `.ytd` |
| Where a tattoo sits, and how big it is | straight in | `.xml` |
| Animations | straight in | `.ycd` |
| Firearms | straight in | `.ydr` `.ytd` |
| Props | straight in | `.ydr` `.yft` `.ytd` |
| Vehicles | straight in | `.yft` `.ydr` `.ytd` |

**[Step by step for each of these, with examples](docs/replacing-files.md)**

## Changing files while the game runs

You do not have to restart FiveM after every change. The plugin watches the `tex_overrides`
folder while you play and reacts on its own when something in it changes.

- Save an edited `overlays.xml` and the tattoo moves on your ped within a second or two. This
  makes tuning easy: nudge a number, save, look, repeat.
- Overwrite a `.ytd` or `.ydd` the plugin already uses and the file is read again straight away.
  Whether you SEE it without restarting depends on the game, not on the plugin. If the game still
  has the old version loaded in memory, it keeps drawing that, and taking the item off and putting
  it back on does not always force a fresh read. When that happens, restart FiveM. Editing a file
  the game has not loaded yet is the case that works reliably.
- Drop in a file with a name nothing else uses and it is picked up right away.
- Do not want to wait? Press **F11** in game and the folder is read again straight away. It only
  works while the game window is focused, and it always writes a line in the log, even when
  nothing has changed, so a key that finds nothing never looks like a key that is broken. To use
  a different key, set `refresh_key` in `_settings.txt` to any `f1` to `f12` key, a letter, a
  digit, or `off`.

The one thing that cannot happen live is taking over a name the server or a DLC has already
loaded. Once the game holds a name it will not hand it over until it restarts, so the log says so
and asks you to restart. On the next launch the plugin claims the name early, before the server
mounts, and from then on editing that file applies live like everything else.

There is also a safety net. If the game crashes right after a live change, the plugin remembers
which files were involved. On the next launch it refuses to load them, and the log tells you.
That way one broken file cannot crash the game again and again. When you have fixed or replaced
the file, delete `_quarantine.txt` from `tex_overrides` and it loads normally again.

## Keeping FiveM's corner text out of screenshots

FiveM writes its version in one corner of the screen and "N mod packs loaded" in the other, and
both show up in every screenshot. To keep them out, open `_settings.txt` and list the keys you
take screenshots with:

```
hide_overlay = printscreen, f9
```

Press one of those keys and the two lines come off the screen for about a second while the
picture is taken, then come back. Nothing else on screen changes. The server's chat and HUD stay
exactly where they are. You can list `printscreen`, any of `f1` to `f12`, a letter, or a digit,
separated by commas. If you had a `_settings.txt` before this version, add the line yourself; the
plugin never rewrites that file.

One limit: the text comes off on the next frame, so the screenshot tool has to grab the screen a
moment after the keypress. ShareX, Steam, Medal and the like all do, and come out clean. Windows'
own PrintScreen (copy to clipboard) grabs the screen in the same instant as the key and may still
show the text.

To keep the text off the whole time you play, use `hide_overlay = always` instead of a key
list. Either way it is only your own screen: the server and other players see nothing different.

## Update check

At startup the plugin asks GitHub one question: what is the newest release number? If a newer
version is out, a small popup tells you and asks if you want it downloaded and installed for you.
Yes downloads the new `.asi` from the GitHub release, checks that its SHA-256 matches the one
printed in the release notes, and puts it in place. It takes effect on your next FiveM restart. No
opens the releases page in your browser instead, and Cancel skips it. If a download does not match
the hash, nothing is installed and the log says so.

The version you had stays beside the new one as `texoverride.asi.old`. If the new version gives
you trouble, delete `texoverride.asi` and rename the `.old` file back to `texoverride.asi`.

To make updates install automatically without asking, set `auto_update = yes` in
`_settings.txt`. To turn the check off completely, set `no_update_check = yes`. See Settings
below.

That is the plugin's only network use. It sends nothing about you, your game or your files, and if you are
offline it quietly does nothing.

One honest limit: when FiveM moves to a new game build, old plugin versions stop loading at all
(see [The build stamp](docs/build.md#the-build-stamp)). A plugin that does not load cannot show a popup, so
after a big game update, check the releases page yourself.

## Turning it off

Set `off = yes` in `_settings.txt` and restart FiveM. The plugin stays installed but returns
before it creates logs, events, hooks or worker threads. It does not rotate `texoverride.log` or
run the update check. That makes it a clean A/B control: the ASI still passes FiveM's loader
check, but none of texoverride's runtime machinery starts.

## Settings

The first time it runs, the plugin writes `_settings.txt` into your `tex_overrides` folder. Open
it in Notepad. Every option is listed, switched off, and explained where it sits. Change a word,
save, restart FiveM.

```
# Write extra detail into texoverride.log.
# Turn this on when someone is helping you work out a problem, and turn it off
# again afterwards. It makes the log a lot longer.
debug = no
```

That is the whole thing. `no` becomes `yes` and the option is on. Lines starting with `#` are
notes and the plugin skips them.

| Option | What it does |
|--------|--------------|
| `off` | Plugin stays installed but does nothing at all |
| `debug` | Adds `DEBUG` detail to the log |
| `texture_budget` | `auto`, `game`, or a number of GB |
| `auto_update` | Installs new versions without asking |
| `no_update_check` | Never checks whether a new version is out |
| `refresh_key` | Which key reads the folder again, `f1` to `f12`, a letter, a digit, or `off` |
| `hide_overlay` | Keys that take FiveM's corner text off the screen for a moment (`printscreen`, `f1` to `f12`, a letter, a digit), or `always` |

`yes`, `on`, `true` and `1` all mean on. Anything else means off. Capital letters do not matter.

The file is only ever created, never rewritten, so your changes and any notes you add to it
survive every update. Delete it and you get a fresh one with everything off.

If you used the older marker files (`_off`, `_debug`, `_budget.txt` and so on), you do not have
to do anything. The first time the plugin runs it copies each one into `_settings.txt`, deletes
it, and writes a line in the log saying what it moved. After that the settings file is the only
one there.

Two other files turn up in that folder on their own. The plugin writes those. Leave them alone,
apart from deleting `_quarantine.txt` when you want a quarantined file tried again.

| File | What it is |
|------|-----------|
| `_quarantine.txt` | Files refused after a crash; delete it to try them again |
| `_inflight.txt` | Scratch file used while registering; disappears on a clean exit |

## Textures gone, everything stuck on low detail

Walls go black, clothes stay blurry, and restarting fixes it for a while. That is the game running
out of texture memory. It is not your graphics card being too small: FiveM hands every machine the
same ceiling, so a 24 GB card and an 8 GB card get exactly the same one.

The plugin raises that ceiling for you by default, based on how much memory Windows reports your
card actually has free. You do not have to do anything. To pick the number yourself, or to leave
the game's own setting alone, use `texture_budget` in `_settings.txt`.

**[Why this happens, and what the numbers mean](docs/textures-and-budget.md)**

## Reading the log

Everything the plugin does is written to `plugins/texoverride.log`. It starts fresh on every
launch, and the previous session is kept beside it as `texoverride.log.old`, so a log survives a
crash. It never contains your Windows user name and is safe to paste publicly.

**[What every line in it means](docs/reading-the-log.md)**

## Why your antivirus may call it a trojan

It happens, and the honest answer is that the plugin does the things antivirus software watches
for. Not by accident and not hidden: it is what a game mod that changes what the game draws has to
do. It writes five bytes into the running game to redirect one function, keeps the original in
memory that is both writable and executable, scans the game's memory for byte patterns, and is an
unsigned file that almost nobody has run yet.

Names like `Wacatac`, `Injector` or `Trojan:Win32/Wacatac.B!ml` mean a heuristic fired, not that
something was found. The `!ml` on the end literally means a machine learning guess.

Every release is built by GitHub Actions from the source in this repository and signed with build
provenance, so you can ask for proof that the file you have came out of here:

```
gh attestation verify texoverride.asi --repo blancodagoat/texoverride
```

If someone hands you a `texoverride.asi` from anywhere else and that command fails, do not run it.
That is the check worth doing, because a tampered copy is the one real risk here.

**[The rest, including what to keep switched on in Windows Security](docs/antivirus.md)**

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

## How it works

FiveM's own client mod paths cannot deliver ped textures, so the plugin works one layer down. It
registers your files with the game's streaming layer, which is the same thing a server does with
its `stream/` folder, then re-asserts them once a second so nothing takes the slot back. It never
edits or re-encrypts the game's archives, sends nothing to the server, and other players see no
change.

**[The full explanation, and why FiveM allows this](docs/how-it-works.md)**

## Limitations

- Proof of concept. It works, and you should still keep an eye on the log.
- Needs a rebuild whenever FiveM bumps the game build (see [the build stamp](docs/build.md#the-build-stamp)). Major game updates
  can also shift the byte patterns.
- Exact matching means you need the right collection name. Servers that re-stream clothing under
  their own custom DLC collections may not use the base collection for a given menu item. Trust
  the log over the base name.
- A `.ymt` can be replaced only if the game does not already have one under that exact name. Every
  animal ships its own, so an animal mod's `.ymt` is refused: the call that would replace it takes
  the game down. The rest of that mod still loads, and only parts it ADDED on top of the original
  animal stay unpickable.
- A reclaim changes what loads next, not what is already on screen. If an item was visible at the
  moment its slot was taken back (a server re-streamed it mid-session), take it off and put it
  back on once.
- Placement `.xml` files need at least 3 presets, and most of them must be unedited, or the safety
  check cannot verify the file and skips it.
- Client-side only. Other players and the server see no difference.
- Animal mods that need a different skeleton cannot work. Only `.ytd`, `.ydd`, `.yft` and `.ymt`
  can be handed to the game this way, and the skeleton is in none of them.

## Build

`build.bat`, with the free Visual Studio Build Tools 2022. Builds are reproducible, so the file you
build yourself is byte for byte the file CI publishes.

**[Requirements, the build stamp and CI](docs/build.md)**

## Credits

Written by blancodagoat.

chocomintw contributes features, not only fixes. Replacing weapon models started as their
pull request in 0.8.8, and so did the log levels that turned the log into something you can
read instead of a wall of text. They also split the plugin out of one long file into the
`src/` tree it has now, and wrote the updater that offers you a new version and installs it
for you. Their issue #20 is why six scattered marker files became one settings file in
0.8.13.

chunguscodes forked the plugin and sends fixes as small, separate pull requests. Four of them
shipped in 0.8.6: the plugin now stops when its hook fails to install instead of carrying on and
crashing on the first file it touches, it no longer leaks thread handles, copying a large folder
into `tex_overrides` while the game runs no longer stalls it or drops a change, and builds are
reproducible with the build server building twice and comparing before it publishes anything.

## Files

```
src/                    modular C++ source (hook, streaming, budget, tattoo placement, live reload, gate)
dllmain.cpp             plugin entry point (DllMain)
build.bat               MSVC build
texoverride.rc          FX_ASI_BUILD stamp
minhook/                vendored MinHook with the Freeze() patch
COLLECTIONS.md          every valid collection folder name, characters and animals
tools/make-zip.ps1      packs the release zip, folder list read from COLLECTIONS.md
tools/gate_test.bat     runs the safety-gate cases against the real code in src/gate.h
docs/replacing-files.md       step by step for clothes, tattoos, animations, weapons, props, vehicles
docs/reading-the-log.md       what every line in texoverride.log means
docs/textures-and-budget.md   why textures go missing, and the memory ceiling
docs/how-it-works.md          the streaming layer, and why FiveM allows this
docs/antivirus.md             why scanners flag it, and how to check the file yourself
docs/build.md                 building it yourself, the build stamp, CI
docs/overlay_index.tsv        every vanilla tattoo and overlay: name, file, position, texture
docs/client-side-dlc-packs.md how to run a DLC pack client side on FiveM (not texoverride)
CHANGELOG.md            what changed in each version
```

MIT licensed. MinHook is copyright Tsuda Kageyu, BSD-2-Clause; the Hacker Disassembler Engine
inside it is copyright Vyacheslav Patkov.
