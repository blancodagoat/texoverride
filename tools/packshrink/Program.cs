// packshrink — put an oversized tex_overrides pack on a diet without visibly hurting it.
//
// Policy (quality first):
//   - textures already BC-compressed, within the size cap, with mips: left byte-identical
//   - larger than the cap (default 2048): downscaled by clean power-of-two halving
//   - uncompressed (A8R8G8B8 and friends): BC1 (opaque) / BC3 (has alpha), like vanilla
//   - ATI1/ATI2 sources keep their format family (BC4/BC5), BC7 stays BC7
//   - missing mip chains are regenerated
// Output goes to a sibling folder (<input>_shrunk by default); originals are never touched.
//
// Uses CodeWalker.Core for ytd/ydd plumbing and BCnEncoder.NET for the actual encoding.

using BCnEncoder.Encoder;
using BCnEncoder.Shared;
using CodeWalker.GameFiles;
using CodeWalker.Utils;
using Microsoft.Toolkit.HighPerformance;

const long HEAVY = 8L << 20;

string inDir = null, outDir = null;
int cap = 2048;
for (int i = 0; i < args.Length; i++)
{
    if (args[i] == "--max" && i + 1 < args.Length) cap = int.Parse(args[++i]);
    else if (args[i] == "--out" && i + 1 < args.Length) outDir = args[++i];
    else if (inDir == null) inDir = args[i];
}
if (inDir == null || !Directory.Exists(inDir))
{
    Console.WriteLine("usage: packshrink <tex_overrides folder> [--max 2048] [--out folder]");
    return 1;
}
inDir = Path.GetFullPath(inDir).TrimEnd('\\', '/');
outDir ??= inDir + "_shrunk";
Directory.CreateDirectory(outDir);

// GetPixels' channel order isn't documented; measure it once with a solid red block
bool bgra = DetectBgra();
Console.WriteLine($"packshrink: cap {cap}px, decode order {(bgra ? "BGRA" : "RGBA")}, output -> {outDir}");

long memBefore = 0, memAfter = 0;
int filesChanged = 0, filesCopied = 0, filesFailed = 0, texChanged = 0;

foreach (var path in Directory.EnumerateFiles(inDir, "*.*", SearchOption.AllDirectories))
{
    var relPath = Path.GetRelativePath(inDir, path);
    var dest = Path.Combine(outDir, relPath);
    Directory.CreateDirectory(Path.GetDirectoryName(dest));

    var ext = Path.GetExtension(path).ToLowerInvariant();
    var data = File.ReadAllBytes(path);
    long oldMem = RscMem(data);
    memBefore += oldMem;

    byte[] outData = null;
    if ((ext == ".ytd" || ext == ".ydd") && oldMem > 0)
    {
        try { outData = ShrinkFile(data, ext); }
        catch (Exception ex)
        {
            Console.WriteLine($"  FAILED  {relPath} — {ex.Message}; copied unchanged");
            filesFailed++;
        }
    }

    if (outData != null)
    {
        long newMem = RscMem(outData);
        memAfter += newMem;
        filesChanged++;
        Console.WriteLine($"  {oldMem / 1048576.0,7:F1} -> {newMem / 1048576.0,5:F1} MB  {relPath}");
        File.WriteAllBytes(dest, outData);
    }
    else
    {
        memAfter += oldMem;
        filesCopied++;
        File.Copy(path, dest, true);
    }
}

Console.WriteLine();
Console.WriteLine($"done: {filesChanged} file(s) shrunk, {filesCopied} untouched, {filesFailed} failed");
Console.WriteLine($"pack cost in game memory: {memBefore / 1048576.0:F1} -> {memAfter / 1048576.0:F1} MB ({texChanged} texture(s) re-encoded)");
Console.WriteLine($"swap the folders when happy: the shrunk pack is a drop-in replacement.");
return 0;

// ---------------------------------------------------------------------------------------------

byte[] ShrinkFile(byte[] data, string ext)
{
    bool changed = false;
    if (ext == ".ytd")
    {
        var ytd = new YtdFile();
        ytd.Load(data);
        changed = ShrinkDict(ytd.TextureDict);
        return changed ? ytd.Save() : null;
    }
    var ydd = new YddFile();
    ydd.Load(data);
    if (ydd.Drawables != null)
        foreach (var dr in ydd.Drawables)
            if (dr?.ShaderGroup?.TextureDictionary != null)
                changed |= ShrinkDict(dr.ShaderGroup.TextureDictionary);
    return changed ? ydd.Save() : null;
}

bool ShrinkDict(TextureDictionary dict)
{
    var texs = dict?.Textures?.data_items;
    if (texs == null) return false;
    bool changed = false;
    for (int i = 0; i < texs.Length; i++)
    {
        var nt = ShrinkTexture(texs[i]);
        if (nt == null) continue;
        texs[i] = nt;
        if (dict.Dict != null && nt.NameHash != 0) dict.Dict[nt.NameHash] = nt;
        changed = true;
        texChanged++;
    }
    return changed;
}

