#!/usr/bin/env python3
"""Amalgamate headers and source files into a single translation unit."""
import sys
import re
import os

HEADERS = [
    "include/bit_reader.h",
    "include/huffman.h",
    "include/inflate.h",
    "include/zlib_wrapper.h",
    "include/png_unfilter.h",
    "include/png_decoder.h",
    "include/term_render.h",
]

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
    out = []
    for line in text.splitlines(keepends=True):
        if LOCAL_INCLUDE.match(line):
            continue
        if SYSTEM_INCLUDE.match(line):
            key = line.strip()
            if key in seen_system:
                continue
            seen_system.add(key)
        out.append(line)
    return "".join(out)


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")

    seen_system = set()
    chunks = [
        "/* Amalgamated source - generated via tools/amalgamate.py */\n"
    ]

    for rel_path in HEADERS + SOURCES:
        abs_path = os.path.join(root, rel_path)
        with open(abs_path, encoding="utf-8") as f:
            text = f.read()
        chunks.append(f"\n/* {rel_path} */\n")
        chunks.append(strip_local_includes(text, seen_system))

    sys.stdout.write("".join(chunks))


if __name__ == "__main__":
    main()
