#!/usr/bin/env python3
"""Automated master regression test runner."""
import glob
import os
import subprocess
import sys
import time

BIN = os.path.join("build", "pngdecoder.exe" if os.name == "nt" else "pngdecoder")
PYTHON = sys.executable

def step(title):
    print(f"\n========================================================")
    print(f"=== {title}")
    print(f"========================================================")

def run_cmd(cmd, desc):
    print(f"--> {desc}...")
    t0 = time.perf_counter()
    proc = subprocess.run(cmd, capture_output=True, text=True)
    t1 = time.perf_counter()
    if proc.returncode != 0:
        print(f"FAILED: {desc} (exit code {proc.returncode})")
        if proc.stdout: print(proc.stdout)
        if proc.stderr: print(proc.stderr)
        sys.exit(1)
    else:
        print(f"  OK ({t1 - t0:.2f}s)")
    return proc

def main():
    print("=== Master Regression Harness: Starting Full Verification ===\n")
    t_start = time.perf_counter()

    # Step 1: Clean build and unit tests
    step("Step 1: Clean Rebuild & Isolated Unit Tests")
    make_cmd = "mingw32-make" if os.name == "nt" else "make"
    run_cmd([make_cmd, "clean"], "make clean")
    run_cmd([make_cmd, "all"], "make all (zero warnings)")
    run_cmd([make_cmd, "check"], "make check (all primitive, bitreader, huffman, inflate, zlib, unfilter unit tests)")

    # Step 2: Decode all demo and corpus images
    step("Step 2: Decode All Demo and Corpus Images")
    corpus_files = sorted(glob.glob("demo/*.png") + glob.glob("tests/fixtures/*.png"))
    for f in corpus_files:
        proc = subprocess.run([BIN, f, "--info"], capture_output=True, text=True)
        if proc.returncode != 0:
            print(f"FAILED to decode {f}: {proc.stderr}")
            sys.exit(1)
        print(f"  PASS: {f}")
    print(f"All {len(corpus_files)} corpus files decoded successfully (exit code 0).")

    # Step 3: Stage 2a Inflate Ground Truth Verification
    step("Step 3: Stage 2a - Inflate Output Ground Truth Verification (Python zlib)")
    run_cmd([PYTHON, "tools/verify_inflate.py", BIN], "tools/verify_inflate.py")

    # Step 4: Stage 2b Pixel Buffer Ground Truth Verification
    step("Step 4: Stage 2b - Pixel Buffer Ground Truth Verification (Pillow)")
    run_cmd([PYTHON, "tools/verify_pixels.py", BIN], "tools/verify_pixels.py")

    # Step 5: Stage 2c Terminal Render & Alpha Compositing Verification
    step("Step 5: Stage 2c - ANSI Rendering, Aspect Ratio, & Alpha Compositing")
    run_cmd([PYTHON, "tools/verify_render.py", BIN], "tools/verify_render.py")

    # Step 6: Stage 3 Committed Adversarial & Malformed Suite Rejection
    step("Step 6: Stage 3 - Rejection of Committed Adversarial Suite (tests/malformed/)")
    malformed_files = sorted(glob.glob("tests/malformed/*.png"))
    if not malformed_files:
        print("Error: No malformed files found in tests/malformed/")
        sys.exit(1)

    malformed_passed = 0
    for mf in malformed_files:
        proc = subprocess.run([BIN, mf, "--info"], capture_output=True, text=True)
        if proc.returncode == 1:
            err_line = (proc.stderr or proc.stdout).strip().split('\n')[0][:70]
            print(f"  PASS (rejected): {os.path.basename(mf):<35} -> {err_line}")
            malformed_passed += 1
        elif proc.returncode == 0:
            print(f"  FAIL: {mf} unexpectedly returned exit code 0!")
            sys.exit(1)
        else:
            print(f"  CRASH: {mf} returned code {proc.returncode}!")
            sys.exit(1)
    print(f"All {malformed_passed} adversarial test cases rejected cleanly (exit code 1, zero crashes).")

    # Step 7: Stage 4 Memory Correctness & Zero Leaks
    step("Step 7: Stage 4 - Memory Correctness & Zero-Leak Verification")
    memcheck_bin = "build/test_memcheck.exe" if os.name == "nt" else "build/test_memcheck"
    if not os.path.exists(memcheck_bin):
        run_cmd([make_cmd, memcheck_bin], "build memcheck binary")
    run_cmd([PYTHON, "tools/verify_memory.py"], "tools/verify_memory.py")

    # Step 8: Static Analysis
    step("Step 8: Static Analysis (GCC -fanalyzer)")
    run_cmd([PYTHON, "tools/analyze_static.py"], "tools/analyze_static.py")

    # Step 9: Performance Benchmarking
    step("Step 9: Stage 5 - Performance Benchmarking")
    run_cmd([make_cmd, "bench"], "make bench")

    t_end = time.perf_counter()
    print("\n" + ("=" * 70))
    print(f"ALL REGRESSION CHECKS PASSED SUCCESSFULLY in {t_end - t_start:.2f}s!")
    print("=" * 70)
    sys.exit(0)

if __name__ == '__main__':
    main()
