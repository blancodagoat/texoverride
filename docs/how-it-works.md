# How it works

## How it works

For the technically curious, and for server owners deciding whether to allow it.

The plugin hooks one game function, `registerRawStreamingFile`, the same routine FiveM uses to
register loose and server-streamed files. The byte pattern that locates it comes from Cfx's open
source tree (`gta-streaming-five/src/Streaming.cpp`).

It hooks the game module (`GTA5.exe`) only, never FiveM's own DLLs. FiveM's `legitimacy`
anti-tamper terminates the process if you modify Cfx components; a hook in the game module is the
same surface trainers and `PackfileLimitAdjuster.asi` use, and it survives full connected
sessions.

Base freemode clothing lives inside `x64v.rpf` and never passes through that function, so waiting
to intercept it would wait forever. Instead the plugin calls `registerRawStreamingFile` itself and
registers your loose file under the base slot name. If the game rejects that call because the slot
already owns a handle, the plugin locates the slot through its actual streaming module and attaches
the local pgRawStreamer handle directly, matching FiveM's occupied-slot mechanism. It does not use
FiveM's diagnostic name map because that map omits base-RPF names.

That claim alone is not enough: a streaming
slot maps name → id → handle, and whoever writes the handle last owns the slot. Vanilla DLC mounts
re-point claimed slots when they load, and FiveM's loader overwrites handles of already-registered
slots directly, without calling the hooked function at all. So the plugin remembers the handle its
claim produced and re-asserts it once a second: if anything re-pointed the slot, it writes its own
handle back. Last writer wins, and the plugin is always the last writer. This is the same
handle-overwrite mechanism Cfx's own override path uses in `LoadStreamingFile.cpp`; the plugin
just repeats it. Streamed files that pass through the hook under a claimed name are also
redirected to the local file on an exact `collection/file` match.

Bare-name `.ytd` files at the root of `tex_overrides` are registered the same way, under the file
name alone. This is the same trust model as a server `stream/` folder: an exact-name match
replaces exactly that texture dictionary and nothing else.

Tattoo placement works on data, not code. The game parses each `overlays.xml` into its
`PedDecorationManager`. The plugin locates that manager with the pattern Cfx itself publishes
(`PatchTattooSort.cpp`) and rewrites the position floats of the presets you edited. It never
hardcodes struct offsets. Instead it fingerprints your file's preset name hashes and unedited
values against memory, and writes only after at least 70% of the presets match exactly. Applied
values are re-asserted once a second, like the handles.

The hook is installed without suspending any threads, and the timing is what makes that safe:
FiveM loads `.asi` plugins in `LauncherInterface::PostLoadGame`, before the game's entry point has
ever run, so no thread can be executing game code during the patch. FiveM applies its own startup
patches in the same window for the same reason. MinHook's usual thread-freeze step cannot work
under FiveM anyway, since `CreateToolhelp32Snapshot` is blocked; the vendored copy is patched to
skip it, which is commented in `minhook/src/hook.c`.

The path handed to the game is a plain absolute path, which FiveM's VFS opens without complaint.
The game reads the whole resource from your file (header, page flags, data), so there is no size
or flag mismatch to manage.

The plugin makes exactly one network request: at startup it asks GitHub for the newest release
number (see [Update check](../README.md#update-check)). Nothing else is transmitted anywhere, and nothing is
ever sent to the game server. The plugin reads a local folder and changes what your client draws.
Other players keep seeing whatever the server streams.

## Why FiveM allows this

The plugin loads because FiveM's own loader is built to load third-party ASIs, not to block them.
Four things from Cfx's own source and docs, strongest first:

- **The `FX_ASI_BUILD` stamp is Cfx's API, not a workaround.** The loader looks up an
  `FX_ASI_BUILD` resource for the running game build, and when a plugin has none it tells you to
  add `FX_ASI_BUILD <build> BEGIN "\0" END` to the `.rc` file when building the plugin, or to
  contact its maintainer if you do not have the source. That is Cfx documenting how to ship a
  supported ASI. You do not build a versioning contract for software you want gone.
  ([asi-five Component.cpp](https://github.com/citizenfx/fivem/blob/master/code/components/asi-five/src/Component.cpp))
- **The loader is deny-by-exception.** It loads every `.asi` in the plugins folder except a short
  hardcoded blacklist (`openiv.asi`, `scripthookvdotnet.asi`, `fspeedometerv.asi`), an outdated
  `Gears.asi`, and .NET/CLR assemblies. Everything not named loads. An allowlist would be the
  design if the intent were to restrict.
  ([asi-five Component.cpp](https://github.com/citizenfx/fivem/blob/master/code/components/asi-five/src/Component.cpp))
- **The docs say so.** The client manual states FiveM "allows the use of certain plugins," placed
  in the plugins folder, where you can put "many types of .asi scripts you would typically use in
  singleplayer," and that servers "have the option to disallow the use of plugins."
  ([Client Manual](https://docs.fivem.net/docs/client-manual/))
- **Pure mode is opt-in.** The server-commands reference documents two pure mode levels, 1 and 2.
  There is no level 0 because level 0 is just a server that has not turned pure mode on, which is
  the default.
  ([Server Commands](https://docs.fivem.net/docs/server-manual/server-commands/))

All four settle one question: whether a plugin is allowed to load. None of them say anything about
what a plugin does in memory once loaded. That is a separate question, covered honestly in
[Ban risk, stated plainly](../README.md#ban-risk-stated-plainly) below.

[Back to the README](../README.md)
