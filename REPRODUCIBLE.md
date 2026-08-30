# REPRODUCIBLE.md: Reproducible Build

`make all` produces a byte-identical binary given the same toolchain and OS.

## Verified with

* **Compiler:** `gcc (Rev10, Built by MSYS2 project) 15.2.0` (MinGW-w64 UCRT64)
* **OS / arch:** Windows x86-64 (`x86_64-w64-mingw32`)

## How to verify

```bash
bash tools/verify_reproducible.sh
```

The script clones the repo twice into temporary directories, builds
independently from each, and checks that `sha256sum` of both resulting
binaries is identical.

## What's covered

* Two successive `make clean && make all` invocations from separate clones
  of the same commit produce a byte-identical `pngdecoder` binary.
* No `__DATE__`/`__TIME__` macros in `src/` or `include/`.
* `make repro` applies additional determinism flags (`-frandom-seed=0
  -fdebug-prefix-map=$(CURDIR)=.`) to suppress residual
  build-path or symbol-ordering non-determinism.

## Scope

This claim is scoped to the toolchain and OS above. Cross-compiler or
cross-platform reproducibility (e.g. MinGW vs native Linux GCC, or
x86-64 vs ARM) is not claimed or tested.