Texture ShrinkTexture(Texture tex)
{
    if (tex == null || tex.Data?.FullData == null) return null;
    int w = tex.Width, h = tex.Height;
    bool isBC = tex.Format is TextureFormat.D3DFMT_DXT1 or TextureFormat.D3DFMT_DXT3 or TextureFormat.D3DFMT_DXT5
                            or TextureFormat.D3DFMT_ATI1 or TextureFormat.D3DFMT_ATI2 or TextureFormat.D3DFMT_BC7;
    bool tooBig = w > cap || h > cap;
    bool noMips = tex.Levels <= 1 && Math.Max(w, h) >= 256;
    if (isBC && !tooBig && !noMips) return null;                       // already sane, keep bytes
    if (w < 4 || h < 4 || (w & (w - 1)) != 0 || (h & (h - 1)) != 0) return null;   // odd sizes: leave alone

    var px = DDSIO.GetPixels(tex, 0);                                  // decoded, bgra order per probe
    // downscale by box-halving until it fits the cap (exact 2x steps keep quality high)
    while (w > cap || h > cap) { px = HalveBox(px, w, h); w /= 2; h /= 2; }

    var colors = new ColorRgba32[w * h];
    bool alpha = false;
    for (int p = 0, c = 0; c < colors.Length; p += 4, c++)
    {
        byte r = bgra ? px[p + 2] : px[p], b = bgra ? px[p] : px[p + 2];
        colors[c] = new ColorRgba32(r, px[p + 1], b, px[p + 3]);
        if (px[p + 3] < 250) alpha = true;
    }

    var fmt = tex.Format switch
    {
        TextureFormat.D3DFMT_ATI1 => CompressionFormat.Bc4,
        TextureFormat.D3DFMT_ATI2 => CompressionFormat.Bc5,
        TextureFormat.D3DFMT_BC7  => CompressionFormat.Bc7,
        _ => alpha ? CompressionFormat.Bc3 : CompressionFormat.Bc1,
    };

    var enc = new BcEncoder();
    enc.OutputOptions.GenerateMipMaps = true;
    enc.OutputOptions.Quality = fmt == CompressionFormat.Bc7 ? CompressionQuality.Fast : CompressionQuality.Balanced;
    enc.OutputOptions.Format = fmt;
    enc.OutputOptions.FileFormat = OutputFileFormat.Dds;
    var dds = enc.EncodeToDds(new ReadOnlyMemory2D<ColorRgba32>(colors, h, w));
    using var ms = new MemoryStream();
    dds.Write(ms);

    var nt = DDSIO.GetTexture(ms.ToArray());
    nt.Name = tex.Name;
    nt.NameHash = tex.NameHash;
    nt.Usage = tex.Usage;
    nt.UsageFlags = tex.UsageFlags;
    nt.ExtraFlags = tex.ExtraFlags;
    return nt;
}

static byte[] HalveBox(byte[] px, int w, int h)
{
    int nw = w / 2, nh = h / 2;
    var np = new byte[nw * nh * 4];
    for (int y = 0; y < nh; y++)
        for (int x = 0; x < nw; x++)
            for (int c = 0; c < 4; c++)
            {
                int a = ((y * 2) * w + x * 2) * 4 + c, b = a + 4, d = a + w * 4, e = d + 4;
                np[(y * nw + x) * 4 + c] = (byte)((px[a] + px[b] + px[d] + px[e] + 2) / 4);
            }
    return np;
}

// exact in-game memory charge, from the RSC7 header (same decode texoverride logs with)
static long RscMem(byte[] data)
{
    if (data.Length < 16 || BitConverter.ToUInt32(data, 0) != 0x37435352) return 0;
    return SizeFromFlags(BitConverter.ToUInt32(data, 8)) + SizeFromFlags(BitConverter.ToUInt32(data, 12));
}
static long SizeFromFlags(uint f)
{
    long pages = ((f >> 27) & 0x1) + (((f >> 26) & 0x1) << 1) + (((f >> 25) & 0x1) << 2)
               + (((f >> 24) & 0x1) << 3) + (((f >> 17) & 0x7F) << 4) + (((f >> 11) & 0x3F) << 5)
               + (((f >> 7) & 0xF) << 6) + (((f >> 5) & 0x3) << 7) + (((f >> 4) & 0x1) << 8);
    return (0x200L << (int)(f & 0xF)) * pages;
}

static bool DetectBgra()
{
    var red = new ColorRgba32[16];
    Array.Fill(red, new ColorRgba32(255, 0, 0, 255));
    var enc = new BcEncoder();
    enc.OutputOptions.GenerateMipMaps = false;
    enc.OutputOptions.Format = CompressionFormat.Bc1;
    enc.OutputOptions.FileFormat = OutputFileFormat.Dds;
    var dds = enc.EncodeToDds(new ReadOnlyMemory2D<ColorRgba32>(red, 4, 4));
    using var ms = new MemoryStream();
    dds.Write(ms);
    var px = DDSIO.GetPixels(DDSIO.GetTexture(ms.ToArray()), 0);
    if (px[2] > 200 && px[0] < 60) return true;    // red landed in byte 2: BGRA
    if (px[0] > 200 && px[2] < 60) return false;   // red landed in byte 0: RGBA
    throw new Exception("channel-order self test failed — refusing to re-encode anything");
}
