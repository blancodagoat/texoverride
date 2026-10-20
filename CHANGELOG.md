# Changelog

## 0.8.29 (2026-10-20)

- Fixes Windows accounts whose name uses letters outside Windows' "language for non-Unicode
  programs" setting, like a Russian name on an English Windows. On those the plugin could not
  even find its own folder. Every path inside the plugin is now kept in a form that holds any
  alphabet, and Windows is always asked for files that way.
- Fixes the same kind of bug in a second place. When a server streamed a file you had replaced,
  the plugin handed the game an old-style path. On an account name with letters like ö or ç the
  swap did nothing, while the log still said `REDIRECT`.
- The crash saver now covers files you add while playing. If the game crashed within 30 seconds
  of a live add, FiveM's own shutdown could delete the note naming that file, so the next launch
  loaded it again and crashed again. Now that file is set aside. The catch: quitting the game
  within 30 seconds of adding a file sets it aside once too, and the log says how to undo it.
- A file added while playing is now registered exactly like one that was there at startup,
  including the check for names the game already owns. Before, those could land in a slot
  nothing reads.
- A crash inside the game's own registration while adding a file live is no longer hidden. It
  used to be caught, which left the game's file list half updated. The game then died minutes
  later, looking like somebody else's fault.
- When the plugin cannot find the game function it hooks, it now really switches itself off, as
  the log line already said. Before, parts of it kept running.
- Tattoo placement re-checks the size of the game's list every second. A list that shrinks after
  a pack reloads can no longer be written past its end.
- The texture budget is never lowered. If you raise the graphics slider during a session, the
  plugin leaves FiveM's higher number alone instead of writing its own smaller one back every
  second.
- A typo in `texture_budget` now gets a warning in the log instead of quietly turning the raise
  off.
- The updater only downloads over https, and a download that gets cut off counts as a failure
  instead of a finished file.
- The updater also looked for the download link starting a little before the plugin's entry in
  GitHub's answer. That was close enough to the .zip's link that it could have fetched the wrong
  file. It now reads only the plugin's own entry.
- Startup is lighter on big packs. The crash saver's note is written through one open file
  instead of being opened and closed once per file, and every close meant another antivirus scan.
- A folder link that points back up the tree no longer sends the scan round forever. It stops 16
  folders deep.
- Log lines renamed: `Occupied-slot exports` is now `Slot lookup exports` and
  `Live reload exports`. The "non-English characters" line is gone, since there is only one kind
  of path now.
- For anyone building it: the code is reorganised so each part keeps its own data to itself, and
  live reload and startup share one claim function. `build.bat dev` replaces `builddev.bat`.
- The build server now checks that the version number matches everywhere it is written. It also
  runs new tests for the update check, the file size reader, the tattoo file reader, and a folder
  named in two alphabets.

## 0.8.28 (2026-09-13)

- The plugin no longer looks up the Windows functions for listing and freezing threads. The
  copy of MinHook in here used to suspend every other thread while it wrote its patch, which
  never worked under FiveM in the first place, since FiveM blocks the call that lists them.
  It was not needed either: the patch goes in before the game has started running, so there
  is no thread to freeze. Eight function lookups are gone from the file with it, and thread
  listing plus thread freezing is one of the things antivirus software watches for hardest.
  Nothing about how the plugin behaves changed. Measured on VirusTotal the same day: 0.8.27
  was flagged by four engines out of 71 and this build by one. Bkav, Cynet and Trapmine no
  longer flag it. Defender still does, as `Trojan:Win32/Wacatac.C!ml`, which is reported to
  Microsoft as a false positive. Part of that is simply that this file is new, so treat the
  three that cleared as encouraging rather than proven.
- Why bother: 0.8.27 and 0.8.26 hold the same code, and Defender flagged one and not the
  other. `docs/antivirus.md` now has the numbers from both, explains why a brand new release
  scores worse than an old one even when nothing in it changed, and lists every Windows
  function the plugin uses next to what a scanner reads it as.
- New test covering the patch going in and coming back out, since that code was rewritten.
  It hooks a function inside the test itself, so it runs anywhere, no game needed.

