# Replacing files

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

The game calls each of these folders a *collection*. [COLLECTIONS.md](../COLLECTIONS.md) lists every
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

To find the name to use, open [docs/overlay_index.tsv](overlay_index.tsv) in a spreadsheet app
or a text editor and search for the tattoo. The `txd` column is the file name.

## Moving tattoos (position, size, rotation)

Where a tattoo sits on the body, and how big it is, is not in the picture. It is a set of numbers
in a game file called `overlays.xml`. The plugin can change those numbers for you.

This one is fiddly. You have to dig a file out of the game and edit it by hand.

**Step 1. Find the file that owns your tattoo.** Look the tattoo up in
[docs/overlay_index.tsv](overlay_index.tsv). The `source` column names the exact
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

## Replacing vehicles

A vehicle mod that REPLACES a car or bike the game already has works the same way as a prop. Each
vehicle is three or four files, all named after the vehicle, and all of them go straight into
`tex_overrides`, no folder:

```
tex_overrides/
  bagger.yft        <- the model
  bagger_hi.yft     <- the close-up model
  bagger.ytd        <- the paint and parts
  bagger+hi.ytd     <- close-up textures (not every mod has one)
```

Most vehicle mods come as an OpenIV package (a `.rpf` for the `mods` folder). Open it with
OpenIV, walk down to `dlc.rpf`, `x64`, `vehicles.rpf`, and drag the files out. The `data`
folder next to it (`handling.meta`, `carcols.meta` and friends) is not something the plugin can
use, so leave it. The model and textures are what you see; that is what gets replaced.

What does not work: a mod that ADDS a new vehicle with its own name. Adding a vehicle needs the
game to read `vehicles.meta` and `handling.meta`, which the plugin cannot do. For those, keep
using the `mods` folder package the mod came as.

[Back to the README](../README.md)
