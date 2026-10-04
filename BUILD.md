# Building melonDS X432R+

These instructions cover Qt 6 builds for Windows, Linux and macOS, including
running tests and creating standalone packages. Automated build and packaging
steps are available in [`.github/workflows`](.github/workflows).

* [Get the source](#get-the-source)
* [Windows](#windows)
* [Linux](#linux)
* [macOS](#macos)
* [Nix](#nix)
* [Running tests](#running-tests)
* [Preparing a release](#preparing-a-release)

## Get the source

Install Git, then clone this fork:

```sh
git clone https://github.com/ZironZ/melonDS-X432R-Plus.git
cd melonDS-X432R-Plus
```

Run the commands below from the repository directory. Keep separate build
directories for different compilers, architectures and static/dynamic builds.
The CMake presets require CMake 3.25 or newer.

## Windows

### Development build

Install [MSYS2](https://www.msys2.org/) and open **MSYS2 UCRT64** for an x64
build. The commands in this section use that terminal, rather than PowerShell
or the plain MSYS terminal.

Update MSYS2 with `pacman -Syu`. If it asks you to close the terminal, reopen
UCRT64 and run the update again. Install the build tools and dependencies:

```sh
pacman -S --needed git \
  mingw-w64-ucrt-x86_64-{toolchain,cmake,ninja,pkgconf} \
  mingw-w64-ucrt-x86_64-{SDL2,libarchive,enet,zstd,faad2} \
  mingw-w64-ucrt-x86_64-{qt6-base,qt6-multimedia,qt6-svg,qt6-tools}
```

Configure and build:

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DUSE_QT6=ON \
  -DUSE_VCPKG=OFF -DBUILD_STATIC=OFF \
  -DBUILD_POLICY_TESTS=ON -DBUILD_DEBUG_EXPORT_TESTS=ON \
  -DBUILD_RENDERER_SHADER_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/melonDS.exe
```

This build needs the MSYS2 DLLs. Run it from UCRT64 so it can find them, or add
your MSYS2 `ucrt64/bin` directory to the current shell's `PATH`. Use the static
build below when distributing the executable.

The renderer tests require OpenGL 4.3. On a machine without it, configure with
`-DBUILD_RENDERER_SHADER_TESTS=OFF`; the other tests can still run.

### Standalone release build

To build a standalone x64 executable, use UCRT64 with static Qt 6 and other
dependencies supplied by vcpkg. With the UCRT64 build tools installed, run:

```sh
cmake --preset release-mingw-x86_64 \
  -DUSE_QT6=ON -DENABLE_DEBUG_DEPS=OFF \
  -DBUILD_POLICY_TESTS=ON -DBUILD_DEBUG_EXPORT_TESTS=ON \
  -DBUILD_RENDERER_SHADER_TESTS=ON
cmake --build --preset release-mingw-x86_64 --parallel
ctest --test-dir build/release-mingw-x86_64 --output-on-failure
```

The preset enables `USE_VCPKG` and `BUILD_STATIC`. Configuration obtains vcpkg
if needed and installs the dependencies defined in `vcpkg.json`, using the
`x64-mingw-static-release` triplet. The first build includes Qt and can take
considerably longer than rebuilding the emulator.

The executable is `build/release-mingw-x86_64/melonDS.exe`. Once dependencies
are installed, you can add `-DVCPKG_MANIFEST_INSTALL=OFF` when reconfiguring
that build to reuse them. Leave it on for a fresh build, and turn it back on
after changing the dependency manifest or triplet.

For an existing vcpkg installation, pass `-DVCPKG_ROOT=...`. If reusing a
separate dependency directory, also pass `-DVCPKG_INSTALLED_DIR=...` and point
`PKG_CONFIG_EXECUTABLE` at that triplet's `tools/pkgconf/pkgconf.exe`. Reuse
dependencies built with the same compiler, architecture and triplet.

### Windows CI builds

The [Windows workflow](.github/workflows/build-windows.yml) uses LLVM with
Visual Studio build tools and vcpkg, rather than MSYS2. Its presets are
`release-windows-x86_64` and `release-windows-arm64`.

To use them locally, install Visual Studio C++ build tools, LLVM, CMake, Ninja
and Git. Open a Visual Studio developer shell for the target architecture,
with `clang` and `llvm-rc` on `PATH`. For x64:

```sh
cmake --preset release-windows-x86_64 -DENABLE_DEBUG_DEPS=OFF
cmake --build --preset release-windows-x86_64 --parallel
ctest --test-dir build/release-windows-x86_64 --output-on-failure
```

Use `release-windows-arm64` in an ARM64 developer environment for ARM64.
CI enables the policy and debug-export tests. OpenGL tests are run separately
on a machine with a suitable graphics context.

## Linux

The [Ubuntu workflow](.github/workflows/build-ubuntu.yml) builds with Qt 6 on
Ubuntu 22.04 for x64 and ARM64. For Ubuntu 22.04 or 24.04, install:

```sh
sudo apt update
sudo apt install build-essential git cmake ninja-build pkg-config \
  extra-cmake-modules libpcap0.8-dev libsdl2-dev libenet-dev \
  qt6-base-dev qt6-base-private-dev qt6-multimedia-dev qt6-wayland \
  libqt6svg6-dev libarchive-dev libzstd-dev libfaad-dev
```

Then configure and build:

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DUSE_QT6=ON \
  -DBUILD_POLICY_TESTS=ON -DBUILD_DEBUG_EXPORT_TESTS=ON \
  -DBUILD_RENDERER_SHADER_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/melonDS
```

Other distributions need the equivalent development packages. As on Windows,
disable renderer tests if OpenGL 4.3 is unavailable.

For headless testing, CI uses Mesa software OpenGL with Xvfb:

```sh
sudo apt install xvfb xauth mesa-utils libgl1-mesa-dri
LIBGL_ALWAYS_SOFTWARE=1 QT_QPA_PLATFORM=xcb \
  xvfb-run -a ctest --test-dir build --output-on-failure --timeout 1200
```

The Ubuntu workflow also stages an installation and packages an AppImage with
linuxdeploy and its Qt plugin. Follow that workflow for the packaging steps;
the executable in `build` alone still depends on system libraries.

## macOS

macOS's native OpenGL implementation does not support the OpenGL 4.3 compute
shaders used by this fork. Building the app does not enable the Compute
renderer or the enhancements that require compute shaders. Leave renderer
shader tests disabled on macOS.

### Development build

Install the Xcode command-line tools and [Homebrew](https://brew.sh/), then:

```sh
brew install git cmake ninja pkg-config sdl2 qt@6 libarchive enet zstd faad2
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DUSE_QT6=ON \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6);$(brew --prefix libarchive)" \
  -DBUILD_POLICY_TESTS=ON -DBUILD_DEBUG_EXPORT_TESTS=ON \
  -DBUILD_RENDERER_SHADER_TESTS=OFF
cmake --build build --parallel
ctest --test-dir build --output-on-failure
open build/melonDS.app
```

For a bundle that includes its Homebrew libraries, add
`-DMACOS_BUNDLE_LIBS=ON` when configuring. This runs `tools/mac-libs.rb` after
the build. Check the bundle on a machine without those Homebrew dependencies
before distributing it.

### Release packages

The [macOS workflow](.github/workflows/build-macos.yml) uses vcpkg presets
`release-mac-x86_64` and `release-mac-arm64` for separate Intel and Apple
Silicon builds. It then combines the executables with `lipo`, signs the
resulting bundle with an ad hoc signature, and creates a universal ZIP.
An ad hoc signature is not Apple notarization.

For a native Apple Silicon build matching the preset:

```sh
brew install git cmake ninja autoconf automake autoconf-archive \
  libtool python-setuptools
cmake --preset release-mac-arm64 \
  -DBUILD_POLICY_TESTS=ON -DBUILD_DEBUG_EXPORT_TESTS=ON \
  -DBUILD_RENDERER_SHADER_TESTS=OFF
cmake --build --preset release-mac-arm64 --parallel
ctest --test-dir build/release-mac-arm64 --output-on-failure
```

Use `release-mac-x86_64` on Intel. Do not pass both architectures in one
`CMAKE_OSX_ARCHITECTURES` value when using vcpkg; build them separately as CI
does.

## Nix

With [Nix](https://nixos.org/) and flakes enabled, use this fork's checkout:

```sh
nix build
nix run .
```

Use `nix develop` for a development shell. The flake supports Linux and macOS;
the macOS renderer limitations still apply.

## Running tests

The [test README](test/README.md) describes the CTest suite and its coverage.
The tests use synthetic inputs and do not require game ROMs or save states.

Policy tests are enabled by default. For a smaller build without Qt or SDL:

```sh
cmake -S . -B build-policy -DBUILD_QT_SDL=OFF -DBUILD_POLICY_TESTS=ON
cmake --build build-policy --target policy-tests --parallel
ctest --test-dir build-policy --output-on-failure
```

`BUILD_DEBUG_EXPORT_TESTS` adds export and worker-lifetime tests.
`BUILD_RENDERER_SHADER_TESTS` adds shader and renderer-lifetime tests that
need a working OpenGL 4.3 context. These two options are off by default; the
development commands above enable them where supported.

## Preparing a release

Build from the commit intended for release, with a clean tracked working tree.
Use a separate Release build directory and test the executable you will ship.
Set `MELONDS_FORK_VERSION` in the top-level `CMakeLists.txt` to the release
version. It supplies the default version shown in About and the title bar,
along with the Windows and macOS package metadata. For a local release, add
build identification to the CMake configuration:

```sh
-DMELONDS_EMBED_BUILD_INFO=ON
"-DMELONDS_BUILD_PROVIDER=Local release"
```

These are CMake arguments, not separate shell commands. Branch and commit
information can also be supplied with `MELONDS_GIT_BRANCH` and
`MELONDS_GIT_HASH`; CI supplies its build identification automatically.
`MELONDS_VERSION_SUFFIX` can override the displayed suffix for custom builds.

Before packaging:

1. Run CTest for the release build, including renderer tests on a supported
   machine. Check failures rather than treating missing GL support as a pass.
2. Check startup, loading and resetting games, renderer switching, and a few
   representative games with the intended enhancement settings.
3. For Windows static builds, inspect DLL imports with `objdump -p` or
   `dumpbin /DEPENDENTS`. Windows system DLLs are expected; Qt, SDL and MinGW
   runtime DLLs should not be required by the standalone executable.
4. Check the packaged application outside the development environment, and
   generate a SHA-256 checksum for the distributed file.

CI builds and automated tests complement game testing. A successful build
alone does not verify image quality, performance or packaging on another PC.
