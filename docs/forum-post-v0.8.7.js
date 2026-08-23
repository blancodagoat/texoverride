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
    '<p><strong>texoverride 0.8.7 is out, and this one does animations</strong></p>',
    '<p>Same idea as the clothes and tattoos: you put a file in a folder and your game plays your version of it. Nobody else sees anything. The file for an animation is a <strong>.ycd</strong>, which the game calls a clip dictionary, and it goes loose in <code>tex_overrides</code> with no folder around it.</p>',
    '<p><strong>One rule decides whether your pack can work, so read this bit first.</strong></p>',
    '<p>The plugin can replace an animation your <strong>server</strong> loads. It cannot replace one that came with GTA. From the outside those look exactly the same, and the difference is not something you can guess by looking at the file.</p>',
    '<p>The reason is plumbing. A server file passes through the same call the plugin listens on, so the plugin swaps it as it goes by. A file that came with GTA never makes that call, so there is nothing to swap. Clothes and textures are not like this, the plugin does reach the base game for those. Animations are the exception.</p>',
    '<p><strong>How to set up a client side animation pack</strong></p>',
    '<p>1. Start the game once, join your server, then open <code>plugins\\texoverride.log</code>. Every animation the server loads is listed:</p>',
    '<pre>collection: gtawpl_1.ycd                 [overridable]\ncollection: agangsign2@animation.ycd     [overridable]</pre>',
    '<p>If the animation you want is in that list, you can replace it. If it is not there, it came with GTA, and no amount of building will make it work.</p>',
    '<p>2. Find out which of those your emote actually uses, and what the clip inside is called. Most servers publish an animation list with both, and it usually reads something like <em>emote name, dictionary, clip</em>. You need the last two.</p>',
    '<p>3. Build the .ycd. Open a dictionary that already has a clip with the right name, swap in the animation you want, save. OpenIV or CodeWalker both do this. The name of the clip inside matters as much as the name of the file, because that is what gets asked for.</p>',
    '<p>4. Name the file exactly after the dictionary and drop it in:</p>',
    '<pre>tex_overrides/gtawpl_1.ycd</pre>',
    '<p>5. Restart the game and play the emote.</p>',
    '<p><strong>A worked example.</strong> On GTA World the gang sign emotes numbered 33 and up all use dictionaries called <code>gtawpl_1</code> through <code>gtawpl_24</code>, and each asks for a clip called <code>idle_a</code>. So nine custom gang signs meant nine files named <code>gtawpl_1.ycd</code> to <code>gtawpl_9.ycd</code>, each holding one animation under the clip name <code>idle_a</code>. That is the whole pack. The same nine animations aimed at the dictionaries GTA ships did nothing at all, which is the rule above in one sentence.</p>',
    '<p><strong>One thing that will catch you out.</strong> Replacing a dictionary replaces all of it. If the one you are copying held three clips and yours holds one, the other two stop existing, and anything that played them stops working. Keep the ones you are not changing.</p>',
    '<p><strong>The log got a lot more useful too</strong>, because working the above out the first time was mostly guesswork. It now tells you what the game resolves each of your file names to, counts your files by type instead of giving up after sixty lines, reads the pool back after claiming to check the game really points at your files, and reports every second how many of your files the game is holding and how many something keeps taking back off you.</p>',
    '<p>If something does not work, that log is the thing to post.</p>',
    '<p>Also in this one: less of the plugin\'s work happens before the game is allowed to start, so a big pack no longer sits in front of the loading screen. Thanks to chunguscodes for that and for the occupied slot work.</p>',
    '<p>Source is all there if you want to read what it actually does.</p>',
    '<p><a href="https://github.com/blancodagoat/texoverride/releases/tag/v0.8.7" rel="external nofollow">github.com/blancodagoat/texoverride</a></p>'
  ].join(''), { callback: () => console.log('inserted, ' + ed.getData().length + ' chars') });
})();
