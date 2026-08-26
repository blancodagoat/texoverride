# texoverride

texoverride changes how clothes, tattoos, weapons and other textures look in GTA V on FiveM. Only you see
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

## Replacing clothes

Clothes go in a folder. Which folder depends on where the item came from.

**Step 1. Open `tex_overrides` and find the folder for your character.**

```
mp_m_freemode_01      male character
mp_f_freemode_01      female character
```

Those two cover almost everything you will ever change. The download comes with a folder already
made for every name there is, so you never have to make one or guess a spelling.

If your mod came from a game update, its readme names a longer folder, something like
`mp_m_freemode_01_mp_m_gunrunning_01`. If it names one, use that folder instead of the short one.
If it names nothing, use the short one.

**Step 2. Grab the files out of the mod and drop them in that folder.**

```
tex_overrides/
  mp_m_freemode_01/
    uppr_012_r.ydd              <- the shape of the item
    uppr_diff_012_a_uni.ytd     <- the picture painted on it
```

A `.ydd` file is a 3D model. A `.ytd` file holds the textures, which are the images painted on the
model. Some mods only give you one of the two. That is fine, drop in what you have.

**Step 3. Start FiveM.** That is the whole job.

### Which item does it change?

The file name, and nothing else. `uppr_012_r.ydd` replaces whatever `uppr_012_r` already was. You
do not pick a slot or type a number anywhere. Leave the names exactly as the mod ships them.

The names are how the game labels body parts, not something the mod made up. `uppr` is a top,
`lowr` is trousers, `feet` is shoes, and so on.

### If you do not know which folder to use

The game calls each of these folders a *collection*. [COLLECTIONS.md](COLLECTIONS.md) lists every
name that came with the game, and `docs/ped_collections.tsv` has all 469 of them with file counts.

Easier still: start the game once and read the log. It lists every collection the server actually
uses and marks whether the plugin can reach it.

### Server characters and pets

Servers add their own characters and animals with names that are in neither list. Those work too.
Name the folder after the model and put the parts in it.

What the plugin checks is not the folder name but the files inside. They have to be named the way
GTA names body parts, like `head_000_r.ydd` or `uppr_diff_001_a_uni.ytd`. That is what stops a
vehicle texture or a map file being loaded onto somebody, which is the reason the rule exists.
Story characters are refused outright, as are maps. Props and weapons have their own place, see
below.

## Replacing animals

Eight animals are built like your own character, out of a folder of parts:

```
a_c_chop  a_c_husky  a_c_mtlion  a_c_panther
a_c_retriever  a_c_rottweiler  a_c_sharktiger  a_c_shepherd
```

**Step 1. Drop the animal's folder straight into `tex_overrides`.** Most animal mods are already
laid out the way the plugin wants:

```
tex_overrides\a_c_shepherd\head_000_r.ydd
tex_overrides\a_c_shepherd\head_diff_000_a_whi.ytd
tex_overrides\a_c_shepherd\uppr_000_u.ydd
```

**Step 2. Any loose files go in the top of `tex_overrides`, not in the animal's folder.**

```
tex_overrides\a_c_shepherd.yft
```

**Step 3. Start FiveM.**

Every other animal (pug, poodle, westy, cat, coyote, deer, cow, pig, rabbit, rat, and the birds
and fish) is one single model instead of a folder of parts. Those have no folder at all. Put
`a_c_<name>.ydd`, `.ytd`, `.yft` and `.ymt` straight into `tex_overrides`.

### About the `.ymt` file

Animal mods usually ship one, for example `a_c_shepherd.ymt`. **For those eight animals it is
turned away on purpose.** The game already owns that exact name, and the call that would replace
it crashes the game outright, so there is nothing to be done about it. Drop it in if you like. The
log says it was ignored and the rest of the mod still loads.

What you lose is only the parts the mod *added* on top of the original animal. Those stay
unselectable. Anything the mod replaced still shows up. For every other animal the `.ymt` works
normally, so leave it in.

### One thing to check first

A model built on a different skeleton than the original animal will not work, because the skeleton
lives in a part of the game files this plugin cannot reach. Retextures and remodels that keep the
original skeleton are fine, and that is what nearly every animal mod is.