## 0.8.27 (2026-09-12)

- The download now has a folder for the GTA World clothing packs. Nine of them, three per
  character plus the prop ones, named the way the server names them. Before this you had to
  read your log and make the folder yourself. The server dogs and cats have folders too:
  `canine`, `caninepd`, `caninesd`, `caninefd`, `caninesp`, `blackcat` and `browncat`.
  `COLLECTIONS.md` lists them all, and the readme in the folder says how to find the names
  for any other server.
- No code in the plugin changed. The only difference from 0.8.26 is the version number
  stamped into the file, so there is nothing to gain from updating unless you want the new
  folders. Everything else below is about the repository, not the plugin.
- The tests run on every push now. Four of the five only existed on one machine, in a folder
  git ignores, so nothing ever ran them. One of the five had been failing since 0.8.8: it
  still expected a `.ydr` or `.yft` dropped straight into `tex_overrides` to be refused,
  which the weapon and prop work deliberately changed. Nobody could see it, because nothing
  ran it.
- The build compiles at a higher warning level. It found two harmless things, both fixed.
  The vendored copy of MinHook builds on its own now so its own warnings stay out of the way.

## 0.8.26 (2026-09-09)

- Fixes the crash 0.8.25 caused on busy servers: `ERR_GEN_PAGE_1` at `GTA5_b3751.exe+13EC17E`,
  a few minutes into a session. The cause was `force_reload`, added in 0.8.24 and widened in
  0.8.25. Once claims started landing on the game's real slots, it began dropping clothing and
  animation files out of memory while a ped was still using them, which is the one thing a plugin
  must never do to the game. It had never done anything useful in any log before that either. It
  is gone, the plugin never frees anything the game has loaded any more, and the line is removed
  from your `_settings.txt` on the next start. The real-slot fix from 0.8.25 stays.
- The log always carries full `DEBUG` detail now. The `debug` option is gone and its line is
  removed from your settings file. Nobody has to be told to turn it on and send the log again.
- Update messages always appear. The `no_update_check` option is gone. A new version always
  produces a message, whether the plugin installed it on its own because `auto_update` is on or
  is asking you first. Before this, with `auto_update = yes`, an update landed in silence, which
  is how "I never turned updates on" reports start.
- The 500 line cap on the list of overridable server files is gone. It only ever existed to be
  lifted by `debug`.

## 0.8.25 (2026-09-08)

- Your files were being registered into slots the game never reads. This is the big one, and it
  affected every file in every version so far. The plugin claims a name on the first file the
  game streams, but at that moment the game cannot answer "what does this name point at yet",
  so the claim invented a brand new slot instead of taking over the real one. Your file loaded,
  the log said it was held, and the game carried on reading its own copy. The plugin now waits
  for the game to be ready to answer before it claims anything. On one real pack this turned
  758 invented slots into 220 real takeovers.
- Added support for game build 3889 (The Kortz Center Heist) and 3323. Without a line for the
  build you are on, FiveM refuses the plugin and writes no log at all.
- Body skin and face overlays cannot be replaced from tex_overrides, and the log now says so at
  startup instead of letting you find out the hard way. Files named `mp_fm_skin_*`,
  `mp_fm_faov_*`, `ng_hip_faov_*` and `mp_eye_colour` get claimed, get loaded, and still do not
  show, because your ped is built from them earlier than any of this reaches. Those belong in a
  FiveM mods folder package. Head models and head textures in a collection folder are not
  affected and keep working normally.
- Dropping the game copy of a file now also happens when the plugin had to take the slot back
  before it saw the file load. That case was slipping through, so a file could sit there held
  by us and still show the stock version.
- The log no longer tells you to switch on `debug` in a line that only prints when `debug` is
  already on.
## 0.8.24 (2026-09-08)

- Your files now win a slot the game already filled. Claiming a name early was only ever half
  the job: if the game loaded its own copy first, that copy stays in memory and nothing on screen
  changes, which is why body skin, eye colour and some clothing textures could look untouched
  while the log said everything was held. The plugin now drops the game's copy so yours is read
  instead, and says `FORCED-RELOAD` when it does. This is why it matters: the same texture name
  can live in four different game archives, and every one of them gets loaded while you start up.
  Set `force_reload = no` in `_settings.txt` if you would rather it left loaded files alone.
