# Changelog

## 0.8.0 (2026-08-21)

- Animals work now. Eight of them are built the same way your own character is, out of a folder of
  parts: chop, husky, mtlion, panther, retriever, rottweiler, sharktiger and shepherd. Most dog
  mods you can download are already laid out the way this plugin wants, so the folder goes straight
  into `tex_overrides` and that is the whole install.
- Mods for those animals also ship two loose files, a `.yft` and a `.ymt`, and both are accepted
  now. They go in `tex_overrides` itself rather than in the animal's folder. The `.ymt` is the
  important one: it is what tells the game which parts and textures exist, so without it anything
  the mod added on top of the original animal cannot be picked and the mod looks half finished.
- Animals that are one single model instead of a folder of parts (pug, poodle, westy, cat, coyote,
  deer, and the rest) work too. Their files go straight into `tex_overrides` with no folder.
- Releases now come as a zip as well. It holds the plugin plus a ready-made `tex_overrides` with a
  folder already created for every collection you can use, so nobody has to guess a name or spell
  one. The plugin on its own is still there for upgrading, so your own folder is left alone.
- Nothing else got easier to touch. Story characters, vehicles, weapons, props, maps and scripts
  are refused exactly as before, and there is now a test that checks that on every build.

## 0.7.3 (2026-08-21)

- Big packs no longer hold the game on the loading screen. Before it can start, the plugin has to
  look at every file you gave it, and it was doing that in the one place where the game can only
  sit and wait for it. On a pack with thousands of files that ran for minutes with nothing on
  screen. That work now happens while the game gets on with its own startup, several files at a
  time instead of one, and each file is read once instead of twice.
- The log says how many files it found, how long the check took, and names the step while it is
  running, so a long pause during startup no longer looks like the plugin died with no
  explanation.
- Nothing about what gets loaded has changed. The size limit that keeps oversized files out still
  applies to exactly the same files, in the same order, with the same log lines.

## 0.7.2 (2026-08-20)

- The check that reads your graphics card can no longer take the game down with it. If it ever
  fails on your machine the plugin now says so in the log, leaves the texture budget exactly as the
  game set it, and carries on doing everything else.
- Background, since it explains 0.7.1 as well. In 0.7.0 that check ran far too early in startup,
  while Windows was still loading plugins. On one player's PC it crashed outright, and on another
  it appears to have upset a separate upscaling plugin that also works with graphics memory, which
  took the whole game down and looked like that other plugin's fault. Moving the check later, in
  0.7.1, fixed both. This release makes sure that even in the worst case it can only ever cost you
  the budget feature.

## 0.7.1 (2026-08-20)

- Fixes the headline feature of 0.7.0, which did not work. The check that reads how much video
  memory your card has was running too early in startup, at a point where Windows will not answer
  it, so every log said it could not read the card and the budget was left alone. It now runs a
  moment later, once the game is properly up, and it says which step failed if it ever cannot read
  the card at all.
- Corrected what the log and the readme say the game's own ceiling is. It is about 2.9 GB with the
  Extended Texture Budget slider untouched and about 7.8 GB with that slider maxed out. Neither
  number has anything to do with your graphics card, so a 24 GB card and an 8 GB card hit the same
  wall, which is the whole reason this feature exists.
- The pack cost report now compares your pack against what the game is actually giving you rather
  than guessing.

## 0.7.0 (2026-08-20)

- The texture budget now sizes itself to your PC instead of leaving everyone on the same fixed
  ceiling. GTA gives every machine the same roughly 3 GB for textures no matter what card is in
  it, which is why the "textures gone, stuck on low detail, restart needed" bug hits high end
  builds just as hard as cheap ones. The plugin now asks Windows how much video memory it is
  willing to hand this process, holds back a quarter of it (or 1.5 GB, whichever is more) for the
  rest of the game, and raises the ceiling to what is left. Nothing to configure. Cards with
  little to spare are left alone.
- `_budget.txt` still works and still wins if you want to pick the number yourself. Put a 0 in it
  to switch the whole thing off and leave the game's budget untouched.
- The pack cost report now says whether the raised ceiling actually covers your pack, and stops
  implying a bigger graphics card would have saved you.
- Releases are now signed with build provenance and list the file's SHA-256, so you can prove a
  download came from this repository and was built from this code rather than taking anyone's word
  for it. The README shows the one command that checks it. This does not change antivirus warnings,
  which need a paid certificate; it does mean a tampered copy from somewhere else fails the check.
- The file now carries proper version details, so right clicking it and looking at Properties
  shows what it is and where it came from. A file with no details at all counts against it with
  Windows Defender, which is part of why some people saw a trojan warning on a fresh release. The
  README now has a section explaining those warnings and what to do about one.
- Clearer message when a file added while the game is running cannot be picked up. The game will
  not hand over a name the server or a DLC has already loaded, so a restart is the only way to
  claim it. The old wording made that sound like a plugin failure and left the impression that
  live reload was not working, when editing files the plugin already owns, and tattoo placement
  edits, apply live as they always did. The log now separates the three reasons a live add can
  fail instead of lumping them into one line.

## 0.6.3 (2026-08-20)

