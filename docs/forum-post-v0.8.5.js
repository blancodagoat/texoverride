// Forum announcement post for texoverride v0.8.5, the release that stops caring about folder
// names and starts caring about file names.
//
// Same as the other posts in this folder: the cfx.re reply box is a CKEditor instance, so open
// the reply box, paste this whole thing into the browser console, and it fills the box in.
// It looks the editor up rather than naming an instance id, because the id is per topic.

(() => {
  const ck = window.CKEDITOR;
  const ed = ck && (ck.currentInstance || ck.instances[Object.keys(ck.instances)[0]]);
  if (!ed) return console.error('No CKEditor instance found. Click into the reply box first.');
  ed.setData([
    '<p><strong>texoverride 0.8.5 is out: your server\u2019s own characters and animals work now</strong></p>',
    '<p>Until now the plugin only accepted folders whose names came with GTA. If your server added its own dog, its own cat, its own anything, the plugin refused the files before it had even read them. That was a real wall, and a stupid one, because a server can name a model whatever it likes and no rule was ever going to guess it.</p>',
    '<p>The folder name is not what decides any more. What decides is whether the files inside are named the way GTA names body parts:</p>',
    '<pre>tex_overrides\caninesd\head_000_r.ydd\ntex_overrides\caninesd\head_diff_000_a_whi.ytd</pre>',
    '<p>Name the folder after the model and put the parts in. That is the whole thing. This was tested on a server whose dogs are called <strong>canine</strong>, <strong>caninepd</strong>, <strong>caninesd</strong> and <strong>caninefd</strong>, none of which exist in GTA at all, and it works.</p>',
    '<p>If you do not know what a model is called, start the game once and read the log. It lists every collection the server uses.</p>',
    '<p><strong>Nothing unsafe got easier.</strong> A vehicle texture, a prop or a map file is still refused no matter what folder you put it in, because none of them are named like body parts. That is the real job the old folder list was doing, and it is the part that stayed. Story and cutscene characters are still refused by name.</p>',
    '<p><strong>The other half of this release is a crash fix.</strong> A few people had the game die at startup after adding an animal mod, and the file doing it was the <strong>.ymt</strong>. Here is why, because it is worth knowing before you go looking for it in your own pack.</p>',
    '<p>A .ymt with a name the game has never seen registers perfectly happily. Servers push about a dozen of their own through the exact same code path every session and nothing goes wrong. A .ymt with a name the game <em>already owns</em> kills the game outright. Every animal ships one, so an animal mod\u2019s .ymt always collides, and it can never win. Those eight names are refused now, and the log tells you so instead of the game closing on you.</p>',
    '<p>What that costs you: parts a mod <em>added</em> on top of the original animal stay unpickable. Everything the mod <em>replaced</em> works fine. Every other .ymt name is accepted now, which is what a clothing pack needs and was blocked before for no good reason.</p>',
    '<p>Also in the last few releases, if you skipped them:</p>',
    '<ul>',
    '<li>If your Windows username has a non English letter in it, the plugin did nothing at all. Not one override, no error, a log that looked completely healthy. Turkish, Hungarian, Polish, Chinese, anything outside plain A to Z. Fixed in 0.8.3, and it is worth updating for on its own.</li>',
    '<li>A file that crashes the game now costs you one launch instead of every launch. The plugin writes down which file the game is holding, and if the game dies right then, the next start skips that one file and tells you which it was.</li>',
    '<li>Files over 32 MB load again. They used to be refused outright, which left packs half applied with nothing on screen to explain it. The log still names every one of them so you know where to look if things go wrong.</li>',
    '</ul>',
    '<p>Source is all there if you want to read what it actually does.</p>',
    '<p><a href="https://github.com/blancodagoat/texoverride/releases/tag/v0.8.5" rel="external nofollow">github.com/blancodagoat/texoverride</a></p>'
  ].join(''), { callback: () => console.log('inserted, ' + ed.getData().length + ' chars') });
})();