- The log was reporting the wrong half of what a loaded file costs. It measured system memory,
  and texture pixels live in video memory, so every texture came back as 0.0 MB. A 16 MB body
  skin looked free, and the "really costs" warning could never fire for a texture, which is the
  one kind of file that texture loss is actually about. Both figures are read now and shown
  separately.
- The check for "does the game already own this name somewhere else" was made one step too late,
  after the plugin had already claimed the name, so it could only ever answer no. On a 335 file
  pack it had never once found anything. It is asked first now, and where the game does own the
  name elsewhere, both entries are pinned, which is what the log line always said it did.
- Every claimed slot is now re-checked in turn, a few dozen per second, instead of only the ones
  something else wrote to. A slot nobody touched was never looked at again after startup, so if
  the game moved that name to a different place the plugin went on reporting it as held for the
  rest of the session.
- The line that says whose file the game read now looks at the moment the game started reading,
  not a second later. If the plugin took the slot back in between, the old line called that a win
  when the game may well have read its own copy. When it cannot tell, it says so.

## 0.8.23 (2026-09-07)

- The refresh key is gone. Pressing F11 read the folder again, but the watcher already does that
  on its own the moment a file changes, so the key had nothing left to find and looked broken.
  What no key can do is make the game draw the new file: once the game has loaded something it
  holds on to it until you restart.
- The plugin takes the old `refresh_key` line out of your `_settings.txt` on the next start, along
  with the note above it, so the file stops describing a setting that is not there. Everything
  else in the file is left exactly as you wrote it.

## 0.8.22 (2026-09-06)

- Put `_override` at the end of a folder name and the files inside it win against the same file
  in any other folder. Keep a big pack whole in `feseroclothingpack/` and put your own edited
  copies in `feseropack_override/`, and yours are the ones that load. Before this the winner was
  whichever folder Windows handed over first, which nobody could steer. The log says `PREFERRED`
  on the line where a file takes a slot off another copy.
- The suffix works on a collection folder too, so `mp_m_freemode_01_override/uppr_013_r.ydd`
  still loads as part of `mp_m_freemode_01`. Two `_override` folders holding the same file are
  back to first one found, and the log still says `DUPLICATE`.
- Which copy wins is worked out when the game starts. Copy an `_override` pack in while you are
  playing and the log now tells you it needs a restart, instead of saying nothing at all.

## 0.8.21 (2026-09-03)

- `hide_overlay = always` no longer cuts DUI screens in half. A player found that with the
  setting on, a DUI television drew only one triangle of its screen, and drew normally with the
  setting off. The handler the setting takes off the screen draws FiveM's version watermark, and
  in a stock FiveM that is the last draw of every frame. Taking it away left the frame ending in
  a different state, and the next frame's screen quad lost a triangle to it. The plugin now puts
  a handler of its own in that exact spot. It draws nothing you can see: a zero-size triangle,
  which is the same trick FiveM itself uses to settle its drawing state before each frame.
- The same handler is also the one place FiveM draws the text its other parts queue up, and the
  place its text memory is recycled. With it parked, that text (the red reconnect warning, for
  one) never appeared and the queue only ever grew. The stand-in makes that call too.

## 0.8.20 (2026-09-03)

- The re-assert loop checks that a slot still carries the file's name before writing its handle
  back. FiveM frees a slot the server created once that resource is gone, and after a reconnect
  the same index can belong to a different file. Writing into it then pointed a stranger's asset
  at one of your files, and the game read a shirt where it wanted a lamp. It only affected files
  the server streamed first and the plugin took over later (`LATE-BIND`, live reload, occupied-slot
  takeover), and only after a disconnect and reconnect in the same session. The lookup by name
  happens only when something else has written the slot, so the ordinary beat costs nothing
  extra. When the name has moved the plugin follows it and the log says `MOVED`; when the name is
  gone it waits for it to come back, the way `LATE-BIND` already does.