## Replacing tattoos, skin and other overlays

Tattoos, skin textures, face paint and beards do not go in a folder at all.

**Step 1. Drop the file straight into `tex_overrides`, next to the folders.**

```
tex_overrides/
  mp_gr_tat_027_m.ytd          <- a tattoo
  mp_fm_skin_m_up_whi.ytd      <- a skin texture
```

These are always one single `.ytd` file. There is no `.ydd` for a tattoo, and a `.ydd` outside a
folder is never accepted.

**Step 2. Start FiveM.**

### Which tattoo does it change?

The file name is the whole match. Your file replaces the one texture with that exact name and
nothing else. Custom server tattoo packs work the same way, so any name is accepted here.

To find the name to use, open [docs/overlay_index.tsv](docs/overlay_index.tsv) in a spreadsheet app
or a text editor and search for the tattoo. The `txd` column is the file name.

## Moving tattoos (position, size, rotation)

Where a tattoo sits on the body, and how big it is, is not in the picture. It is a set of numbers
in a game file called `overlays.xml`. The plugin can change those numbers for you.

This one is fiddly. You have to dig a file out of the game and edit it by hand.

**Step 1. Find the file that owns your tattoo.** Look the tattoo up in
[docs/overlay_index.tsv](docs/overlay_index.tsv). The `source` column names the exact
`overlays.xml` inside the game files. The other columns show the tattoo's current position
(`uvX`, `uvY`), size (`scaleX`, `scaleY`) and rotation.

