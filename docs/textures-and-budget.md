# Texture memory and the budget

## Textures gone, everything stuck on low detail

On busy servers GTA sometimes gets stuck like this: buildings turn into grey blobs, textures
vanish, and only a game restart fixes it. That happens when the game's texture memory runs out.
The game never frees memory ahead of time, so once the budget is full it stays full. Heavy
servers can hit this on their own, with no mods at all.

Big override files make it worse. A texture saved at 4K, or saved without compression, can cost
the game 20 to 90 MB where the original cost 1 MB. A few of those on screen and the budget dies.

The plugin now measures this for you. At startup the log prints a line like
`pack cost when fully loaded: 240.0 MB of texture memory`, and below it a `HEAVY` line for every
file that costs 8 MB or more. Those files are the ones to fix: open them in a tool like
OpenIV or CodeWalker, resize the textures to what the original used (clothing is usually
512 to 1024 pixels), and save them DXT compressed. Smaller files look nearly identical on a
character and leave the rest of the game room to breathe.

If it still happens with a light pack, it is the server, not you. You do not need to touch the
Extended Texture Budget slider for it: the plugin already raises that ceiling for you at startup,
and puts it back every time the settings screen overwrites it. Lowering Texture Quality one step
still helps.

### The budget, and why a good graphics card does not save you

The Extended Texture Budget slider does not set a size. It multiplies a fixed base of about 2.9 GB,
and at its maximum setting it lands at about 7.8 GB. Those are the only two numbers that matter,
and your graphics card is in neither of them. A 24 GB card gets the same 7.8 GB ceiling as an 8 GB
card, which is why this bug shows up on expensive builds too and why maxing the slider is often not
enough on its own.

The plugin raises the ceiling for you, so the slider is not something you have to think about. It
is still worth maxing on a smaller card, because the plugin only ever raises and never lowers: on
an 8 GB card a maxed slider lands at 7.8 GB, which is higher than the 6 GB the plugin would pick,
so the plugin leaves the bigger number alone. On a big card the plugin wins by miles either way.

On startup it asks Windows how much video memory it is willing to give the game right now, holds
back an eighth of that (or 2 GB, whichever is more) for the parts of the game that are not
textures, and raises the ceiling to whatever is left. The log line looks like this:

```
budget: sized to this PC - 18.0 GB, up from the 7.8 GB the game set
        (card 24.0 GB, Windows is offering this process 23.2 GB right now)
```

If your card has nothing to spare, the plugin says so and leaves the budget alone rather than
pushing past what the card holds, which would make the game stutter instead of helping.

To pick the number yourself, set `texture_budget` in `_settings.txt` to a number of GB:

```
texture_budget = 8
```

Set it to `game` instead to switch the whole thing off and leave the game's budget exactly as it
was. Either way, restart FiveM after changing it.

A bigger ceiling buys headroom before the bug hits. It does not remove the bug, which lives inside
GTA itself, and it cannot make a pack fit that is simply too big. Shrinking the files in the
`HEAVY` list is still the fix that always works.

[Back to the README](../README.md)