## 0.8.19 (2026-09-02)

- A folder whose name starts with `disabled` is skipped, with everything inside it. Rename
  `Pack1` to `disabledPack1` and it stops loading; rename it back and it loads again. That is
  the whole feature: swap between packs by renaming folders, no deleting or moving files. The
  log says `DISABLED` and the folder name at startup so you can see it was skipped on purpose.
  Renaming mid-session works for turning a pack on. Turning one off needs a restart, because
  files already handed to the game stay there until FiveM closes. Suggested by a user.

## 0.8.18 (2026-09-02)

- `hide_overlay = always` keeps FiveM's version text and mod pack counter off your screen for
  the whole session, not only around a screenshot. The 0.8.16 notes said there would never be
  such a setting. That is reversed here, on purpose, and said out loud rather than slipped in.
  What has not changed: this is your own screen only. The server sees nothing, other players
  see nothing, and the plugin still writes into no code of FiveM's or the game's. The key list
  works as before if you would rather keep the text and only lose it in pictures.

## 0.8.17 (2026-09-02)

- You can now keep your files in folders of your own inside `tex_overrides`. Put
  `clothingpack1/mp_f_freemode_01_female_heist/uppr_013_r.ydd` and it loads exactly as if the
  `clothingpack1` folder were not there. The rule: a clothing file's collection is the folder it
  sits in directly, and any folders above that are yours to name. Weapons, props, animations
  and tattoo textures can go in folders too; they still load by their own file name. The flat
  layout keeps working unchanged, so nothing has to move.
  If two packs ship the same file, the first one found wins and the log says which copy was
  skipped (`DUPLICATE`). Files the plugin cannot place now get a `SKIP` line that says why.

## 0.8.16 (2026-09-01)

- New `hide_overlay` setting keeps FiveM's version text and mod pack counter out of your
  screenshots. FiveM writes its version in one corner of the screen and "N mod packs loaded" in
  the other, and both end up in every picture you take. List the keys you take screenshots with,
  for example `hide_overlay = printscreen, f9`, and those two lines come off the screen for a
  second and a half when you press one, then come back. Nothing else changes: the server's chat
  and HUD stay where they are. There is no way to leave the text off, and there will not be one.
  The branding is FiveM's and the plugin does not remove it; this is only about it not being in
  your saved pictures.
  If you already have a `_settings.txt`, add the line yourself. The plugin never rewrites that
  file, so an existing install does not get the new option on its own.
  One limit, stated plainly: the text comes off on the next frame, so a tool that grabs the
  screen in the same instant as the keypress can still catch it. ShareX, Steam and the like grab
  it a moment later and come out clean. Windows' own PrintScreen to clipboard is the one that
  may not.
- This adds no patch into the game. It moves two entries on one of FiveM's own drawing lists
  and moves them back. Nothing is written into any code, FiveM's or the game's, and the
  antivirus profile of the file is the same as 0.8.15's.

## 0.8.15 (2026-08-30)

- Fixes the crash on startup that 0.8.14 brought in. Several people saw the game die once or
  twice during loading before it would start. 0.8.14 was the first version to hook into FiveM's
  own per-frame events, and it did that from a background thread a second or two into loading,
  which is the exact moment FiveM's own components are doing the same thing. Two threads editing
  the same list with no lock can lose an entry, and the entry that goes missing is not ours. The
  plugin now signs up while the game is still being handed to FiveM, before any of that starts,
  and the sign-up itself is a single atomic write that cannot drop somebody else's entry.
- The texture pool reading is taken on the game's own thread now, instead of from the plugin's
  background thread. It asks the game for a pointer, and a question like that belongs where the
  game asks it.
- The plugin can update itself again. It checks the download against a SHA-256 before installing
  anything, and it used to read that hash out of the release notes, which meant a hand-edited
  release page left it with nothing to check against and every install stuck on the old version.
  It now reads the hash GitHub publishes on the file itself, and only falls back to the notes.

## 0.8.14 (2026-08-29)

