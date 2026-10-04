# GitHub Actions

The workflows in [`.github/workflows`](../.github/workflows) build the emulator,
run automated tests, and produce downloadable packages. They do not publish
GitHub Releases.

For local build instructions and release checks, see [BUILD.md](../BUILD.md).
For details about the tests, see the [test README](README.md).

## Starting a run

- Push to `x432r-plus`, `master`, `main`, or a branch matching `ci/*` or
  `release/*`.
- Pull requests targeting `x432r-plus`, `master`, or `main` run the same checks.
- Tags beginning with `v` trigger builds, including release-candidate tags.
- Each workflow supports **Actions > workflow name > Run workflow**. GitHub
  shows this manual control once the workflow is on the default branch. Select
  the branch or tag to test.

To test workflow changes before merging, include them in a branch such as
`ci/build-check` and push it. The push trigger works without first adding the
workflows to the default branch. A release tag is not required.

On a fork, enable workflows in the **Actions** tab if GitHub asks. Jobs run on
GitHub-hosted machines, with virtual machines for BSD.

## Coverage

- **Windows:** x64 and ARM64 builds, with policy and debug-export tests.
- **Ubuntu:** x64 and ARM64 builds, with the full CTest suite. Shader and
  renderer-lifetime tests run under Xvfb with Mesa software OpenGL.
- **macOS:** Intel and Apple Silicon builds, with policy and debug-export tests.
  A separate job combines the builds into a universal app and checks its
  architectures and ad hoc signature.
- **BSD:** FreeBSD, NetBSD and OpenBSD x64 builds, with policy tests.

Shader tests require OpenGL 4.3 and are disabled in the Windows, macOS and BSD
workflows. Ubuntu uses software rendering to provide the required GL support.
This checks shader behavior, but does not measure GPU performance or cover
hardware driver differences. Native macOS lacks the compute shader support
needed by those tests and enhancements.

Each build job must pass its selected tests before uploading a binary.
Finding no tests counts as an error. Other platform jobs continue if one fails,
so check the results for every platform. A downloadable binary does not mean
that later packaging steps or the rest of the workflow passed.

## Results and downloads

Open a run in the repository's **Actions** tab and inspect its job results.
If a job fails, open the failed step's log. Downloadable `test-results-*`
artifacts contain CTest logs and JUnit XML when available, including after test
failures. An earlier build failure may leave no test report.

Binaries and packages appear in the run's artifact list. Artifacts are retained
for 14 days. Use downloads from the commit you intend to test or distribute.

Packages use upstream melonDS's platform formats, with `README.md` and
`LICENSE` alongside the application in every ZIP, without an enclosing folder:

- **Windows:** a ZIP containing `melonDS.exe`.
- **Linux AppImage (recommended):** a ZIP containing the `.AppImage`.
- **Ubuntu and BSD:** a ZIP containing `melonDS`, requiring system libraries.
- **macOS:** a ZIP containing `melonDS.app`, including a universal build.

Each ZIP has a separate `SHA256SUMS-*` download covering the archive itself.
ZIPs are uploaded directly to avoid an extra archive wrapper and preserve the
executable permissions and app bundle structure.

Use the AppImage package as the main Linux download: it bundles the application
and its library dependencies. The plain Ubuntu binary is an alternative for
users who already have the required libraries installed. Extract the AppImage
ZIP before running it; if needed, mark the `.AppImage` file as executable.

Embedded build information identifies the Git commit used. A tag triggers
builds only; it does not create a GitHub Release or set a new application
version automatically. The fork version is set by `MELONDS_FORK_VERSION` in the
top-level `CMakeLists.txt`. CI package names are not versioned; rename the ZIP and
regenerate its checksum entry when preparing a versioned release download.

The macOS app has an ad hoc signature; it is not notarized by Apple.

The macOS workflow pins CMake and includes its version in the dependency cache
key. Update both through `CMAKE_VERSION` in that workflow when changing CMake.
This avoids unexpected tool upgrades and lets a new version save a fresh cache.

Before publishing a package, follow the
[release checks in BUILD.md](../BUILD.md#preparing-a-release). Passing CI does
not replace gameplay checks or testing the package on supported hardware.
