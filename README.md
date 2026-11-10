<p align="center">
  <img src="assets/logo.png" alt="slinkyFX" width="420">
</p>

<h1 align="center">slinkyFX</h1>

<p align="center">
  A ghost client for Minecraft 1.8.9 and 1.7.10.
</p>

---

slinkyFX serves as an accessible, cost-effective alternative to the official slinky ghost client.
Developed specifically for budget-conscious users, its core mission is to emulate the original software's entire ecosystem.
The developers have made every effort to match slinky's exact functionality, ensuring that all variables, configurations, and performance traits remain indistinguishable from the premium version.

The loader is deliberately quiet. It scans for a Java process that actually
has LWJGL loaded, works out which naming scheme the game is running with, and
starts the same native client on every one of them. If it cannot inject, it says
so in the same place and the same shape as a successful injection, including a
short close countdown, so a failure is never a silent window that disappears.

If that scan is not finding your game - an instance started before the loader,
a launcher that renames the window, a game running as administrator while the
loader is not - both loaders also take the process id directly: start Minecraft,
find its number in the launcher's log line or with `ps` (Task Manager's Details
tab on Windows), then run `slinky-loader --pid <that number>`. The id is checked
before anything is injected, so a wrong one is refused with a message rather than
a library dropped into an unrelated program.

Everything runs in user space. On Windows the loader writes into the game
process with the normal Win32 process APIs; on Linux it attaches with ptrace
and calls `dlopen` in the target. Linux does not need root and does not
re-launch itself through `sudo`. If Yama's `kernel.yama.ptrace_scope`
restriction is active, the loader tries to relax it for the session; on a
system where that needs root the attempt fails quietly and the regular error
path reports it.

## Supported clients

Four clients, two Minecraft versions, two operating systems. Nothing outside
that list is supported, tested or claimed - other launchers, other versions,
other mod loaders.

### Full compatibility matrix

**Windows 10 / 11, x64**

| Client | 1.8.9 | 1.7.10 |
|---|---|---|
| Vanilla | Yes | Yes |
| Forge | Yes | Yes |
| Badlion Client | Yes | Yes |
| Lunar Client | Yes | Yes |

**Linux x86_64 - Arch, Debian / Ubuntu, Fedora**

| Client | 1.8.9 | 1.7.10 |
|---|---|---|
| Vanilla | Yes | Yes |
| Forge | Yes | Yes |
| Badlion Client | Yes | Yes |
| Lunar Client | Yes | Yes |

Every cell in both tables runs the same engine and the same modules; what
differs is how it gets into the game. The Linux 1.7.10 column is new in 1.5.0.
The Linux 1.8.9 column was carried by a Java client until 1.6.0, which is the
release where Linux stopped having a second client: 1.7.10 and 1.8.9 now run the
same C++ code on Linux as on Windows, as `libslinky-engine.so`.

The Java the game runs on has to be 8 or newer, on any client and both versions.
The piece of code the engine puts inside the game to reach its classes is built
for that, and a 1.7.10 instance still set to a Java 7 runtime refuses it: the
injection gets that far and stops, with no overlay and no key binds. Badlion and
Lunar ship a runtime that is new enough; for vanilla and Forge, pick Java 8 in
the launcher's Java tab. Java 7 support would mean rebuilding that piece for it,
which is not something a config option can do.

The native layer is built from `SlinkyNative/`. On Windows it produces the
slinkyFX native engine (`slinky-native.dll`) and hooks the game directly. On
Linux it produces two files: a bootstrap (`libslinky-native.so`), which the
loader puts into the game, and the engine (`libslinky-engine.so`), which the
bootstrap starts and which does the hooking - so a Linux game runs the same
client code a Windows game does. There are no Java files to ship or to build:
the payload jars that Linux 1.8.9 used to carry are gone.

The engine detects the naming scheme in use (official/notch, SRG for Forge,
named for MCP/dev environments, or legacy 1.7.10). Check `slinky-native.log` in
the loader's temp folder if you want to see which mapping was chosen; the log
prints the detected version and namespace.

## Supported platforms

| Platform | Display stack | Status |
|---|---|---|
| Windows 10 / 11 (x64) | Win32 | Supported |
| Arch Linux (x86_64) | Wayland (native) | Supported |
| Arch Linux (x86_64) | X11 / XWayland | Supported |
| Debian 12+, Ubuntu 22.04+ (x86_64) | Wayland (native) | Supported |
| Debian 12+, Ubuntu 22.04+ (x86_64) | X11 / XWayland | Supported |
| Fedora (x86_64) | Wayland (native) | Supported |
| Fedora (x86_64) | X11 / XWayland | Supported |

Those rows describe the download. Debian 11 and Ubuntu 20.04 are supported as
well, but only through `./build.sh` on that machine; the third bullet below has
the numbers and the reason.

Three things about those Linux rows, none of them a limitation of slinkyFX:

- The game is located through `/proc` first, so a Wayland session needs no X11
  tooling. X11 support is optional and soft: when the X11 libraries are present
  the loader can also match a window title, but the `/proc` scan works on its
  own. Install the X11 development headers only if your distribution does not
  already provide them. The loader in the Linux zip is the `/proc`-only build -
  no window-title matching - because libX11 is not part of the cross toolchain
  that makes it. Building from source with the headers installed gets both.

- A 1.7.10 game runs on LWJGL 2, and LWJGL 2 has no Wayland backend. On a
  Wayland session it is therefore served by XWayland, which is what the
  "X11 / XWayland" row covers; slinkyFX does not care which of the two the game
  came in through.