- Live reload now runs on FiveM's own per-frame event instead of a patch into the game's message
  pump. One less patch into the game, and the work always lands on the game thread. If a FiveM
  update ever stops exporting that event, the old pump is still there as a fallback, and the log
  says which one it used. To be straight about what this does and does not do: the folder is
  watched and your changed file is read again, but if the game already has the old version loaded
  in memory it carries on drawing that, and taking the item off and putting it back on does not
  reliably force a fresh read. Editing a file the game has not loaded yet is the case that works.
  For anything already on your ped, restart FiveM.
- New refresh key. Press F11 in game and the plugin reads `tex_overrides` again straight away,
  the way SA modloader worked. Set `refresh_key` in `_settings.txt` to any f1 to f12 key, a
  letter, a digit, or `off` to switch it off. It only responds while the game window is focused,
  and it always writes a line in the log, including "nothing has changed since the last look", so
  a key that finds nothing never looks like a key that does not work.
- Tattoo placement files with only one or two presets now work, and so do files where you edited
  every value. The layout only has to be worked out once per session, and every file after that
  is matched by its preset names alone.
- The log now says how much of the texture budget the game is actually using, and warns once when
  it passes 90 percent. That is the number every "my textures went missing" report needed and
  nobody had.
- Each file now reports what it really costs in memory, including the textures it shares with
  other items. The old figure came from the file on disk, which charged a model nothing for the
  textures it pulls in with it.
- The live-reload watcher no longer waits forever on a server that streams nothing of its own.

## 0.8.13 (2026-08-29)

- The old marker files migrate themselves and then go away. 0.8.12 left two ways to set every
  option, the settings file and the `_off` / `_debug` / `_verbose` / `_budget` / `_auto_update` /
  `_no_update_check` files, which is not standardising anything. Now the first launch that finds
  one copies its value into `_settings.txt`, deletes it, and says in the log what it moved. Your
  settings carry over on that same launch, so nothing changes behaviour, and from then on the
  folder holds one settings file instead of a scattering of empty ones. Notes you added to the
  file survive, and a setting whose line you deleted is put back rather than lost.

## 0.8.12 (2026-08-29)

