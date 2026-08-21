// The main forum post: the index of everything blancodagoat makes, not tied to one project.
//
// Paste into the browser console with the reply box open. It finds the CKEditor instance
// itself rather than naming an id, because the id is per topic.
//
// Lives in this repo because texoverride is the flagship, not because the post is about it.
// Two things to keep true when editing:
//   - No version numbers in the text. A pinned post goes stale the moment something ships,
//     so only the release links carry versions.
//   - FiveM and GTA tools stay at the top. That is who reads the forum it is posted on.

(() => {
  const ck = window.CKEDITOR;
  const ed = ck && (ck.currentInstance || ck.instances[Object.keys(ck.instances)[0]]);
  if (!ed) return console.error('No CKEditor instance found. Click into the reply box first.');
  ed.setData([
    '<p><strong>Everything I make, in one place</strong></p>',
    "<p>I build small tools, mostly for GTA and FiveM, plus a couple of Windows things that got out of hand. <strong>All of them are free</strong>, nothing is paywalled, and anything that runs on your own machine has its source public so you can read what it does before you run it. I will keep this post updated as things change.</p>",
    '<p><strong>For FiveM and GTA V</strong></p>',
    '<ul>',
    '<li><strong><a href="https://github.com/blancodagoat/texoverride" rel="external nofollow">texoverride</a></strong><br>Wear your own clothes and textures without the server having them. Drop your own .ydd and .ytd files in a folder and your own game draws them on your ped. No archive edits, nothing sent to the server, and other players see nothing. It also raises the texture memory ceiling the game runs on, which is the thing behind textures going blurry or black until you restart.</li>',
    '<li><strong><a href="https://github.com/blancodagoat/autogrammar" rel="external nofollow">autogrammar</a></strong><br>A red squiggly line under misspelled words in the chat box, the same way your browser marks them anywhere else. It never changes what you typed and it sends nothing anywhere. Works with whatever chat script your server runs, custom ones included.</li>',
    '<li><strong><a href="https://blancodagoat.dev/dlc-builder/" rel="external nofollow">DLC Builder</a></strong><br>Build GTA V add-on DLCs entirely in your browser. Nothing to install and nothing to learn first.</li>',
    '<li><strong><a href="https://blancodagoat.dev/chatlog-magician/" rel="external nofollow">Chatlog Magician</a></strong><br>Turns roleplay chatlogs into properly formatted screenshots. Free forever, and I mean that one literally.</li>',
    '</ul>',
    "<p>The first two are plugins, so a heads up: they <strong>only load on servers with pure mode off</strong>. That is FiveM's rule for every plugin of that kind and there is nothing I can do about it from my end.</p>",
    '<p><strong>Other things I make</strong></p>',
    '<ul>',
    '<li><strong><a href="https://github.com/blancodagoat/DejaVu" rel="external nofollow">DejaVu</a></strong><br>Instant replay for Windows. It keeps a rolling 5 to 25 minutes in the background and <strong>one key saves the clip</strong> after something happens. Crash safe, keeps Discord voices out of the recording, AV1, one small exe.</li>',
    '<li><strong><a href="https://github.com/blancodagoat/memento" rel="external nofollow">Memento</a></strong><br>Screenshots that stay out of your way. Tray only, about 8 MB of RAM, no uploads and no telemetry.</li>',
    '<li><strong><a href="https://blancodagoat.dev/discord/" rel="external nofollow">BlancoKit</a></strong><br>35 Discord tools in one place.</li>',
    '</ul>',
    '<p>Everything else lives at <a href="https://blancodagoat.dev/" rel="external nofollow">blancodagoat.dev</a>.</p>',
    '<p><strong>Discord: <a href="https://discord.gg/KHhZsYFGrU" rel="external nofollow">discord.gg/KHhZsYFGrU</a></strong><br>Best place to reach me. Bug reports, feature requests, or just tell me something is broken and I will usually get to it quickly.</p>',
    '<p>If one of these saved you some time and you feel like it, <a href="https://ko-fi.com/Q5Q668VS3" rel="external nofollow">ko-fi.com/Q5Q668VS3</a>. Never expected, and nothing here is locked behind it.</p>'
  ].join(''), { callback: () => console.log('inserted, ' + ed.getData().length + ' chars') });
})();
