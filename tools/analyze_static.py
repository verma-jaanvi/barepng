#!/usr/bin/env python3
"""GCC -fanalyzer static analysis runner."""
import glob
import os
import subprocess
import sys

def main():
    src_files = sorted(glob.glob("src/*.c"))
    print(f"=== Running GCC -fanalyzer on {len(src_files)} source files ===")

    all_ok = True
    os.makedirs("build", exist_ok=True)

    for src in src_files:
        obj = os.path.join("build", "analyzer_tmp.o")
        cmd = [
            "gcc", "-std=c11", "-Wall", "-Wextra", "-Wpedantic",
            "-fanalyzer", "-Iinclude", "-c", src, "-o", obj
        ]
        proc = subprocess.run(cmd, capture_output=True, text=True)
        if os.path.exists(obj):
            try:
                os.remove(obj)
            except OSError:
                pass

        output = (proc.stderr + proc.stdout).strip()
        if proc.returncode != 0 or "warning:" in output or "error:" in output:
            print(f"  ISSUES: {src}")
            for line in output.splitlines():
                print(f"    {line}")
            all_ok = False
        else:
            print(f"  OK:     {src}")

    print("\n" + ("=" * 60))
    if all_ok:
        print("GCC static analysis: ALL FILES OK (zero analyzer warnings/errors).")
        sys.exit(0)
    else:
        print("GCC static analysis: Issues detected.")
        sys.exit(1)

if __name__ == '__main__':
    main()