- One settings file instead of a folder of oddly named marker files (issue #20 by chocomintw).
  The plugin writes `_settings.txt` into `tex_overrides` on first run. Every option is in it,
  switched off, with a plain English note above it saying what it does. Change `no` to `yes`,
  save, restart FiveM. The options are `off`, `debug`, `texture_budget`, `auto_update` and
  `no_update_check`. `yes`, `on`, `true` and `1` all mean on, and capital letters do not matter.

  The file is only ever created, never rewritten, so your edits and any notes you add survive
  updates. Delete it to get a fresh one.

  Nothing breaks for anyone using the old marker files. `_off`, `_debug`, `_verbose`, `_budget`,
  `_auto_update` and `_no_update_check` all still work, and now each is accepted with or without
  `.txt` on the end, so it no longer matters whether Windows is hiding file extensions when you
  make one. A marker file turns its option on and `_settings.txt` cannot switch it back off, so
  delete the marker file to go back to the settings file.

- `debug = yes` now lists every overridable server file instead of stopping at 500, and names the
  ones it used to only count. Looking up the exact name of one server prop is the main reason to
  read that part of the log, and the cap got in the way of the one job it has. The default is
  unchanged, so nobody who has not asked for it pays the extra log lines.

- A file with no `RSC7` header is refused instead of loaded. Those are raw dumps rather than real
  game files, usually a texture renamed to `.ytd` or something pulled out with the wrong export
  option, and the game dies in its own loader with "Invalid fixup, address is neither virtual nor
  physical" the moment it streams one in. The log now names the file and says to export it with
  OpenIV. `.ymt` is exempt, since a metadata file is not always an RSC7 resource.

- README: how to install a vehicle mod that replaces a car or bike the game already has. The
  model and texture files go straight into `tex_overrides` like any other prop. Adding a brand new
  vehicle still needs the `mods` folder package it came with, because that needs `vehicles.meta`
  and `handling.meta`, which the plugin cannot read.

## 0.8.11 (2026-08-26)

- The update popup can now download and install the new version for you (PR #18 by chocomintw).
  Yes downloads `texoverride.asi` from the GitHub release, checks that it is a 64-bit DLL and that
  its SHA-256 matches the one CI printed in the release notes, then swaps it in. The new version
  loads the next time FiveM starts. No opens the release page as before, Cancel skips it. The
  version you had is kept beside the new one as `texoverride.asi.old`, so if the new one misbehaves
  you can delete `texoverride.asi` and rename the `.old` file back. A release whose notes carry no
  hash is never installed this way; the popup falls back to opening the page. An empty
  `_AUTO_UPDATE` file in `tex_overrides` installs updates without asking.

## 0.8.10 (2026-08-25)

- The log file is now opened once and kept open, instead of being opened, written and closed for
  every single line. At connect the plugin writes several hundred lines while the game's own file
  thread is inside our hook, and each open and close was a trip through NTFS and the antivirus
  on-close scan. Every line is still flushed straight away, so a crash log is not missing its last
  lines. One side effect: the log cannot be deleted while FiveM is running (it can still be read
  and copied).
- The file name a server streams is lowercased once per hook call instead of twice.
- The log now says whose file the game actually read the first time each held slot lands in
  memory. `LOADED ... from your file` is a debug line; `LOADED ... from the GAME file` is a
  warning, and a RECLAIM on a slot that is already in memory says so. Holding a slot is only half
  the story: if the game loaded the texture while a DLC mount owned the handle and then pinned
  it, the swap never shows. Body skin (`mp_fm_skin_*`) is the case that prompted this, because
  the skin blend keeps a reference on its source textures for the life of the ped.

## 0.8.9 (2026-08-25)

- The log could go silent for a whole session. It was opened in a mode that refuses to share the
  file, so while any other program held `texoverride.log` open (a log viewer, an upload in
  progress) every line the plugin tried to write was dropped, with nothing to show for it. It now
  opens shared.
- When a file is registered but its slot is not there yet, the log now says why (no such file type
  in the game, or the name is unknown), instead of only "target not present yet".
- Props can be replaced now. Any `.ydr` or `.yft` put straight into `tex_overrides` is taken and
  handed to the game under that exact name, so a phone, a notepad, a police laptop or a door model
  can be swapped the same way a weapon model can. Until now only `w_` names were taken, and a prop
  pack copied in as a folder was refused file by file with a message about ped part naming. That
  message now says where the files go instead. Tested in game on two props that ship with GTA
  (`prop_beer_bottle`, `prop_beer_logger`), which also settles the open question from 0.8.8: a
  model slot the game already owns can be claimed, so weapons that came with GTA work as well.
- `_OFF` now really is off. It used to still install the streaming hook and route every file
  registration through the plugin; now the plugin returns before it creates a log, scans for
  patterns, installs anything or starts a thread, so an `_OFF` launch is the same as a launch
  with no plugin. It no longer rotates `texoverride.log` either. By chunguscodes (#14).
- Live reload paces itself. Some game loops call the message pump several times per frame, which
  could turn a big folder copy into hundreds of registrations in one burst. The pump now does one
  batch every 10 ms, repeated writes to the same file fold into the work already queued, and the
  queue is capped at 2,048 changes; past that the log says which changes need a restart. By
  chunguscodes (#13).

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
- The log is much shorter and much faster. It used to list every single file a server streams,
  including all the car parts, props and map pieces the plugin can never touch. On one real server
  that was 24,758 useless lines out of 26,337, over 1,600 of them written in a single second, and
  the log filled 3 MB in a couple of minutes. Worse, writing each line briefly blocked the thread
  the game runs on, so a big server could stutter while the list was being written. Now the useful
  lines stay: every collection, refused ones included, and every loose file you could actually
  replace. The rest are counted and the total is reported. Put a file called `_debug.txt` in
  `tex_overrides` to get the full list back.
- The limit on how many lines the file list writes actually works now. It announced itself at 500
  and then carried on listing everything anyway.
- Thanks to chunguscodes, whose pull requests are behind two of those.

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
