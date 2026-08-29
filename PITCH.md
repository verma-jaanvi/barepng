# 30-Second Pitch — imgview

Memorize this. Don't read it live — glance at it in rehearsal, then say it
in your own words from memory during the demo. A read-aloud pitch always
sounds like a read-aloud pitch; a memorized one sounds like conviction.

## The script (~88 words, ~30–35s at a confident, unhurried pace)

> imgview decodes and renders PNGs from scratch in C, zero dependencies —
> no libpng, no zlib. That includes the hard part: a full DEFLATE
> decompressor, the algorithm behind gzip, written straight from the RFC —
> canonical Huffman trees, dynamic tables, all of it. It decodes a
> two-megapixel photo in about twenty milliseconds, then renders it in
> true 24-bit color right in your terminal using half-block Unicode.
> It's scoped to 8-bit RGB and RGBA on purpose — the overwhelming majority
> of real PNGs — and rejects anything else cleanly.
>
> Let me show you.

Then immediately run the demo command — the last line is your cue to stop
talking and hit enter.

## Delivery beats (where to breathe / land emphasis)

1. **"from scratch in C, zero dependencies"** — this is the headline claim.
   Say it slowly. Everything else supports it.
2. **"That includes the hard part"** — the turn. This is where a listener
   who assumed "PNG decoder" is a weekend `libpng` wrapper recalibrates.
3. **"twenty milliseconds"** — a concrete number lands harder than an
   adjective. Don't rush past it.
4. **"on purpose"** — say this like a decision, not an apology. It's the
   line that turns the scope cut into engineering judgment instead of an
   excuse a judge has to forgive.
5. **"Let me show you"** — stop talking. Silence here is fine; it's the
   handoff to the terminal.

## If you get cut off or asked to go shorter (~15s / ~40 words)

> imgview decodes and renders PNGs from scratch in C — including a
> hand-written DEFLATE decompressor, the algorithm behind gzip, built
> straight from the RFC. Zero dependencies. Twenty milliseconds for a
> two-megapixel image. Scoped to RGB/RGBA on purpose. Let me show you.

## Rehearsal checklist

- [ ] Said it from memory, not read, at least 3 times back to back
- [ ] Timed with a phone stopwatch — actually 30–35s, not estimated
- [ ] Said it standing up, at demo volume, not muttered at a screen
- [ ] Practiced the handoff: pitch ends, hands move to keyboard, command
      is already typed (or in shell history) so there's no dead air
- [ ] Have the ~15s fallback ready in case a judge interrupts with a
      question partway through
