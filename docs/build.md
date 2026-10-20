# Build

## Build

You need Visual Studio Build Tools 2022 with the "Desktop development with C++" workload. Then:

```
build.bat
```

If the batch file's vcvars auto-detect fails on your machine, run the same thing from an "x64
Native Tools Command Prompt for VS 2022"; the batch skips detection when the environment is
already set up.

`build.bat dev` makes a test build instead, called `texoverride-dev.asi`. It writes extra lines
marked `DEV` into the log, for tracking down a problem. Never hand it out as a release. To try it,
move your normal `texoverride.asi` out of the plugins folder first and put this one in under that
name, because two copies in the folder would both load.

`tools\run_tests.bat` builds and runs the tests. CI runs them on every push before it builds.

### The build stamp

FiveM refuses any `.asi` on game build 2189 or newer that does not claim support for the running
build. The claim is the `FX_ASI_BUILD` resource in `texoverride.rc`, one line per supported game
build:

```
FX_ASI_BUILD 3751 BEGIN "\0" END
FX_ASI_BUILD 3788 BEGIN "\0" END
```

When FiveM moves to a new game build, add a line with the new number and rebuild, or the plugin
silently stops loading. This is why community ASIs go dead after every update.

### CI builds

GitHub Actions builds every push, so you can grab a fresh `texoverride.asi` from the Actions tab
without installing anything. Pushing a tag like `v0.2.0` builds and publishes a release with the
binary attached.

[Back to the README](../README.md)