- The three files a release ships are built so that one set serves every row
  above. The engine needs nothing newer than glibc 2.29 and the bootstrap
  nothing newer than 2.17; the loader is the demanding one of the three, at
  2.34, which covers Debian 12 and later, Ubuntu 22.04 and later, and every
  current Fedora and Arch. On an older distribution - Debian 11, Ubuntu 20.04 -
  the download will not start, and `./build.sh` on that machine will: a source
  build links the glibc it is standing on, and has always been allowed to stay
  there.

## Building

You need CMake 3.20 or newer and a C++17 compiler. On Windows the slinkyFX
native engine in `SlinkyNative/` is built with the bundled LLVM/clang-cl
toolchain in `tools/llvm/`, so no separate compiler installation is required.
Python 3 is optional; when it is present the build also checks the native module
export ledger.

None of this is needed to play. The release page carries
`slinkyFX-<version>-windows.zip` (the loader and `slinky-native.dll`) and, from
1.6.0, `slinkyFX-<version>-linux.zip` (the loader, `libslinky-native.so` and
`libslinky-engine.so`). Keep to the steps below when you want files built by
your own machine - which is also the only way to run on a distribution older
than the floors listed above.

### Windows

Use the **x64 Native Tools Command Prompt for VS** (or any shell where `cl`
is available), then from the repository root run the cross-platform helper
under Git Bash, MSYS2 or Cygwin:

```sh
./build.sh
```

`build.sh` downloads LLVM 23.1.4 and Ninja into `tools/` on first run, builds
the slinkyFX native engine with clang-cl, builds the MSVC loader, and copies
both files to `dist/`.

You can also configure and build directly. The loader and engine are built
separately because the engine requires clang-cl/Ninja:

```bat
cmake -S . -B build -A x64
cmake -S SlinkyNative -B build/SlinkyNative -G Ninja ^
  -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl ^
  -DCMAKE_LINKER=lld-link -DCMAKE_AR=llvm-lib -DCMAKE_RC_COMPILER=llvm-rc
cmake --build build/SlinkyNative --config Release
cmake --build build --config Release
```

Then copy `build/SlinkyNative/slinky-native.dll` next to
`build/bin/Release/slinky-loader.exe`.

### Linux

Install a compiler, CMake, and the X11 development headers if your distribution
splits them out (`libx11-dev` on Debian/Ubuntu, `libX11-devel` on Fedora,
`libx11` on Arch):

```sh
sudo apt install build-essential cmake libx11-dev    # Debian / Ubuntu
sudo dnf install gcc-c++ cmake libX11-devel          # Fedora
sudo pacman -S base-devel cmake libx11               # Arch
./build.sh
```

The build produces `build/bin/slinky-loader`, `build/bin/libslinky-native.so`
and `build/linux-engine/libslinky-engine.so`, and copies all three to `dist/`.
Keep them in the same folder.

The first two are the loader and the bootstrap that hands the game to the
engine; the engine is the client, the same one Windows ships, built with your
machine's clang instead of the bundled one. That last part needs clang, lld and
the LLVM tools:

```sh
sudo apt install clang lld llvm python3              # Debian / Ubuntu
sudo dnf install clang lld llvm python3              # Fedora
sudo pacman -S clang lld llvm python                 # Arch
```

Without them `build.sh` stops and prints those lines, because there is nothing
else to run: the Java payload that Linux 1.8.9 used before the engine existed
there has been removed. `tools/build-linux-engine.sh` on its own prints the same
install lines. `./build.sh -Clean` throws `build/` away first.

The engine is deliberately clang-only: its warning set and the alignment check
that runs on the finished file are clang's, and it is the compiler the engine
was written and verified against.

Everything above is also what a *release* build does, and the three release
files are cross-built from Windows: `tools/setup-linux-sysroot.sh` lays out the
sysroots, `tools/build-linux-engine.sh` makes the engine, and
`tools/build-linux-binaries.sh` compiles the loader and the bootstrap against
Ubuntu 20.04 headers and links them. The two scripts together are how one set of
files carries those glibc floors and serves every distribution in the matrix.
`tools/check-linux-bootstrap.sh` does the same compile without linking, so a
mistake in that code is caught before it becomes somebody else's build failure.

### Cross-platform helper

`build.sh` works under Git Bash, MSYS2 and Cygwin on Windows. It configures,
builds, runs the export and constant-alignment checks when Python is available,
and copies what it built into `dist/`: two files on Windows and three on Linux -
the loader, the bootstrap, and the engine both versions run. Without the
engine's toolchain it stops and prints the install lines, rather than handing out
half a set.

```sh
./build.sh          # configure + build + copy to dist/
./build.sh -Clean   # remove build/ first
```

## Running

Start Minecraft first, then run the loader:

- Windows: double-click `slinky-loader.exe` (or run it from a terminal).
- Linux: run `./slinky-loader` from the folder that contains
  `libslinky-native.so` and `libslinky-engine.so` - the bootstrap puts the
  engine into the game, and the engine is the client on both versions.

The loader waits for the game, injects, and prints
`Injected successfully. Loader will close in 5s.` before it exits. If anything
fails you get the same block with `Failed to inject.` and the reason on the
license line, followed by the same countdown.

On Linux, a game that is already running slinkyFX has to be unloaded from
inside the game (the unload key) before the loader will inject it again; the
loader says so instead of pulling the library out from under a running client.
Once it is unloaded, injecting again works.

Open the in-game GUI with the key bind the client registers. It appears in
Minecraft's Controls screen and can be changed there.

---

<p align="center">This project carries an MIT license.</p>
