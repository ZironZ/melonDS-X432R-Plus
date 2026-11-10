# JIT regressions

Enable `BUILD_JIT_TESTS` in a frontend build with `ENABLE_JIT=ON`. It defaults
to the value of `BUILD_DEBUG_EXPORT_TESTS`, so the existing release CI runs it
on Windows, Linux and both macOS architectures. No ROM, BIOS dump, user
configuration, audio device, or graphics context is needed.

```sh
cmake --build build
ctest --test-dir build -R '^jit-' --output-on-failure
```

The four test groups exercise production CPU dispatch and generated code:

- `jit-literals`: ARM9/ARM7 Thumb PC-relative loads at both instruction
  alignments, word/halfword/byte literal changes, folding and branch
  optimizations on/off, requested fastmem on/off, and interpreter controls.
  Every result is checked twice to exercise compilation and cached execution.
  CPU serialization and main RAM also round-trip through `Savestate`, followed
  by a JIT reset. This isolated fixture does not load frontend device state;
  full console savestate loading is covered separately by release game replays.
- `jit-invalidation`: normal memory writes must invalidate overlapping code
  while preserving a disjoint compiled block in the same 512-byte range.
- `jit-branches`: dirty all general registers before a skipped conditional
  PC write. All fourteen ARM conditions match interpreter registers/flags on
  both emulated CPUs, with branch optimization and requested fastmem on/off.
  Static code-memory slices are reserved in one pass so the normal allocator
  also exercises dynamically allocated code and far calls on x64 hosts.
- `jit-faults`: a fault at a real generated-code address must be rejected
  before consulting mapping tables when fastmem is off. On POSIX hosts,
  child processes also check forwarding non-access SIGSEGV signals to a
  previous handler and restoring the default signal action.

Unsupported platforms still disable fastmem; the tests verify the effective
setting rather than assuming that a request enabled it. A Windows run does
not validate the AArch64 backend or POSIX signal behavior.

Sources: [upstream PR #2747](https://github.com/melonDS-emu/melonDS/pull/2747)
and [issue #2782](https://github.com/melonDS-emu/melonDS/issues/2782), including
its corrected macOS fault-handler diagnosis. The Thumb alignment correction
is applied to both backends: the new guest reproduces the equivalent x64
second-lookup failure as well.
