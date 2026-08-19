# packshrink

Puts an oversized `tex_overrides` pack on a diet without visibly hurting it. Oversized and
uncompressed textures are the usual cause of the "stuck on low detail, textures gone" bug on
busy servers; this tool fixes the files instead of the symptoms.

```
packshrink <tex_overrides folder> [--max 2048] [--out folder]
```

What it does to each `.ytd` / `.ydd`:

- already compressed, within the size cap, with mip maps: copied byte for byte, untouched
- larger than the cap (default 2048): downscaled by clean power-of-two halving
- uncompressed: converted to BC1 (opaque) or BC3 (with alpha), the same formats vanilla uses
- ATI1/ATI2/BC7 textures keep their format family so shaders see what they expect
- missing mip chains are regenerated

Output goes to a separate folder (`<input>_shrunk` by default). Your originals are never
modified. Check the result in game, then swap the folders.

## Building

Needs the .NET 9 SDK and a checkout of CodeWalker (the project reference points at
`CodeWalker.Core` three folders up, at `codewalker/CodeWalker-fork` next to this repo; adjust
the path in `packshrink.csproj` to wherever your CodeWalker checkout lives).

```
dotnet build -c Release
```