**Step 2. Copy that file out and edit it.** Use [OpenIV](https://openiv.com/) to open the path from
the `source` column and save the `.xml` to your computer. Open it in any text editor and find your
tattoo by name. `uvPos` is the position, `scale` is the size, `rotation` is the angle.

**Change only the tattoos you want moved. Leave the rest of the file alone.** That is not
politeness, it is required. Before changing anything in the running game the plugin checks the
entries you did not touch against the game, to be sure it has the right file. If nearly every entry
is changed there is nothing left to check against, and the file is skipped.

**Step 3. Put the edited `.xml` into `tex_overrides`**, next to your `.ytd` files, and start the
game. If the file does not line up, because it is the wrong one or the game has updated, nothing is
changed and the log says so.

### One file to leave alone

`shop_tattoo.meta` sits next to `overlays.xml` in the game files. It is the shop catalog (price,
menu label, unlock, and which shop slot points to which tattoo), not the tattoo's looks or
position. The plugin does not apply it, and if you drop one in anywhere the log says it was
ignored.

A useful thing to know: the game's own reader for that file has no field for `zone`, so a `<zone>`
line inside `shop_tattoo.meta` is thrown away on load. The zone that actually places a tattoo,
along with its size, angle and texture, lives in the `.ytd` and the `overlays.xml`. So when you
replace or move a tattoo, nothing in `shop_tattoo.meta` needs to change. If you keep it for your
own reference, put it in a folder named after its pack (`tex_overrides/mplowrider/shop_tattoo.meta`),
matching the game's own layout.

A note on why that file has no pack name in it: the game connects each `shop_tattoo.meta` to its
DLC through the DLC's own content list (`content.xml`), not through the file name. That is also the
answer for *adding* whole new tattoos, where a shop entry does matter: a new tattoo needs its
texture, overlay entry and shop entry loaded together as a pack. FiveM loads such packs client side
as mod packages in `FiveM.app\mods` (this is how server tattoo packs are built). texoverride stays
out of that; it replaces and moves what exists.

## Replacing animations

An animation lives in a `.ycd` file, which the game calls a clip dictionary. One file holds one or
more clips, and whatever plays an animation asks for it by dictionary name plus clip name. To
replace one, put your `.ycd` straight into `tex_overrides`, no folder:

```
  tex_overrides/
    gtawpl_1.ycd
```

**Read this part before you build anything, it decides whether your pack can work at all.**

The plugin can replace an animation the **server** streams. It cannot replace one that came with
GTA. Those are two different things and they look identical from the outside:

| where the animation comes from | can the plugin replace it |
|---|---|
| your server streams it (it appears in the log) | yes |
| it shipped with GTA | no |

The reason is how each one reaches the game. A server file is registered through the same call the
plugin listens on, so the plugin swaps the path as it goes past. A file that came with GTA never
makes that call, so there is nothing to swap. Clothes and textures are different, and the plugin
does reach the base game for those.

So the first thing to do is start the game once and read the log. Every dictionary the server
streams is listed:

```
[19:04:22] [INFO] [COLLECTION] Server file:       gtawpl_1.ycd              [overridable...]
[19:04:22] [INFO] [COLLECTION] Server file:       agangsign2@animation.ycd  [overridable...]
```

If the dictionary your animation uses is in that list, you can replace it. If it is not, the
animation came with GTA and this will not work no matter how the file is built.

Then get the clip name right. The file name has to be the dictionary name exactly, and the file has
to contain a clip called what is being asked for. Many servers publish a list of their animations
with the dictionary and clip for each one, and that is the easiest way to get both.

Two more things worth knowing:

- **Replacing a dictionary replaces all of it.** If the original held three clips and yours holds
  one, the other two are gone, and anything that played them stops working. Start from a copy of a
  dictionary that already has the right clips, swap the one you want changed, keep the rest.
- **Clip names are matched by number, not by spelling.** Tools sometimes show a clip under a label
  left over from whoever built it while the name the game actually uses is different. If a pack
  looks wrongly named and still works, that is why.

## Replacing firearms

A weapon mod usually comes with two files: a `.ydr` (the 3D model) and a `.ytd` (the textures).
Put both straight into `tex_overrides`, no folder:

```
tex_overrides/
  w_pi_pistol.ydr          <- weapon model
  w_pi_pistol.ytd          <- weapon textures
```

Every GTA V weapon is named `w_` and then the weapon, so that is the name to use.

Texture-only mods (`.ytd` only, no model change) have always worked. Nothing new is needed for
those.

Both kinds work: weapons your **server** streams (listed in the log as `Server file` lines) and
weapons that came with GTA. Models that came with GTA go through exactly the same slot claim as
props, and a vanilla prop model was confirmed showing in game on 2026-08-25.

If a weapon does not change, read `plugins/texoverride.log`. A line saying `OVERRIDE-REG` or
`REDIRECT` with your file name on it means the plugin claimed the slot. If neither line is there,
open an issue with the log attached.

## Replacing props

A prop is any object that is not a character or a car: a phone, a notepad, a cardboard box, a
police laptop, a sheet on a bed. Their model files are `.ydr` (a plain model) or `.yft` (a model
with physics, like a door that swings), and their textures are `.ytd`. All three go straight into
`tex_overrides`, no folder:

```
tex_overrides/
  prop_cs_hand_radio.ydr   <- model
  prop_cs_hand_radio.ytd   <- textures
  prop_flag_sheriff.yft    <- a model with physics
```

If you copy a prop pack in as a folder, the log says `SKIP` and `files go straight into
tex_overrides, not in a folder` for each one. Move the files up one level and restart.

The file name is the whole rule. The plugin takes any `.ydr` or `.yft` at the root and gives it to
the game under exactly that name, so it can only ever land on the object with that name. Props a
server adds show up in the log as `Server file` lines and can be replaced, the same as a server
weapon or animation. Props that came with GTA work too: `prop_beer_bottle.ydr` and
`prop_beer_logger.ydr` dropped at the root showed the new models in game (2026-08-25).

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

If it still happens with a light pack, it is the server, not you. You do not need to touch the
Extended Texture Budget slider for it: the plugin already raises that ceiling for you at startup,
and puts it back every time the settings screen overwrites it. Lowering Texture Quality one step
still helps.

### The budget, and why a good graphics card does not save you

The Extended Texture Budget slider does not set a size. It multiplies a fixed base of about 2.9 GB,
and at its maximum setting it lands at about 7.8 GB. Those are the only two numbers that matter,
and your graphics card is in neither of them. A 24 GB card gets the same 7.8 GB ceiling as an 8 GB
card, which is why this bug shows up on expensive builds too and why maxing the slider is often not
enough on its own.

The plugin raises the ceiling for you, so the slider is not something you have to think about. It
is still worth maxing on a smaller card, because the plugin only ever raises and never lowers: on
an 8 GB card a maxed slider lands at 7.8 GB, which is higher than the 6 GB the plugin would pick,
so the plugin leaves the bigger number alone. On a big card the plugin wins by miles either way.

On startup it asks Windows how much video memory it is willing to give the game right now, holds
back an eighth of that (or 2 GB, whichever is more) for the parts of the game that are not
textures, and raises the ceiling to whatever is left. The log line looks like this:

```
budget: sized to this PC - 18.0 GB, up from the 7.8 GB the game set
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
The plugin stays installed but returns before it creates logs, events, hooks or worker threads. It
does not rotate `texoverride.log` or run the update check. That makes `_OFF` a clean A/B control:
the ASI still passes FiveM's loader check, but none of texoverride's runtime machinery starts.

## Reading the log

Everything the plugin does is written to `plugins/texoverride.log`. The file starts fresh on every
launch, and the previous session's log is kept next to it as `texoverride.log.old`, so if the game
crashed, the log from the crashed session is still there.

Every line has the same shape:

```
[19:04:22] [INFO] [CLAIM] REDIRECT mp_m_freemode_01/uppr_012_r.ydd -> tex_overrides/...
```

The level is `INFO`, `WARN` or `ERROR`. If something did not work, search the file for `WARN` and
`ERROR` first, because those two carry the reason. The category says which part of the plugin
spoke: `CORE`, `SCAN`, `COLLECTION`, `AUDIT`, `CLAIM`, `VERIFY`, `LIVE`, `TATTOO` or `UPDATE`.

There is a fourth level, `DEBUG`, which is off unless you make an empty file called `_debug.txt`
inside `tex_overrides`. It adds internal detail that is only useful when someone is helping you
work out a problem.

| Line | What it means |
|---|---|
| `texoverride x.y.z ...` | The plugin is in and running |
| `Loaded N override(s) in Ns` | Your files were found |
| indented `Collections` and `Root Assets` lines | How your files were grouped |
| `Pack cost when fully loaded: ...` | What your files cost the game in memory |
| `HEAVY x MB file` | That file is oversized; shrink it to avoid texture loss |
| `HUGE file - x MB` | Over 32 MB; it is loaded, but shrink it first if you start crashing |
| `UNREADABLE file` | The file could not be opened, so it was not loaded |
| `SKIP file` | The name does not fit any rule; the reason is on the line |
| `IGNORED file` | Not a type the game can be handed this way; the reason is on the line |
| `CRASH SAVER: ...` | Last run died on a file; it is skipped this launch so you can get in |
| `QUARANTINED file` | Skipped after a crash; delete `_quarantine.txt` to try it again |
| `Texture budget: Sized to this PC ...` | The texture budget was raised to fit your card |
| `Texture budget: a -> b GB` | The raise was written into the game |
| `Loaded placement file: ...` | Your edited `.xml` was read |
| `... layout solved` | The `.xml` matched the game; changes can be applied |
| `Streaming manager @ ...` | Internal: found what it needs to keep overrides in place |
| `registerRawStreamingFile @ ...` | Internal: found the function it works through |
| `MH_EnableHook: MH_OK` | Internal: ready |
| `OVERRIDE-REG: slot <- file` | Your file took over that item |
| `OVERRIDE-TAKEOVER: slot <- file` | The slot already existed, so its handle was replaced |
| `OVERRIDE-WAIT: slot <- file` | The file is ready and will bind when its target slot appears |
| `OVERRIDE-FAILED: slot <- file` | Registration failed and produced no usable entry |
| `LATE-BIND: slot ...` | A previously missing target appeared and was attached |
| `RECLAIM: slot (old -> ours)` | The game tried to take an item back; the plugin re-took it |
| `REDIRECT name -> file` | A server file was swapped for yours |
| `PLACEMENT: ...` | A tattoo position change was applied |
| `LIVE-ADD` / `LIVE-TAKEOVER` / `LIVE-UPDATE` | A file you changed while playing was picked up |
| `Server collection: name kind [tag]` | A collection the server uses, what it is, and whether it is reachable |
| `Server file: name [overridable...]` | A loose file the server streams that you can replace |
| `Other server files ... counted, not listed` | How many streamed files the plugin can never touch |
| `Update available` / `Plugin is up to date` | Whether you have the newest version |
| `Heartbeat (beat N) ...` | The plugin is still running |
| `pattern NOT FOUND` | The game updated; the plugin needs an update |

The three tags on a `Server collection` line mean:

- `overridable`, make a folder with that name and your files will be used.
- `depends on the file names inside`, the collection itself is fine, but each file still has to be
  named the way GTA names ped parts.
- `OTHER - never touched`, a story or ambient character. The plugin refuses these on purpose.

Collections are always listed, refused ones included, because that list is how new character names
get found. Loose files are treated differently: the ones you can replace are listed, and everything
else the server streams (car parts, props, map pieces, often tens of thousands of files) is only
counted. Those names cannot ever be used, so listing them buried the useful lines and slowed the
game down while it wrote them. Put a file called `_debug.txt` in `tex_overrides` if you want the
full list back.

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
registers your loose file under the base slot name. If the game rejects that call because the slot
already owns a handle, the plugin locates the slot through its actual streaming module and attaches
the local pgRawStreamer handle directly, matching FiveM's occupied-slot mechanism. It does not use
FiveM's diagnostic name map because that map omits base-RPF names.

That claim alone is not enough: a streaming
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

**If FiveM shows "Couldn't load texoverride.asi" and there is no `texoverride.log` next to the
file**, an antivirus stopped the file from loading and did not tell you. McAfee does this. Nothing
shows up in Windows Security, the file looks fine, and loading it from anywhere else works. Add
`texoverride.asi` to McAfee's Real-Time Scanning exclusions, or remove McAfee (Windows Defender
takes over on its own) and restart FiveM.

Windows Security settings and the plugin, if you want to check yours:

- Keep on: real-time protection, cloud-delivered protection, firewall, reputation-based
  protection (PUA blocking and SmartScreen), core isolation. None of these stop the plugin. At
  worst one flags a fresh download; allow it in Protection history.
- Must stay off: Smart App Control. It blocks every unsigned file, this one included. Once it is
  off it cannot be turned back on without reinstalling Windows.
- Leave at defaults: Exploit protection program settings (an entry for FiveM with Code integrity
  guard or Arbitrary code guard would block the plugin) and Controlled folder access (if it is on
  and the log never appears, allow FiveM there).
- Do not run a second antivirus next to Defender. McAfee, Norton, Avast, AVG and Kaspersky trials
  switch Defender off and can veto the plugin loading with no notice, which is the case above.

To find out what is blocking it when nothing admits to it, use Process Monitor:

1. Download Procmon from Microsoft (search "Sysinternals Process Monitor") and run it as
   administrator. Close FiveM first.
2. In Procmon press Ctrl+L, add the filter `Path` `contains` `texoverride.asi`, click Add, OK.
3. Launch FiveM and wait for the "Couldn't load" dialog. Go back to Procmon.
4. Read the Result column. `SUCCESS` all the way down and still no log means a driver vetoed the
   load; look at the Process Name column for anything that is not FiveM (an antivirus service
   touching the file right before the failure is the answer). `ACCESS DENIED` names the blocker
   directly. `NAME NOT FOUND` means FiveM is looking in a different plugins folder than the one
   you put the file in.
5. To keep the evidence: File > Save, PML format, and attach it to a GitHub issue.

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

## Credits

Written by blancodagoat.

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
docs/overlay_index.tsv  every vanilla tattoo and overlay: name, file, position, texture
docs/client-side-dlc-packs.md  how to run a DLC pack client side on FiveM (not texoverride)
CHANGELOG.md            what changed in each version
```

MIT licensed. MinHook is copyright Tsuda Kageyu, BSD-2-Clause; the Hacker Disassembler Engine
inside it is copyright Vyacheslav Patkov.
