THIS FOLDER - what goes where

This folder is where your own files go. You grab the files out of the mod you downloaded and
you place them in here. Nothing else. There is no menu, no list to edit, no numbers to type.

The file name decides what it changes. A file called uppr_012_r.ydd replaces whatever
uppr_012_r already was. That is the whole system. Leave the names exactly as the mod ships
them and it works out on its own.

Start FiveM after you drop files in. It reads the folder as it starts up.


===============================================================================
CLOTHES AND BODY PARTS
===============================================================================

Clothes go in a folder. Which folder depends on where the item came from.

Look at the folders in here and open one of these two:

   mp_m_freemode_01      male character
   mp_f_freemode_01      female character

That covers almost everything you will ever change.

Now grab the files out of the mod. They look like this:

   uppr_012_r.ydd              the shape of the item
   uppr_diff_012_a_uni.ytd     the picture painted on it

Drop both into the folder. Done.

Some mods only give you one of the two. That is fine, drop in what you have.

IF THE MOD CAME FROM A GAME UPDATE
Its readme will name a longer folder, something like mp_m_freemode_01_mp_m_gunrunning_01.
If it names one, use that folder and not the short one. There is a folder in here for every
name there is, so you never have to make one or guess the spelling. If the mod names nothing,
use the short one.

COLLECTIONS.md on the project page lists which is which if you ever need to look one up.


===============================================================================
TATTOOS, SKIN, FACE PAINT, BEARDS
===============================================================================

These do not go in a folder at all. Drop them right here, next to this readme:

   mp_gr_tat_027_m.ytd       a tattoo
   mp_fm_skin_m_up_whi.ytd   a skin texture

They are always one single .ytd file. There is no .ydd for a tattoo.

If you do not know which file name to use, the project page has a list. Open the docs folder
and open overlay_index.tsv. Search it for the tattoo. The column called txd is the name.

MOVING A TATTOO INSTEAD OF REPLACING IT
Where a tattoo sits on the body, and how big it is, is not in the picture. It is a set of
numbers in a game file. This plugin can change those numbers, but you have to dig the file out
of the game with OpenIV and edit it by hand. The README on the project page walks through it.
Edit only the tattoo you care about and leave the rest of the file alone, or the plugin will
refuse the file.


===============================================================================
ANIMALS
===============================================================================

Eight animals have a folder in here, the same as your character does:

   a_c_chop  a_c_husky  a_c_mtlion  a_c_panther
   a_c_retriever  a_c_rottweiler  a_c_sharktiger  a_c_shepherd

Body parts go in the animal's folder. Any loose files the mod ships go right here in the top
of tex_overrides, NOT in the animal's folder:

   a_c_shepherd.yft

Every other animal, and any pet a server added itself, has no folder. Make one named exactly
after the animal and put the parts in it, and put any loose files in the top of tex_overrides.

ABOUT THE .ymt FILE
Animal mods usually ship one, for example a_c_shepherd.ymt. For those eight animals it will be
turned away, on purpose. The game already owns that exact name and the call that would replace
it crashes the game outright, so there is nothing to be done about it. Drop it in anyway if you
like, the log will just say it was ignored and the rest of the mod still loads.

What you lose is only the extra parts the mod ADDED on top of the original animal. Those stay
unselectable. Anything the mod replaced still shows up. For every other animal the .ymt works
fine, so leave it in.


===============================================================================
ANIMATIONS
===============================================================================

An animation lives in a .ycd file. Drop it right here, next to this readme, no folder:

   anim@heists@box_carry@.ycd

Same rule as everything else, the name has to match the one already in the game.


===============================================================================
THINGS THAT DO NOT BELONG IN HERE
===============================================================================

.meta files. Mods ship them, and they hold shop data such as prices and menu labels, not
looks. The plugin walks past them and says so in the log. You can leave them where they are.

Empty folders. There are a lot of them in here and they cost you nothing. Leave them or
delete the ones you do not use, either is fine.


===============================================================================
IF SOMETHING DOES NOT SHOW UP
===============================================================================

Open texoverride.log, one folder up from here, in Notepad.

Every file you put in is in there, either accepted, or refused with the reason written out in
plain words. If a file is not mentioned at all, the plugin never saw it, which means it is in
the wrong place or has an extension the plugin does not handle.

If the log is missing entirely the plugin did not load. Either texoverride.asi is not in the
plugins folder, or the server blocks plugins. Some servers do, and there is nothing you can do
about that one.
