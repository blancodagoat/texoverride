// Forum post announcing the dlc.rpf to tex_overrides converter and the GTAW animation fixer on
// blancodagoat.dev/dlc-builder, for the texoverride topic.
//
// Same as the other posts in this folder: the cfx.re reply box is a CKEditor instance, so open
// the reply box, paste this whole thing into the browser console, and it fills the box in.
// It looks the editor up rather than naming an instance id, because the id is per topic.

(() => {
  const ck = window.CKEDITOR;
  const ed = ck && (ck.currentInstance || ck.instances[Object.keys(ck.instances)[0]]);
  if (!ed) return console.error('No CKEditor instance found. Click into the reply box first.');
  ed.setData([
    '<h2>Turn any dlc.rpf into a tex_overrides folder, in your browser</h2>',
    '<p>Most of the questions in this topic are the same question: <em>I have a clothing pack, it is a dlc.rpf, how do I get it into tex_overrides?</em> Until now the answer was OpenIV, a lot of right clicking, and knowing which files the plugin will and will not take. Now it is one page.</p>',
    '<p><a href="https://blancodagoat.dev/dlc-builder/#tex-override" rel="external nofollow">blancodagoat.dev/dlc-builder</a>, the tool called <strong>dlc.rpf to tex_overrides</strong>.</p>',
    '<h3>What it does</h3>',
    '<ol>',
    '<li><p><strong>Drop the dlc.rpf.</strong> Clothing, weapons, tattoos, animations, any pack. It reads the pack\'s index and shows you what it found: what it will keep, what it will drop, and why for every dropped file.</p></li>',
    '<li><p><strong>Press write.</strong> A popup lists everything one more time, then either you pick your <code>tex_overrides</code> folder and the files land in it directly (Chrome and Edge), or you get a zip to unpack next to the plugin (other browsers).</p></li>',
    '<li><p><strong>Restart FiveM.</strong> That is it.</p></li>',
    '</ol>',
    '<p>Clothing comes out sorted into its collection folder, <code>mp_m_freemode_01_whatever/jbib_000_u.ydd</code>, weapons and overlays at the root, exactly the layout the plugin wants. Nothing is converted or re-saved. Each file comes out byte for byte as the game would have read it from the pack, so a file that worked in the pack works here.</p>',
    '<h3>Nothing is uploaded and there is no size limit</h3>',
    '<p>The pack never leaves your machine and never even loads into the page. The tool reads a few kilobytes of index and then copies each file straight from the pack on your disk to the folder you picked. A <strong>1.6 GB weapon pack takes no more memory than a 7 MB one</strong>. It has been run over sixty packs from 0 to 3 GB, and against the files CodeWalker extracts from the same packs, and every sampled file matched byte for byte.</p>',
    '<h3>Encrypted packs work too</h3>',
    '<p>OpenIV saves packs NG-encrypted by default, and that used to be a dead end without OpenIV. The keys that open those packs are never shipped anywhere; <strong>they live in your own GTA5.exe</strong>. So when a pack needs them, the page asks for that file. It is read in your browser, the 32 bytes that matter are kept in your browser only, the exe itself is thrown away, and from then on encrypted packs convert like open ones. It takes about half a minute the first time and nothing the second. It has to be the normal GTA5.exe, the one FiveM uses; the Enhanced edition carries different keys.</p>',
    '<h3>It uses the plugin\'s own rules</h3>',
    '<p>The tool keeps a file only if the plugin would, so nothing in the output gets refused at startup. Every dropped file is listed with the reason. Some you will see:</p>',
    '<ul>',
    '<li><strong>Story and cutscene characters</strong> are never touched, same as the plugin.</li>',
    '<li><strong>A .ydr that is not a weapon</strong> (props, vehicles, map pieces) is dropped.</li>',
    '<li><strong>An animation that shipped with GTA</strong> is dropped, because the plugin cannot replace one of those however it is built. The tool knows all 23,000 of them by name, so a pack that replaces the game\'s own animations tells you so up front instead of after an evening of testing.</li>',
    '<li><strong>A pack that is really a map or vehicle pack</strong> gets a warning. Its textures pass the plugin\'s rules, but the plugin cannot add maps or vehicles and it is better to hear that now.</li>',
    '</ul>',
    '<p>It also catches two kinds of broken pack: an inner archive marked open whose contents are still encrypted (the files would be garbage, so they are left out and the archive is named), and a nested rpf with more names than the format was built for, which every tool including CodeWalker used to refuse.</p>',
    '<h3>The GTAW animation fixer</h3>',
    '<p>Same page, one tool up. Drop an animation pack\'s <code>.ycd</code> and it works out which GTA World emote it is, renames the clip inside to the one GTAW asks for, and hands the file back named as the right dictionary. Two names have to match and they live in different places, and renaming the file only fixes one of them; that is why a renamed animation stands the ped still.</p>',
    '<p>It now also knows which of GTAW\'s emotes play a dictionary that <strong>ships with GTA</strong>. Those are marked and cannot be picked, because the plugin cannot replace them. Most of the list is like that. The ones you can retarget to are the server\'s own packs, <code>gtawpl_1</code> to <code>gtawpl_24</code> for the gang signs, for example.</p>',
    '<h3>If something does not work in game</h3>',
    '<p>Both tools have a <strong>Report a problem</strong> button that stays on the page after the download, because you find out in game, not before. It sends the full picture: what was dropped in, every file the pack held, what was kept or dropped and why, and what was written. You see exactly what goes before you press send, there is nothing typed, and it carries no account and no file contents.</p>',
    '<hr>',
    '<p>Source is all there if you want to read what it actually does.</p>',
    '<p><a href="https://github.com/blancodagoat/texoverride" rel="external nofollow">github.com/blancodagoat/texoverride</a> for the plugin, <a href="https://github.com/blancodagoat/blancodagoat.dev" rel="external nofollow">github.com/blancodagoat/blancodagoat.dev</a> for the tools.</p>'
  ].join(''), { callback: () => console.log('inserted, ' + ed.getData().length + ' chars') });
})();
