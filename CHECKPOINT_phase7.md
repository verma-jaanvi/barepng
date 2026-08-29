# Phase 7 Checkpoint — README & demo polish

Per the plan's Phase 7 checklist (Hour 62–70). Status below, item by item,
plus one thing the plan didn't explicitly ask for that the rehearsal step
surfaced anyway.

## Checklist

- **README opening line sells it** — tightened to name gzip/zlib explicitly
  ("the same compression algorithm behind gzip and zlib") rather than just
  "written from the RFC 1951 spec," per the plan's exact wording. The rest
  of the opening (zero-dependency, no libpng/zlib, ~1,700 lines) was already
  in place from earlier work and didn't need changes.
- **Scope cut framed as a decision** — already present in the `## Scope`
  section: "covers the overwhelming majority of real-world PNGs" plus the
  explicit one-sentence justification that interlacing is additive, not a
  core format primitive. Left as-is; it already reads as a decision, not an
  apology.
- **"How it works," one paragraph each** — chunk parsing → inflate →
  unfilter → render, already written and already good (each paragraph
  names the concrete mechanism — CRC-32 Annex D, the 19-symbol code-length
  alphabet, the Paeth predictor, the ANSI escape sequences — rather than
  staying abstract). No changes needed.
- **Terminal screenshot in the README** — this was the one genuinely
  missing piece. The old README had a hand-typed ASCII-art code block, not
  an actual screenshot. Built `tools/render_screenshot.py`, which parses
  imgview's *real* ANSI truecolor output (not a mockup) and paints it into
  a terminal-chrome PNG. Captured `demo/photo_640x480_rgb.png --width 90`
  this way, saved to `docs/screenshot.png`, embedded at the top of the
  README. The script stays in `tools/` so the screenshot can be
  regenerated after any future rendering change.
- **30-second spoken pitch** — written to `PITCH.md`: an ~88-word script
  (30–35s at an unhurried pace), five delivery beats explaining *why* each
  phrase is placed where it is, a ~15s fallback if interrupted, and a
  rehearsal checklist. Still needs to actually be memorized and timed out
  loud — that part isn't something a checkpoint doc can verify for you.
- **Second full demo rehearsal, fallback path triggered on purpose** — done,
  logged in `DEMO_REHEARSAL.md`. Both fallback mechanisms were triggered
  and confirmed, not just read about in the code: the automatic degrade to
  ASCII on a non-tty stdout, and the manual `--mode=ascii` override for the
  case where truecolor technically renders but looks wrong on a given
  screen/projector. `--info` was confirmed as a working last-resort
  independently of the render path.
- **2–3 test images, exact commands memorized, `--info` fallback ready** —
  the exact command sequence is in `DEMO_REHEARSAL.md`: the 640×480 photo
  as the primary image, the 256×256 RGBA file to show alpha compositing
  isn't bolted on, and the 2048×2048 file as an optional third to make the
  speed claim concrete on a much larger input.
- **Final buffer time for a last-hour bug** — see below; this is exactly
  what the buffer is for, and it got used.

## The bug the rehearsal caught

Running the primary render command with `--width` below the image's
native width — exactly the setup the README screenshot needed — produced
a badly distorted image. `term_render.c` computed the number of output
rows as a hardcoded `height / 2` (the half-block factor), with no
relationship to how much the *columns* had been downscaled via
`--width`. Any downscaled render broke the aspect ratio; a 640×480 photo
capped to 90 columns rendered ~240 rows instead of the correct ~34.

This had shipped past `make check`, `make corpus`, and `make fuzz`
without being caught, because `make test`'s smoke test renders every demo
file at native width — the bug's precondition (`out_cols < px->width`)
was never exercised by the existing test suite. This is the direct
version of the risk the MVP-gate and kill-switch philosophy is meant to
guard against: a code path that's never actually been run end-to-end
isn't verified, no matter how reasonable it looks on inspection.

**Fix:** `row_scale` now scales by the same ratio as the column scale
(`row_scale = 2 * scale_x` instead of a hardcoded `2`), so the row count
and sampled rows track the requested downscale. Verified against three
cases (90-wide render of a 640×480 image, native-width render of a
32×32 image, 64-wide render of a 256×256 image) — all producing the
exact expected row count.

**Regression coverage added**, not just a one-off manual check:
`tools/check_render_aspect.py` asserts exact row counts for those three
cases against the built binary, and is now wired into `make test`, so
this exact bug class can't silently return.

**Re-verified after the fix:**
- `make check` — all 43 unit tests still pass (this module has no unit
  test file of its own; term_render's only real test is exercising the
  built binary, which is what `check_render_aspect.py` now does).
- `make corpus`, `make fuzz` (34/34) — clean.
- `make analyze` — `term_render.c` reports no new static-analysis issues.
- Manual ASan+UBSan sweep across all 5 demo files, at 4 different
  `--width` values each (20/60/90/200), specifically to exercise the
  fixed downscale path at multiple ratios, not just the one case used to
  discover the bug — clean, no leaks, no UB.

## Other things fixed this phase, lower-stakes but worth recording

- **`tools/fuzz_malformed.py` had hardcoded Windows backslash paths**
  (`r"build\pngdecoder.exe"`), so `make fuzz` failed outright on Linux.
  Not something this phase's checklist asked for, but it's exactly the
  kind of "demo insurance" gap Phase 6 was supposed to close, and it was
  a one-line, low-risk fix (`os.path.join` + OS-conditional binary name)
  once found. Now: 34/34 pass on Linux and should still pass unchanged on
  Windows, since the logic only branches on `os.name`.

## Known false-positive noted, deliberately not touched

`make analyze` flags three "use of uninitialized value" warnings in
`png_unfilter.c` (Sub/Average/Paeth filter loops reading `dst[x - bpp]`).
Traced through by hand: `dst` is written in strictly increasing `x` order
within the same loop, and every read is at `x - bpp < x`, so the read
always targets a position written by an earlier iteration of that exact
loop, for that exact row. This is the standard, correct PNG-unfiltering
access pattern (confirmed safe under ASan/UBSan, both this phase and in
the Phase 2b/2c/2d checkpoints). GCC's `-fanalyzer` can't prove the
loop invariant "indices `0..x-1` are initialized by iteration `x`" on
heap pointers through a ternary, which is a known category of analyzer
false positive. This is Phase 3 code, unrelated to this phase's changes
— noting it here rather than modifying already-tested, working code to
chase a static-analysis false positive with no behavioral upside.

## Known gap carried forward

This entire rehearsal — including the "does truecolor actually look
right" question — was run in a headless Linux sandbox with no attached
tty. Truecolor escape sequences were verified by inspecting the raw bytes
programmatically, not by looking at a real terminal window. **Before the
actual demo, rehearse once more in a real terminal emulator** (the one
that will actually be used, plus at least one backup) to confirm the
half-block glyphs render as expected — this is explicitly called out as
still-open in `DEMO_REHEARSAL.md`'s checklist.

## Rollback point

```
git tag phase7-readme-demo-polish
```

If anything in the final hour needs to back out to "README and demo
artifacts complete, aspect-ratio bug fixed and covered by regression
test," this is that point.

**Checkpoint passed** for what Phase 7 asked, plus one real rendering bug
found and fixed with regression coverage, plus one portability fix to the
fuzz gauntlet. The two explicitly open items — memorizing/timing the
pitch out loud, and a rehearsal in a real terminal emulator — are things
only Jahnvi can actually do; everything file-and-code-shaped is done.
