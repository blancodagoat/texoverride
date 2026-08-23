// Forum announcement post for texoverride v0.8.7, the release that does animations.
//
// Same as the other posts in this folder: the cfx.re reply box is a CKEditor instance, so open
// the reply box, paste this whole thing into the browser console, and it fills the box in.
// It looks the editor up rather than naming an instance id, because the id is per topic.

(() => {
  const ck = window.CKEDITOR;
  const ed = ck && (ck.currentInstance || ck.instances[Object.keys(ck.instances)[0]]);
  if (!ed) return console.error('No CKEditor instance found. Click into the reply box first.');
  ed.setData([
    '<h2>texoverride 0.8.7: animations</h2>',
    '<p>Same idea as the clothes and the tattoos. You drop a file in a folder, your game plays your version, and nobody else sees a thing. The file for an animation is a <code>.ycd</code>, which the game calls a <em>clip dictionary</em>, and it goes loose in <code>tex_overrides</code> with no folder around it.</p>',
    '<p>Animations came with one surprise, though, and it is the difference between a pack that works and a pack that does nothing at all. So that goes first.</p>',
    '<h3>The one rule</h3>',
    '<blockquote><p>The plugin can replace an animation your <strong>server</strong> loads.<br>It cannot replace one that came with GTA.</p></blockquote>',
    '<p>From the outside those two look identical. Nothing about the file tells you which is which, and building the pack more carefully will not change the answer.</p>',
    '<table><thead><tr><th>Where the animation comes from</th><th>Can you replace it</th></tr></thead><tbody><tr><td>Your server streams it</td><td><strong>Yes</strong></td></tr><tr><td>It shipped with GTA</td><td>No</td></tr></tbody></table>',
    '<p>The reason is plumbing. A server file passes through the same call the plugin listens on, so the plugin swaps the path as it goes past. A file that shipped with GTA never makes that call, so there is nothing to swap. Clothes and textures are not like this, the plugin reaches the base game fine for those. Animations are the exception, and now you know before you spend an evening on it.</p>',
    '<h3>Setting up an animation pack</h3>',
    '<ol>',
    '<li><p><strong>Find out if your animation is even eligible.</strong> Start the game once, join your server, then open <code>plugins</code> and read <code>texoverride.log</code>. Every animation the server loads is in there:</p><pre><code>collection: gtawpl_1.ycd                 [overridable]\ncollection: agangsign2@animation.ycd     [overridable]</code></pre><p>In the list, you are fine. Not in the list, it came with GTA, and stop here.</p></li>',
    '<li><p><strong>Get the dictionary and the clip name.</strong> An animation is asked for by two names, not one: the dictionary (the file) and the clip (inside the file). Most servers publish a list with both, usually laid out as emote, dictionary, clip. You need the last two.</p></li>',
    '<li><p><strong>Build the .ycd.</strong> Open a dictionary that already contains a clip with the right name, swap in the animation you want, save. OpenIV and CodeWalker both do this. The clip name matters as much as the file name, because the clip name is what gets asked for.</p></li>',
    '<li><p><strong>Name it after the dictionary, exactly, and drop it in:</strong></p><pre><code>tex_overrides/gtawpl_1.ycd</code></pre></li>',
    '<li><p><strong>Restart and play the emote.</strong></p></li>',
    '</ol>',
    '<h3>A worked example</h3>',
    '<p>On GTA World the gang sign emotes from 33 upward use dictionaries called <code>gtawpl_1</code> through <code>gtawpl_24</code>, and every one of them asks for a clip called <code>idle_a</code>. So a pack of eight custom gang signs is eight files:</p>',
    '<pre><code>tex_overrides/gtawpl_1.ycd     &lt;- gangsign33\ntex_overrides/gtawpl_2.ycd     &lt;- gangsign34\ntex_overrides/gtawpl_3.ycd     &lt;- gangsign35\n...                            each holding one animation, clip named idle_a</code></pre>',
    '<p>That is the entire pack. Worth saying plainly: the <em>same eight animations</em> aimed at the dictionaries GTA ships did absolutely nothing, with a log that looked perfectly healthy the whole time. That is the rule at the top, in one sentence.</p>',
    '<h3>Three things that will catch you out</h3>',
    '<ul>',
    '<li><strong>Replacing a dictionary replaces all of it.</strong> If the one you copied held three clips and yours holds one, the other two stop existing and anything that played them breaks. Keep the clips you are not changing.</li>',
    '<li><strong>Clip names are matched by number, not by spelling.</strong> Tools often show a clip under a label left over from whoever built it, while the name the game really uses is something else entirely. A pack that looks wrongly named and still works is not a mystery, that is why.</li>',
    '<li><strong>Check your pack for duplicates.</strong> Two files can hold the same animation, and then two emotes look identical and you go hunting for a bug that is not there. Ask me how I know.</li>',
    '</ul>',
    '<h3>The log tells you far more now</h3>',
    '<p>Working the above out the first time was mostly guesswork, so the log stopped being polite about it. It now reports what the game resolves each of your file names to, counts your files by type instead of giving up after sixty lines, reads the pool back after claiming to check the game really points at your files, and tells you every second how many of your files the game is holding and how many something keeps taking back off you:</p>',
    '<pre><code>slot lookup for .ycd: ok\n  .ycd     8 file(s), 0 with no slot at all\nverify: 370 slot(s) now point at your file, 0 still point at the game\'s\nalive (beat 10) - slotsHeld=370 contested=0 inMemory=48</code></pre>',
    '<p>If something does not work, that is the thing to post.</p>',
    '<h3>Everything since the 0.8.0 post</h3>',
    '<p>Seven releases went by since the last announcement and a couple of them matter a lot more than a version number suggests. If you skipped any, these are the ones to know about.</p>',
    '<p><strong>0.8.3 is the one to update for on its own.</strong> If your Windows username has a non English letter in it, Turkish, Hungarian, Polish, Chinese, anything outside plain A to Z, the plugin did <em>nothing at all</em>. Not one override. No error. A log that listed every file as loaded while none of them were on your character. The plugin wrote your folder path one way and FiveM read it another. Found by <strong>akaloi</strong> in issue #2, who pinned it down by making a second Windows account with a plain English name and watching the same files work immediately.</p>',
    '<p><strong>0.8.5 opened up your server\'s own characters and animals.</strong> The plugin used to only accept folders whose names came with GTA, so a dog your server added as <code>caninesd</code> was refused before anything was read. The folder name no longer decides. What decides is whether the files inside are named the way GTA names body parts:</p>',
    '<pre><code>tex_overrides/caninesd/head_000_r.ydd\ntex_overrides/caninesd/head_diff_000_a_whi.ytd</code></pre>',
    '<p>Name the folder after the model, put the parts in, done. Nothing unsafe got easier: a vehicle texture, a prop or a map file is still refused whatever folder you put it in, because none of them are named like body parts. That was the real job the old folder list was doing. Story and cutscene characters are still refused by name.</p>',
    '<p><strong>0.8.1 and 0.8.2 are the crash protection.</strong> A file that kills the game now costs you one launch instead of every launch: the plugin writes down which file the game is holding, and if the game dies right then, the next start skips that one file and tells you which it was. 0.8.2 is there because the first version of that quietly did not work. FiveM catches the crash, shows its report window, then closes down tidily, and on the way out the plugin was wiping the note it had just written. Delete <code>_quarantine.txt</code> in <code>tex_overrides</code> to give a skipped file another go.</p>',
    '<p><strong>Also worth knowing, briefly:</strong></p>',
    '<ul>',
    '<li><strong>Files over 32 MB load again</strong> (0.8.1). They used to be refused outright, which left packs half applied with nothing on screen to explain it. The log still names every one of them on a <code>HUGE</code> line, and that is still the first place to look if you crash.</li>',
    '<li><strong>Animal <code>.ymt</code> files are refused, and the log says why</strong> (0.8.5). The game already owns those names and will not hand one over, and the call that would replace it takes the game down. Every animal ships one, so it can never work. The rest of an animal mod still loads. Every <em>other</em> <code>.ymt</code> name is accepted now, which is what a clothing pack needs.</li>',
    '<li><strong>A failed hook install stops the plugin</strong> (0.8.6), instead of carrying on and crashing on the first file it touched.</li>',
    '<li><strong>Copying a large folder in while the game runs no longer stalls it</strong> (0.8.6), and a change is never dropped because the previous batch is still going.</li>',
    '<li><strong>Builds are reproducible</strong> (0.8.6). The same source produces the same bytes, and the build server builds twice and compares before publishing. You can build it yourself and check your file matches the release, which is the only real answer to \'is this download actually the code above\'.</li>',
    '<li><strong><code>docs/ped_collections.tsv</code></strong> (0.8.2) lists every collection the game ships, all 469 of them, read out of the game files. If a folder name is not in there and your server did not add it, that is your answer.</li>',
    '</ul>',
    '<h3>Also in this release</h3>',
    '<ul>',
    '<li>Less of the plugin\'s work happens before the game is allowed to start, so a big pack no longer sits in front of the loading screen.</li>',
    '<li>A file the server or a DLC already has loaded can be taken over mid session in more cases than before.</li>',
    '<li>Thanks to <strong>chunguscodes</strong> for both of those.</li>',
    '</ul>',
    '<hr>',
    '<p>Source is all there if you want to read what it actually does.</p>',
    '<p><a href="https://github.com/blancodagoat/texoverride/releases/tag/v0.8.7" rel="external nofollow">github.com/blancodagoat/texoverride</a></p>'
  ].join(''), { callback: () => console.log('inserted, ' + ed.getData().length + ' chars') });
})();
