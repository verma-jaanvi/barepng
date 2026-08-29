# Demo Rehearsal Log — Phase 7

Second full rehearsal, run end to end against a clean build. Purpose per
the plan: confirm the exact commands, confirm the fallback path has
actually been *seen live* rather than assumed to work, and catch anything
that would only show up under real execution.

## What this rehearsal caught (worth leading with)

Running the primary render command with `--width` set below the image's
native width for the first time under realistic conditions (this is the
setup used for the README screenshot, not just `make test`'s full-width
smoke test) showed a badly distorted image — vertically stretched, way
more terminal rows than made sense for a 640×480 photo capped to 90
columns.

Root cause: `term_render.c` computed output rows as a hardcoded
`height / 2` (the half-block factor) with no relationship to how much the
*columns* had been downscaled. Any `--width` narrower than the source
image broke the aspect ratio — and this project's own `make test` target
never caught it, because it renders every demo file at full native width
(no downscale, so the bug's precondition was never hit).

Fixed in `term_render.c`: the row step now scales by the same ratio as
the column step (`row_scale = 2 * scale_x`), so `--width` downscaling
preserves aspect ratio. Added `tools/check_render_aspect.py`, wired into
`make test`, asserting exact expected row counts for three downscaled
cases. This is exactly the kind of bug a rehearsal is supposed to surface
before a judge sees it — a smoke test that never varies its parameters
doesn't count as rehearsal.

Re-ran `make check`, `make corpus`, and `make test` after the fix: all
green. (`make fuzz` fails in this Linux sandbox on an unrelated,
pre-existing issue — `tools/fuzz_malformed.py` hardcodes Windows-style
backslash paths, since it was written for the MSYS2/Windows dev
environment. Not a regression from this fix; worth a one-line portability
patch before relying on it outside Windows, but out of scope for this
phase.)

## Exact commands for the live demo (memorize these, don't type them live)

Primary path — the one to actually run in front of judges:

```
make clean && make all && make check
./build/pngdecoder demo/photo_640x480_rgb.png --info
./build/pngdecoder demo/photo_640x480_rgb.png --width 90
```

Second image, to show RGBA/alpha compositing isn't a special case bolted on:

```
./build/pngdecoder demo/alpha_256x256_rgba.png --width 70
```

Third image, if there's time for one more — the 2048×2048 file, to make
the "fast" claim concrete on a file more than an order of magnitude bigger
than the first two:

```
./build/pngdecoder demo/large_2048x2048_rgb.png --info
```

## Fallback path — triggered live, not assumed

Two independent fallbacks were exercised for real this rehearsal, not
just read about in the code:

1. **Automatic degrade.** Redirecting stdout to a file (i.e. not a live
   tty) triggers the auto-detect fallback to ASCII mode with *no flag at
   all* — confirmed by capturing output and finding ASCII luminance
   characters instead of truecolor escape codes. This is the real
   mechanism that would kick in if a judge's terminal/projector setup
   doesn't support 24-bit color or the VT-processing enable call fails.
2. **Manual override.** `--mode=ascii` forces the same degrade
   deliberately, for the case where truecolor *technically* renders but
   looks wrong (bad font, terminal emulator with broken half-block
   glyphs, color banding from a lossy screen-share codec) — a case where
   the auto-detect wouldn't know anything is wrong, only a human watching
   the screen would.
3. **`--info` as the last resort.** If the render pipeline itself hiccups
   for any reason, `--info` alone still ran cleanly and printed full
   decode stats with no rendering step at all — confirmed working
   immediately after the fallback-mode test above, not in isolation.

Also re-confirmed the plain error path stays clean under pressure: a
nonexistent file path prints `file not found: <path>` to stderr and exits
1 — no stack trace, no core dump — which matters if a wrong path gets
typed live.

## Known environment gap

This rehearsal ran in a headless Linux sandbox, not an actual terminal
emulator with a tty attached — so truecolor escape codes were verified by
inspecting raw bytes, not by eye. **The next rehearsal must happen in a
real terminal window** (per the plan's "cross-terminal-emulator testing"
item) to confirm the half-block glyphs actually render correctly, not
just that the correct bytes were emitted. Test in at least: the terminal
that will actually be used for the demo, plus one backup (e.g. Windows
Terminal + a second emulator, or whatever's available).

## Rehearsal checklist status

- [x] Clean build from scratch, `make check` green
- [x] Primary command run and timed against real output
- [x] Fallback path (auto-detect degrade) triggered live, not assumed
- [x] Fallback path (manual `--mode=ascii`) triggered live
- [x] `--info` fallback confirmed independently
- [x] Error path (missing file) confirmed clean
- [x] One real bug found and fixed as a direct result of this rehearsal
- [ ] Repeat in an actual terminal emulator with a real tty (do this
      before the final rehearsal, not after)
- [ ] Time the full spoken pitch + demo together, not just the demo
