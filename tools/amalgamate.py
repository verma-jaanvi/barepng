#!/usr/bin/env python3
"""Concatenate all headers + sources into one translation unit for
the Single File bonus. Strips local #include lines (everything's in
one file now); leaves system includes (<...>) untouched, deduplicated.

Usage:
    python3 tools/amalgamate.py > build/imgview_single.c

Dependency order (verified from actual #include "..." lines in each header):
    bit_reader.h  (no local deps)
    huffman.h     (includes bit_reader.h)
    inflate.h     (no local deps - uses inflate_buffer_t, bit_reader is impl-only)
    zlib_wrapper.h (no local deps)
    png_unfilter.h (no local deps)
    png_decoder.h  (no local deps)
    term_render.h  (includes png_unfilter.h for png_pixels_t)

Sources follow their corresponding headers.
"""
import sys
import re
import os

# Headers in dependency order (confirmed by grepping each header's local includes)
HEADERS = [
    "include/bit_reader.h",
    "include/huffman.h",
    "include/inflate.h",
    "include/zlib_wrapper.h",
    "include/png_unfilter.h",
    "include/png_decoder.h",
    "include/term_render.h",
]

# Sources in link order (dependencies before dependents)
SOURCES = [
    "src/bit_reader.c",
    "src/huffman.c",
    "src/inflate.c",
    "src/zlib_wrapper.c",
    "src/png_unfilter.c",
    "src/png_container.c",
    "src/term_render.c",
    "src/main.c",
]

LOCAL_INCLUDE = re.compile(r'^\s*#include\s*"[^"]+"\s*$')
SYSTEM_INCLUDE = re.compile(r'^\s*#include\s*<[^>]+>\s*$')


def strip_local_includes(text, seen_system):
    """Remove local #include "..." lines (already inlined) and
    deduplicate system #include <...> lines."""
    out = []
    for line in text.splitlines(keepends=True):
        if LOCAL_INCLUDE.match(line):
            continue  # already inlined elsewhere in the amalgamation
        if SYSTEM_INCLUDE.match(line):
            key = line.strip()
            if key in seen_system:
                continue
            seen_system.add(key)
        out.append(line)
    return "".join(out)


def main():
    # Resolve paths relative to repo root (where this script is invoked from)
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    # Force UTF-8 output on Windows where the default console encoding (cp1252)
    # can't represent Unicode characters that appear in source comments (e.g. ▀).
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")

    seen_system = set()
    chunks = [
        "/* Auto-generated amalgamation - see tools/amalgamate.py.\n"
        " * Do not edit directly; edit src/ or include/ and regenerate.\n"
        " * Build: $(CC) $(CFLAGS) imgview_single.c -o imgview_single\n"
        " */\n"
    ]

    for rel_path in HEADERS + SOURCES:
        abs_path = os.path.join(root, rel_path)
        with open(abs_path, encoding="utf-8") as f:
            text = f.read()
        chunks.append(f"\n/* ---- {rel_path} ---- */\n")
        chunks.append(strip_local_includes(text, seen_system))

    sys.stdout.write("".join(chunks))


if __name__ == "__main__":
    main()
