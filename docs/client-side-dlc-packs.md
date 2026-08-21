# Client-side DLC packs on FiveM (mods folder)

This is not about texoverride. It is a note on how to run a GTA V DLC pack, such as a vehicle audio
pack, on the FiveM client without server cooperation. It is written down because working it out took
a long evening and one wrong turn, and the answer is three lines of XML.

## The short version

Wrap the `dlc.rpf` in a mods-folder package whose `assembly.xml` has a **top level** `<add>` inside
`<content>`, pointing at a dlcpacks path:

```xml
<content>
<add source="mypack.rpf">update\x64\dlcpacks\myslot\dlc.rpf</add>
</content>
```

Package layout, all one `.rpf` with `OPEN` (unencrypted) headers:

```
My Pack.rpf
  assembly.xml
  content/mypack.rpf      <- the dlc.rpf, byte for byte
```

Drop it in `%LOCALAPPDATA%\FiveM\FiveM.app\mods\`. No dlclist edit, no changes to the real
game folder. Requires the server to have pure mode off, like every mods-folder package.

## The bit that is easy to get wrong

`<add>` behaves completely differently depending on where it sits:

| where | what it does |
|---|---|
| inside `<archive path="...">` | inserts a file into an archive that **already exists** |
| directly inside `<content>` | writes a file at a **game-relative path**, creating it if needed |

Only the second form can create a dlcpack. Testing only the first leads to the conclusion that
client-side DLCs are impossible on FiveM, which is wrong.

## Several packs in one file

`<content>` takes as many top-level `<add>` entries as you like, so several DLCs can ship as a single
mods-folder file. Give each its own slot name or they overwrite each other:

```xml
<content>
<add source="setA.rpf">update\x64\dlcpacks\mods\dlc.rpf</add>
<add source="setB.rpf">update\x64\dlcpacks\modsb\dlc.rpf</add>
</content>
```

Where two packs define the same thing, the slot that mounts later wins.

## What does not work, and why

- **Editing `dlclist.xml`.** Refused by Cfx outright. Their words: "Not going to happen. DLCs load
  too early for server reloading to remain working with this kind of feature." The archive count is
  also hard-coded by Rockstar and protected by Arxan code guards.
- **Dropping a dlcpack into the real `update/x64/dlcpacks/` folder.** That directory is hash
  validated; you get "modified game files". The mods-folder route avoids this because nothing on
  disk changes, the file only exists in FiveM's virtual view.
- **Merging a vehicle audio DLC into vanilla `game.dat`/`sounds.dat`.** Dat54 entries reference wave
  banks by index into their own file's name table, so concatenating entries from many files silently
  mis-points all of them. FiveM never merges; it registers each file separately.
- **Registering the audio data files from an ASI**, by calling the game's own
  `audMetadataDataFileMounter` / `audWavePackDataFileMounter`. This gets a long way (see CLAUDE.md)
  and then crashes inside the game's loader. A file from a pack that demonstrably works as a server
  resource crashes identically, so it is the loose-folder approach that is wrong, not the files.

## Vanilla vehicles

Expect custom sound to work on DLC and add-on vehicles and to be unreliable on **vanilla** ones. The
dlcpack mounts after the base audio config has already defined those vehicles. Pack authors hit this
too and generally drop the vanilla entries rather than fight it.

## Building one

Any RPF tool that can write `OPEN` archives will do. With CodeWalker.Core, note that
`RpfFile.CreateFile` writes a nested `.rpf` and **then** re-scans it to build an in-memory tree; that
scan can throw on an embedded NG-encrypted `dlc.rpf` even though the bytes on disk are already
complete and correct. Catch it, then reopen the finished package and check both members are present
and the inner archive is its original byte length.