- The crash-size check now covers mesh data as well as texture data. A player's crash dump
  showed the same crash coming from files whose bulk is 3D mesh rather than textures, which the
  0.6.1 check did not count. Anything with more than 32 MB on either side is now refused with a
  TOO BIG line naming it.

## 0.6.2 (2026-08-20)

- The crash cause is confirmed: removing the files over 32 MB stopped the crashes in testing,
  so the size gate from 0.6.1 stays and got tougher. It now also covers files that are
  overwritten with a too-big version while the game runs, falls back to the file's size on disk
  when the header cannot be read, and can no longer be fooled by a broken header claiming a
  tiny size.
- A handful of smaller fixes from a code review: the previous-session log is still cleared even
  when something holds it open, the crash journal from a previous session can no longer be
  deleted before it has been read, and the VRAM check now always talks to Windows' own graphics
  library by full path.

## 0.6.1 (2026-08-20)

- Files with more than 32 MB of texture and mesh data are no longer loaded, and the log says
  so with a `TOO BIG` line naming each one. Every crash we have investigated so far involved
  packs with 45 to 112 MB files in them, while 32 MB files are verified to work. Shrink the
  named files with the CodeWalker Shrink Textures tool to get them back.
- Fixed a startup failure ("Couldn't load texoverride.asi", game refuses to start) on machines
  where a graphics mod's dxgi.dll sits in the FiveM folder without providing everything the
  plugin asked from the real one. The plugin now talks to the system's own dxgi directly.
- If the plugin hits any other error while starting up, it now turns itself off for that
  session instead of stopping FiveM from launching.
- The log from your previous session is now kept as `texoverride.log.old` instead of being
  erased on every launch. If the game crashes, the log that shows what happened survives the
  next start.

## 0.6.0 (2026-08-19)

- You can now raise the game's texture budget past what the settings slider allows. Put a file
  named `_budget.txt` holding a number of GB (for example `8`) into `tex_overrides` and restart.
  The plugin caps the number at what your video card actually has, checks that it found the right
  spot in memory before writing anything, and keeps the value in place when the settings screen
  tries to put it back. More budget means more headroom before the "stuck on low detail" bug
  hits. It is not a cure, and asking for more than your card can hold would cause stutter, which
  is why the plugin refuses to go past your real VRAM.

## 0.5.2 (2026-08-19)

- The log now reports what the whole pack costs the game in memory once everything is loaded,
  and prints a `HEAVY` line for every file that costs 8 MB or more. Oversized or uncompressed
  textures are the usual cause of the "stuck on low detail, textures gone, restart needed" bug
  on busy servers, and the log now names the exact files to shrink. The README explains the bug
  and the fix in a new section.

## 0.5.1 (2026-08-19)

- The update popup now asks if you want to open the download page, and Yes opens it in your
  browser. Before this you had to type the address yourself.

## 0.5.0 (2026-08-19)

- Live reload. The plugin now watches `tex_overrides` while you play. Save an edited
  `overlays.xml` and the tattoo moves in game within a second or two. New `.ytd` and `.ydd`
  files are picked up without a restart. Overwritten textures show the next time the game
  reloads that item, so take the outfit or tattoo off and put it back on to see them. When
  something cannot be applied live, the log says so.
- Crash saver. If the game crashes right after a live change, the next launch refuses to load
  the files involved and says so in the log, so one broken file cannot crash the game twice.
  Delete `_quarantine.txt` from `tex_overrides` to let them load again.

## 0.4.1 (2026-08-19)

- Dropped `.meta` files (like `shop_tattoo.meta`) now get an "ignored" line in the log instead of
  silence, wherever they sit in `tex_overrides`. They hold shop data, not looks; the README
  explains what to do instead, and reserves the pack-folder layout
  (`tex_overrides/mplowrider/shop_tattoo.meta`) for them.

## 0.4.0 (2026-08-18)

- Update check. At startup the plugin asks GitHub for the newest release number, and shows a
  small popup when a newer version is out. That is its only network use; nothing about you or
  your game is sent. Turn it off with an empty `_NO_UPDATE_CHECK` file in `tex_overrides`, or
  skip everything with `_OFF` as before.

## 0.3.0 (2026-08-18)

- Tattoos, skin, face paint, beards and other body overlays can be replaced by putting the
  `.ytd` straight into `tex_overrides`, without any folder. The file replaces the one texture
  with the same name. Any name is accepted, so custom server tattoo packs work too.
- Tattoos can be moved, resized and rotated by putting an edited `overlays.xml` into
  `tex_overrides`. Before changing anything, the plugin checks that the file still matches the
  running game, and skips the file if it does not line up.
- Added `docs/overlay_index.tsv`, a table of all 3,921 tattoos and overlays in the base game. For
  each one it lists the file that owns it, its position, size, rotation, and the texture name.
- The hook is installed earlier now, while FiveM is still loading and before the game has run any
  code, so nothing can be executing the code being patched. The old approach tried to pause all
  threads first, which FiveM blocks anyway.
- Rewrote the README and COLLECTIONS.md in plainer English.

## 0.2.0

- First public version: clothing replacement per collection folder, claim and re-assert at the
  streaming layer, full logging.
