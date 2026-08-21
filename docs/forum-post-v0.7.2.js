// Forum announcement post for texoverride v0.7.2, the release about textures going missing.
//
// The cfx.re reply box is a CKEditor instance, so the way to post this without fighting the
// editor is to paste this whole thing into the browser console with the reply box open. It
// finds the editor itself rather than naming an instance id, because the id is per topic and
// a hardcoded one goes stale the moment you post somewhere else.
//
// Saved here because the original was written in a chat session and then lost, and had to be
// recovered by hand. Keep new posts in this folder.

(() => {
  const ck = window.CKEDITOR;
  const ed = ck && (ck.currentInstance || ck.instances[Object.keys(ck.instances)[0]]);
  if (!ed) return console.error('No CKEditor instance found. Click into the reply box first.');
  ed.setData([
    '<p><strong>texoverride 0.7.2 is out, and this one is about textures going missing</strong></p>',
    '<p>If you have ever had the game drop to blurry low detail, or walls turn black, or textures just stop loading until you restart, read this bit.</p>',
    '<p>That happens because the game only sets aside so much memory for textures, and once it fills up it does not clean up after itself properly. Here is the part that surprised me: the amount it sets aside has nothing to do with your graphics card. It is about 2.9 GB out of the box, and about 7.8 GB if you max the Extended Texture Budget slider in the settings. Those are the only two numbers there are. A 24 GB card gets the exact same 7.8 GB as an 8 GB card.</p>',
    '<p>So people kept telling me they had a strong PC and it was still happening, and they were right. It was never about their hardware.</p>',
    '<p>This version changes that. On startup the plugin asks Windows how much video memory it can actually spare, keeps some back for the rest of the game, and raises the ceiling to whatever is left. My own machine has a 12 GB card and went from 7.8 GB to 8.8 GB. A 24 GB card gets around 21 GB. There is nothing to install and nothing to turn on. If your card has nothing spare, it leaves everything alone and says so in the log.</p>',
    '<p>Max the Extended Texture Budget slider as well. It is free and the plugin builds on top of whatever it finds.</p>',
    '<p>If you want to pick the number yourself, put a file called <strong>_budget.txt</strong> in your <strong>tex_overrides</strong> folder with just a number of GB in it, like 8. Put a 0 in it instead to switch the whole thing off.</p>',
    '<p>This buys you more room before the problem hits. It does not delete the problem, which lives inside GTA itself, and it cannot make a pack fit that is simply too big. If your log lists files under <strong>HEAVY</strong>, those are still worth shrinking. A single 4K clothing texture can eat 64 MB on its own, and vanilla clothing sits under 2 MB.</p>',
    '<p>Smaller things in this one:</p>',
    '<ul>',
    '<li>The log stopped pretending a better graphics card would have saved you, and now tells you what the game is actually giving you and whether your pack fits in it.</li>',
    '<li>When you add a file while the game is running and it cannot be picked up, the log now says why instead of just failing. Usually it is because the server already has that name loaded, and the game will not hand a name over until it restarts. Editing files the plugin already uses still updates live, same as always.</li>',
    '<li>The file itself now has proper details on it, so you can right click it and see what it is and where it came from.</li>',
    '<li>If your antivirus calls it a trojan, there is a new section in the readme explaining why that happens and what you can do. Short version: it patches the running game, which is what every trainer and mod loader does, and scanners flag that on sight. Releases now also come with a checksum and a signed record proving the file came from the repo, so you can check a download is the real one before running it.</li>',
    '</ul>',
    '<p><strong>If you grabbed 0.7.0 earlier today, replace it.</strong> The memory check in that one ran too early in startup. On most PCs it just quietly did nothing, on one it crashed the plugin, and on another it upset a separate upscaling plugin badly enough to take the whole game down, which looked like that other plugin was at fault. 0.7.1 moved the check to a safe point and 0.7.2 makes sure that even if it ever does fail, the worst it can cost you is this one feature.</p>',
    '<p>As always the source is all there if you want to read what it actually does.</p>',
    '<p><a href="https://github.com/blancodagoat/texoverride/releases/tag/v0.7.2" rel="external nofollow">github.com/blancodagoat/texoverride</a></p>'
  ].join(''), { callback: () => console.log('inserted, ' + ed.getData().length + ' chars') });
})();
