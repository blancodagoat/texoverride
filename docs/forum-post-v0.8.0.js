// Forum announcement post for texoverride v0.8.0, the release that adds animals.
//
// Same as the other posts in this folder: the cfx.re reply box is a CKEditor instance, so open
// the reply box, paste this whole thing into the browser console, and it fills the box in.
// It looks the editor up rather than naming an instance id, because the id is per topic.

(() => {
  const ck = window.CKEDITOR;
  const ed = ck && (ck.currentInstance || ck.instances[Object.keys(ck.instances)[0]]);
  if (!ed) return console.error('No CKEditor instance found. Click into the reply box first.');
  ed.setData([
    '<p><strong>texoverride 0.8.0 is out, and this one does animals</strong></p>',
    '<p>Up to now the plugin only replaced clothing and tattoos on your own character. It now does animals too, so you can put your own dog on the server without asking anyone to add anything server side.</p>',
    '<p>Eight animals are built the same way your character is, out of a folder of parts:</p>',
    '<p><strong>chop, husky, mtlion, panther, retriever, rottweiler, sharktiger, shepherd</strong></p>',
    '<p>Most animal mods you can download are already laid out the way the plugin wants, so the folder goes straight into <strong>tex_overrides</strong> and that is the whole install:</p>',
    '<pre>tex_overrides\\a_c_shepherd\\head_000_r.ydd\ntex_overrides\\a_c_shepherd\\head_diff_000_a_whi.ytd</pre>',
    '<p>Those mods usually also come with two loose files, <strong>a_c_shepherd.yft</strong> and <strong>a_c_shepherd.ymt</strong>. Both are accepted now, and they go in <strong>tex_overrides</strong> itself, not inside the animal folder. Do not skip the .ymt. It is the file that tells the game which parts and textures exist, so without it anything the mod added on top of the original animal cannot be picked and the mod looks half finished.</p>',
    '<p>Every other animal is one single model instead of a folder of parts. Pug, poodle, westy, cat, coyote, deer, cow, pig, rabbit, rat, the birds and the fish. Those have no folder at all, the files go straight into <strong>tex_overrides</strong>.</p>',
    '<p>One thing to know before you go looking: a model built on a different skeleton than the original animal will not work, because the skeleton lives in a part of the game files this plugin cannot reach. Retextures and remodels that keep the original skeleton are fine, and that is nearly every animal mod out there.</p>',
    '<p>Other things in this one:</p>',
    '<ul>',
    '<li>Releases now come as a zip as well. It holds the plugin plus a ready made <strong>tex_overrides</strong> with a folder already created for every collection you can use, so nobody has to guess a name or spell one. The plugin on its own is still there for upgrading, so your own folder is left alone.</li>',
    '<li>The log now says whether the game had to wait for the plugin at startup, and for how long. On a big pack that one line is the whole answer to "is it faster now", because the scan mostly runs while the game is starting anyway.</li>',
    '<li>Nothing else got easier to touch. Story characters, vehicles, weapons, props, maps and scripts are refused exactly as before, and there is now a test that checks that on every build.</li>',
    '</ul>',
    '<p>Source is all there if you want to read what it actually does.</p>',
    '<p><a href="https://github.com/blancodagoat/texoverride/releases/tag/v0.8.0" rel="external nofollow">github.com/blancodagoat/texoverride</a></p>'
  ].join(''), { callback: () => console.log('inserted, ' + ed.getData().length + ' chars') });
})();
