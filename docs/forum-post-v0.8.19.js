// Forum announcement post for texoverride v0.8.19, covering everything since the 0.8.7 post.
//
// Same as the other posts in this folder: the cfx.re reply box is a CKEditor instance, so open
// the reply box, paste this whole thing into the browser console, and it fills the box in.
// It looks the editor up rather than naming an instance id, because the id is per topic.

(() => {
  const ck = window.CKEDITOR;
  const ed = ck && (ck.currentInstance || ck.instances[Object.keys(ck.instances)[0]]);
  if (!ed) return console.error('No CKEditor instance found. Click into the reply box first.');
  ed.setData([
    '<h2>texoverride 0.8.19: your own folders, one settings file, clean screenshots</h2>',
    '<p>Twelve releases since the animations post. Most of them are small. Four of them change how you use the thing day to day, so those go first, then the rest in order of how much they matter.</p>',
    '<h3>Keep packs in folders, and switch them off by renaming</h3>',
    '<p>Until 0.8.17 every collection folder had to sit directly in <code>tex_overrides</code>. Now you can put your own folders around them:</p>',
    '<pre><code>tex_overrides/clothingpack1/mp_f_freemode_01_female_heist/uppr_013_r.ydd\ntex_overrides/weapons/w_pi_pistol.ydr\ntex_overrides/tattoos/mp_sum2_tat_051.ytd</code></pre>',
    '<p>The rule: <strong>a clothing file\'s collection is the folder it sits in directly</strong>, and any folders above that are yours to name. Weapons, props, animations and tattoo textures load by their own file name from any depth. The old flat layout still works, nothing has to move.</p>',
    '<p>New in 0.8.19: <strong>a folder whose name starts with <code>disabled</code> is skipped, with everything inside it.</strong> Rename <code>Pack1</code> to <code>disabledPack1</code> and it stops loading. Rename it back and it loads again. That is the whole feature. Swap between packs by renaming folders, no deleting, no moving. The log says <code>DISABLED</code> and the folder name so you can see it was skipped on purpose. Turning a pack off needs a restart, because files already handed to the game stay there until FiveM closes. Suggested by a user, built the same day.</p>',
    '<p>If two packs ship the same file, the first one found wins and the log says <code>DUPLICATE</code> for the other.</p>',
    '<h3>One settings file</h3>',
    '<p>The marker files are gone. No more <code>_OFF</code>, <code>_debug.txt</code>, <code>_budget.txt</code> and guessing whether Windows hid the extension. The plugin writes <strong><code>tex_overrides/_settings.txt</code></strong> on first run. Every option is in it, switched off, with a plain English note above it saying what it does. Change <code>no</code> to <code>yes</code>, save, restart FiveM.</p>',
    '<pre><code>off = no\ndebug = no\ntexture_budget = 0\nauto_update = no\nno_update_check = no\nrefresh_key = f11\nhide_overlay = no</code></pre>',
    '<p>The file is <strong>only ever created, never rewritten</strong>, so your edits and any notes you add survive updates. That also means an existing install does not get a new option on its own: if a line is missing, add it yourself. If you still have old marker files, the first launch of 0.8.13 or later copies their values into the settings file, deletes them, and says in the log what it moved. Issue #20 by chocomintw.</p>',
    '<h3>FiveM\'s corner text, out of your screenshots</h3>',
    '<p>FiveM writes its version in one corner and "N mod packs loaded" in the other, and both end up in every picture you take. Set <code>hide_overlay = printscreen, f9</code> (whatever keys you take screenshots with) and those two lines come off the screen for a second and a half when you press one, then come back. <code>hide_overlay = always</code> keeps them off for the whole session.</p>',
    '<p>To be clear about what this is: <strong>your own screen only.</strong> The server sees nothing, other players see nothing, and the plugin writes into no code of FiveM\'s or the game\'s. It moves two entries on one of FiveM\'s own drawing lists and moves them back. The server\'s chat and HUD are not touched.</p>',
    '<p>One limit: the text comes off on the next frame, so a tool that grabs the screen in the same instant as the keypress can still catch it. ShareX, Steam and the like grab it a moment later and come out clean. Windows\' own PrintScreen to clipboard is the one that may not.</p>',
    '<h3>The update popup can update for you</h3>',
    '<p>Since 0.8.11 the popup\'s Yes button downloads the new version, checks it is a 64-bit DLL and that its SHA-256 matches what the build server published, and swaps it in. The new version loads next time FiveM starts. Your old file is kept beside it as <code>texoverride.asi.old</code>, so if the new one misbehaves, delete the new one and rename the old one back. <code>auto_update = yes</code> installs without asking. PR #18 by chocomintw.</p>',
    '<p><strong>If you are on 0.8.13 or 0.8.14 and it never offered 0.8.15</strong>, update by hand once. A hand-edited release page left the updater with nothing to check against. 0.8.15 reads the hash off the file GitHub publishes instead, so it does not happen again.</p>',
    '<h3>Props and weapons that came with GTA</h3>',
    '<p>Any <code>.ydr</code> or <code>.yft</code> put in <code>tex_overrides</code> is handed to the game under that exact name. A phone, a notepad, a police laptop, a door, a car or bike the game already has: same as replacing a weapon model. Tested on two props that ship with GTA (<code>prop_beer_bottle</code>, <code>prop_beer_logger</code>), and that settles a question from 0.8.8: <strong>a model slot the game already owns can be claimed</strong>, so weapons that came with GTA work too. Only animations have the server-only rule from the last post.</p>',
    '<p>Adding a brand new vehicle still needs the <code>mods</code> folder package it came with, because that needs <code>vehicles.meta</code> and <code>handling.meta</code>, which the plugin cannot read. Replacing an existing one does not.</p>',
    '<h3>Refresh key and live reload</h3>',
    '<p>Press <strong>F11</strong> in game and the plugin reads <code>tex_overrides</code> again straight away, the way SA modloader worked. <code>refresh_key</code> in the settings file takes any f1 to f12 key, a letter, a digit, or <code>off</code>. It always writes a line in the log, including "nothing has changed since the last look", so a key that finds nothing never looks like a key that does not work.</p>',
    '<p>Straight about what live reload does and does not do: the folder is watched and your changed file is read again, but <strong>if the game already has the old version loaded in memory it carries on drawing that.</strong> Editing a file the game has not loaded yet is the case that works. For anything already on your ped, restart FiveM.</p>',
    '<h3>Two you should update for on their own</h3>',
    '<p><strong>0.8.8: servers on an older game build.</strong> The plugin has to name every build it supports, and it only named the two newest, so on a server pinned to anything else FiveM refused to load it and nothing was written to the log at all. It now names every build from 2189 up. Reported by benzwxc on a build 3407 server (issue #10).</p>',
    '<p><strong>0.8.15: the startup crash 0.8.14 brought in.</strong> Several people saw the game die once or twice during loading before it would start. 0.8.14 signed up to FiveM\'s own per-frame events from a background thread a second into loading, which is the exact moment FiveM\'s own components are doing the same thing, and two threads editing the same list can lose an entry. It now signs up while the game is still being handed to FiveM, before any of that starts. If you are on 0.8.14, move.</p>',
    '<h3>The log tells you more</h3>',
    '<ul>',
    '<li><strong>How much of the texture budget the game is actually using</strong>, and a warning once it passes 90 percent. That is the number every "my textures went missing" report needed and nobody had.</li>',
    '<li><strong>What each file really costs in memory</strong>, including the textures it shares with other items. The old figure came from the file on disk, which charged a model nothing for the textures it pulls in with it.</li>',
    '<li><strong>Whose file the game actually read</strong> the first time each slot lands in memory. Holding a slot is only half the story: <code>LOADED ... from the GAME file</code> means the game loaded the original while a DLC mount owned the handle and then pinned it. Body skin is the case that prompted this.</li>',
    '<li><strong>Every line says how serious it is and which part wrote it</strong>, like <code>[WARN] [SCAN]</code>. Search for <code>WARN</code> and <code>ERROR</code> instead of reading all of it. Thanks to chocomintw.</li>',
    '<li><strong>The log could go silent for a whole session</strong> if any other program held it open, a log viewer, an upload in progress. Fixed in 0.8.9.</li>',
    '<li><strong>A file with no <code>RSC7</code> header is refused</strong> and named. Those are raw dumps, usually a texture renamed to <code>.ytd</code> or something pulled out with the wrong export option, and the game dies in its own loader the moment it streams one. Export with OpenIV.</li>',
    '<li><strong><code>debug = yes</code> lists every overridable server file</strong> instead of stopping at 500. Looking up the exact name of one server prop is the main reason to read that part of the log.</li>',
    '</ul>',
    '<h3>It says "Couldn\'t load texoverride.asi"</h3>',
    '<p>Two confirmed causes, neither of them the plugin. <strong>Smart App Control</strong>, a Windows 11 feature separate from the antivirus, blocks unsigned files with no notice anywhere, and antivirus exclusions do not apply to it. <strong>McAfee</strong> vetoes unsigned DLLs loading into the game with no notice either. The README has a section on this right after Install, with a PowerShell test that reports the real Windows error, and there is now a "Plugin will not load" report form that asks the three questions up front.</p>',
    '<h3>Also, briefly</h3>',
    '<ul>',
    '<li><strong>Tattoo placement files with one or two presets work now</strong>, and so do files where you edited every value. The layout only has to be worked out once per session.</li>',
    '<li><strong><code>off = yes</code> really is off</strong>: the plugin returns before it creates a log, scans for anything or installs anything. By chunguscodes (#14).</li>',
    '<li><strong>Big folder copies while the game runs no longer stall it.</strong> Live reload paces itself. By chunguscodes (#13).</li>',
    '<li><strong>The README is a third of the length</strong> and the long parts moved to <code>docs/</code>, so the answer you need is nearer the top.</li>',
    '<li>The plugin is a <code>src/</code> tree now instead of one file, which is why outside fixes come in faster. PR #17 by chocomintw.</li>',
    '</ul>',
    '<p>Thanks to <strong>chocomintw</strong> for the weapons, the log levels, the split, the updater and the settings issue, <strong>chunguscodes</strong> for the off switch and the live reload pacing, <strong>benzwxc</strong> for the build report, and the user who asked for the disabled folders.</p>',
    '<hr>',
    '<p>Source is all there if you want to read what it actually does.</p>',
    '<p><a href="https://github.com/blancodagoat/texoverride/releases/tag/v0.8.19" rel="external nofollow">github.com/blancodagoat/texoverride</a></p>'
  ].join(''), { callback: () => console.log('inserted, ' + ed.getData().length + ' chars') });
})();
