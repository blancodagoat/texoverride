# Changelog

## 0.8.8 (2026-08-23)

- Works on servers that run an older game build. The plugin has to name every build it supports,
  and it only named the two newest, so on a server pinned to anything else FiveM refused to load it
  and nothing was written to the log at all. That looked like a plugin that does nothing. It now
  names every build from 2189 up. Reported by benzwxc on a build 3407 server (issue #10).
- Weapon models can be replaced now. Put a `.ydr` file named after the weapon (like
  `w_pi_pistol.ydr`) straight into `tex_overrides` and the plugin claims that slot the same way it
  claims a texture or animation. The file name has to start with `w_`, which is how every GTA V
  weapon is named. Anything else, like a vehicle part or a prop, is still refused.
- Weapon textures (`.ytd`) already worked before this. Nothing changed there.
- The log is easier to read. Every line now says how serious it is and which part of the plugin
  wrote it, like `[INFO] [CLAIM]` or `[WARN] [SCAN]`, so when something does not work you can
  search the file for `WARN` and `ERROR` and find the reason instead of reading all of it. Lines no
  longer cut off with "and 12 more", and writes from different parts of the plugin can no longer
  land on top of each other. Make an empty file called `_debug.txt` in `tex_overrides` if you want
  the internal detail as well. Thanks to chocomintw for this.
- The list of collections in the log was wrong in three ways and is fixed. A collection was
  labelled by whichever of its files the server happened to stream first, so a collection you can
  override could be marked as one you cannot. Loose files were being listed as collections, under
  their own name plus extension. And the list stopped dead at 500 names without saying so, which
  read like a server that streams nothing. Collections that are neither a character nor a blocked
  one now say the honest thing, which is that it depends on the file names inside.

## 0.8.7 (2026-08-23)

- Animations can be replaced now. Put a `.ycd` file in `tex_overrides` and the plugin claims that
  animation the same way it claims a tattoo texture. This works for animations your **server**
  streams, which is where custom emotes live. Animations that came with GTA cannot be replaced this
  way, because they never pass through the call the plugin works through. The log lists every
  animation the server streams, so you can tell which is which before building anything, and there
  is a section in the README on how to put a pack together.
- The log says a great deal more when something does not work. It reports what the game's own
  streaming system resolves each name to, counts your files by type instead of stopping at the
  first sixty lines, reads the pool back after claiming to check the game really points at your
  files, and says each beat how many of your files the game is holding in memory and how many
  something keeps taking back.
- A file the server or a DLC already has loaded can be taken over mid-session in more cases, by
  attaching to the slot the game reports rather than asking for a new one. If the slot has not
  appeared yet, the file waits and is picked up when it does.
- Less of the plugin's work now happens before the game is allowed to start. Reading the
  crash-saver journal, listing your tex_overrides folder, reading the placement and budget files
  and finding the game's optional internals all moved to the background thread. The only thing
  that still has to happen first is installing the hook itself.
- Thanks to chunguscodes, whose pull requests are behind the last two of those.

## 0.8.6 (2026-08-22)

- If the plugin fails to install its hook, it now stops instead of carrying on. Before, a failed
  install was written to the log and then ignored, and the game crashed on the first thing it tried
  to load. Now it says so and leaves the game alone for that session.
- Copying a large folder into `tex_overrides` while the game runs no longer stalls it. The work is
  done a few files at a time across several frames instead of all at once, and a change is never
  thrown away just because the previous batch is still going.
- Fixed the crash protection losing track of files. Two sets of changes close together, and the
  second overwrote the record of the first, so a crash blamed the wrong file or no file at all.
- Builds are reproducible now. The same source produces the same bytes, and the build server builds
  twice and compares before publishing anything. You can build it yourself and check your file
  matches the release, which is the only real answer to "is this download actually the code above".
- Thanks to chunguscodes, who found all four and sent them as separate pull requests.

## 0.8.5 (2026-08-22)

- Characters and animals your server added itself work now. Before this, the plugin only accepted
  folders whose names came with the game, so a dog your server put in as `caninesd` was refused
  before anything was even read. The folder name is no longer what decides. What decides is whether
  the files inside are named the way GTA names body parts, like `head_000_r.ydd` or
  `uppr_diff_001_a_uni.ytd`. Name the folder after the model and it works.
- Nothing unsafe got easier. A vehicle texture, a prop or a map file is still refused whatever
  folder you put it in, because none of them are named like body parts. That was the real job the
  old folder list was doing. Story and cutscene characters are still refused by name.
- Animal `.ymt` files are refused now, and the log explains why. The game already owns those names
  and will not hand one over: the call that would replace it crashes the game outright, which is
  what a few people had been running into. Every animal ships one, so this can never work. The rest
  of an animal mod still loads. Only the parts a mod ADDED on top of the original stay unpickable.
- Every other `.ymt` name is accepted, which is what a clothing pack needs and what was blocked
  before for no good reason.
## 0.8.4 (2026-08-22)

- Animal `.ymt` files are now handed to the game later, about a minute after you start, instead
  of during startup. On 0.8.0 one player's game died at the exact moment the plugin passed its
  dog's `.ymt` over, while the `.yft` sitting right next to it went through without trouble. The
  difference we can do something about is when it happens: startup is very early, before the part
  of the game that reads those files has finished being built. So they wait, and go over on the
  game's own thread once everything is up. The log says `LATE-REG` when one lands.
- This is a considered guess, not a proven fix, and it is written to be safe either way. If it
  still goes wrong, the file is written down first, so the next launch skips that one file and
  starts normally instead of dying in the same place again.
- Nothing else changed about how `.ymt` files work. They are still loaded, still needed for
  anything an animal mod adds on top of the original, and still only accepted for `a_c_` names.

## 0.8.3 (2026-08-22)

- Fixed the plugin doing nothing at all for anyone whose Windows username has a non-English
  letter in it. Turkish, Hungarian, Polish, Chinese, anything outside plain A to Z. The plugin
  wrote your folder path one way and FiveM read it another, so FiveM went looking for a folder
  that did not exist, found nothing, and said nothing. The log looked completely normal, every
  file was listed as loaded, and not one of them was on your character. Found by akaloi in
  issue #2, who narrowed it down by making a second Windows account with a plain English name
  and watching the same files work straight away.
- If your path has such a character, the log now says so on its own line, so nobody has to work
  that out twice.

## 0.8.2 (2026-08-22)

- The protection added in 0.8.1 did not actually work, and this fixes it. It writes down which
  file the game is holding, so a crash can be traced to one file and that file skipped next
  launch. But FiveM catches the crash, shows its report window and then closes the game down
  tidily, and on the way out the plugin was wiping the note it had just written. So the next
  launch had nothing to go on and crashed in the same place again, forever. The note now survives.
- The log now explains a refused folder instead of just listing the rule. Mods built for RAGE MP
  or singleplayer can ADD a whole new animal, and their folder is named after a ped GTA V does not
  have. This plugin only replaces parts of animals and characters already in the game, so there is
  no slot for those files to take over. The log says that now, in those words.
- Added `docs/ped_collections.tsv`: every collection the game actually ships, 469 of them, read
  out of the game files. If a mod's folder name is not in there, that is the reason it does not
  load. The game has exactly eight animal collections, no more.

## 0.8.1 (2026-08-22)

- One player's game died at startup on 0.8.0 while the plugin was handing the game an animal
  mod's `.ymt`. The `.ymt` is still loaded, because without it nothing the mod added on top of the
  original animal can be picked. What changed is that a file which kills the game can now only do
  it once. The plugin writes down which file the game is holding at the moment it hands it over,
  and if the game dies right then, the next launch skips that one file, says so in the log, and
  starts normally. Deleting `_quarantine.txt` in `tex_overrides` gives the file another try.
  Before this, that protection only covered files you added while the game was already running.
- Files over 32 MB are loaded again. Since 0.5.3 they were refused outright, which left a pack
  half applied with no sign anything was wrong unless you read the log. The log still names every
  one of them, now on a `HUGE` line, and it is still the first place to look if you crash. If one
  of them does take the game down, the same one-launch protection above picks it up.
- Files the plugin cannot open at all are still skipped, since there is nothing to load.

## 0.8.0 (2026-08-22)

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
- The log now says whether the game had to wait for the plugin at startup, and for how long. On a
  big pack that one line is the whole answer to "is it faster now", because the scan itself mostly
  runs while the game is starting anyway.
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
