This is where your own files go.

CLOTHES AND BODY PARTS FOR YOUR CHARACTER
Put the file in the folder named after the collection it belongs to. Almost everything you will
ever change lives in these two:

    mp_m_freemode_01\        male character
    mp_f_freemode_01\        female character

So a shirt goes in, for example:

    mp_m_freemode_01\uppr_012_r.ydd
    mp_m_freemode_01\uppr_diff_012_a_uni.ytd

The other folders are for clothing that came with a specific game update. If a file came out of
one of those, it has to go in that folder and nowhere else. COLLECTIONS.md in the repo says which
is which.

SKIN, TATTOOS, FACE PAINT, BEARDS, HAIR COLOUR
These are loose files with no folder. Put them straight in here, next to this readme:

    mp_fm_skin_m_up_whi.ytd

ANIMALS
Eight animals have a folder, the same as your character does:

    a_c_chop  a_c_husky  a_c_mtlion  a_c_panther
    a_c_retriever  a_c_rottweiler  a_c_sharktiger  a_c_shepherd

Most animal mods also ship two loose files. Those go straight in here, NOT in the animal's folder:

    a_c_shepherd.yft
    a_c_shepherd.ymt

Do not skip the .ymt. Anything the mod adds on top of what the animal already had cannot be
picked without it, and the mod will look half applied.

Every other animal (pug, poodle, westy, cat, coyote, deer, and so on) has no folder at all. Its
files go straight in here as well.

EMPTY FOLDERS COST YOU NOTHING
Leave the ones you do not use. Delete them if you prefer a tidy folder. Either is fine.

IF SOMETHING DOES NOT SHOW UP
Open texoverride.log, one folder up. Every file is either accepted, or refused with the reason
written out. A file that is not mentioned at all is one the plugin never saw.
