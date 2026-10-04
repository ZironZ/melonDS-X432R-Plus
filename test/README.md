# Tests

The automated tests run through CTest and include their own test data. You do
not need game ROMs, save states, recordings or screenshots to run them.

## Running the tests

Run these commands from the repository root. See [BUILD.md](../BUILD.md) for
the build tools and dependencies required on each platform.

### Policy tests

These tests check rendering decisions, such as when an enhancement should
apply, without running the emulator or using a GPU. They are enabled by
default. To build and run them without the Qt/SDL frontend:

```sh
cmake -S . -B build-policy -DBUILD_QT_SDL=OFF -DBUILD_POLICY_TESTS=ON
cmake --build build-policy --target policy-tests
ctest --test-dir build-policy --output-on-failure
```

### Renderer and export tests

With the frontend build dependencies installed, enable the additional tests:

```sh
cmake -S . -B build -DBUILD_POLICY_TESTS=ON \
  -DBUILD_RENDERER_SHADER_TESTS=ON -DBUILD_DEBUG_EXPORT_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

For multi-configuration generators, select the same configuration when building
and testing, for example `--config Release` and `ctest -C Release`.

The renderer tests require Qt to create an OpenGL 4.3 context with compute
shader support. For headless testing, see the Mesa/Xvfb instructions in
[BUILD.md](../BUILD.md#linux). On unsupported platforms, including native
macOS OpenGL, use `-DBUILD_RENDERER_SHADER_TESTS=OFF`. Policy and export tests
can still run.

### Reading the results

CTest reports whether each test passed. `--output-on-failure` shows the output
from failing tests; the full log is in `Testing/Temporary/LastTest.log` inside
the build directory.

Some tests deliberately introduce bugs to check that the tests detect them.
These are called negative controls. Their expected failure counts as a pass
in CTest. A shader compilation error or failure to create an OpenGL context
is not the intended result and should be investigated.

## What the tests cover

- **Policy tests:** rendering decisions using fixed inputs and expected results.
- **Renderer tests:** scaling, transparency reconstruction, combining sprite
  and background pieces, widescreen output, and display capture. They exercise
  the production shaders using generated textures and buffers.
- **Renderer lifetime tests:** resource cleanup, initialization failures,
  renderer switching, and resolution changes. These run without loading user
  settings or starting audio.
- **Export tests:** writing image and CSV data, background export processing,
  and shutdown.

## Adding tests

Keep new tests self-contained so others can run them without game files or
recordings. Policy cases store input values and expected results directly in
C++; comments can explain where a case came from.

The [lift-policy-rows.ps1](policy/lift-policy-rows.ps1) utility converts a
renderer CSV into candidate policy test data. Review the generated inputs and
expected results before adding a case.

For investigating an existing debug export, you can also run
`debug-export-tests --fixture <capture-directory>`. This optional mode uses an
export you supply; the regular CTest suite does not need one.
