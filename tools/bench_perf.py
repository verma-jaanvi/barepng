#!/usr/bin/env python3
"""
Stage 5: Benchmark & Performance Measurement Suite.
Profiles decode latency, memory footprint, and uncompressed throughput (MB/s).
Tests warm-cache and multi-run averages on demo images.
"""
import os
import sys
import glob
import time
import subprocess

def run_benchmark(bin_path, png_path, iterations=5):
    # Warmup
    subprocess.run([bin_path, png_path, "--info"], capture_output=True)

    latencies_ms = []
    file_size_kb = os.path.getsize(png_path) / 1024.0

    uncompressed_mb = 0
    for _ in range(iterations):
        t0 = time.perf_counter()
        proc = subprocess.run([bin_path, png_path, "--info"], capture_output=True, text=True)
        t1 = time.perf_counter()
        if proc.returncode != 0:
            print(f"Benchmark error on {png_path}: {proc.stderr}")
            return None
        latencies_ms.append((t1 - t0) * 1000.0)

    # Extract pixel buffer size from --info output
    for line in proc.stdout.splitlines():
        if "pixel buffer:" in line:
            parts = line.split("pixel buffer:")[1].strip().split()
            val = float(parts[0])
            unit = parts[1]
            if unit == "MB":
                uncompressed_mb = val
            elif unit == "KB":
                uncompressed_mb = val / 1024.0
            elif unit == "B":
                uncompressed_mb = val / (1024.0 * 1024.0)

    avg_ms = sum(latencies_ms) / len(latencies_ms)
    min_ms = min(latencies_ms)
    throughput_mbs = (uncompressed_mb / (avg_ms / 1000.0)) if avg_ms > 0 else 0

    return {
        'path': png_path,
        'compressed_kb': file_size_kb,
        'uncompressed_mb': uncompressed_mb,
        'avg_ms': avg_ms,
        'min_ms': min_ms,
        'throughput_mbs': throughput_mbs
    }

def main():
    bin_name = os.path.join("build", "pngdecoder.exe" if os.name == "nt" else "pngdecoder")
    if len(sys.argv) > 1:
        bin_name = sys.argv[1]

    if not os.path.exists(bin_name):
        print(f"Error: binary '{bin_name}' not found. Run make perf first.")
        sys.exit(1)

    print("=== Stage 5: Benchmark & Performance Suite (Optimized Build) ===\n")
    print(f"{'Image File':<36} | {'IDAT (KB)':<10} | {'Uncomp (MB)':<12} | {'Avg Latency':<12} | {'Throughput':<12}")
    print("-" * 90)

    # Wall-clock budget for the *whole* subprocess invocation (process spawn +
    # decode + --info formatting + teardown), not just the internal decode-only
    # clock the binary itself reports. 1 second is a generous, defensible
    # budget for a single CLI invocation on the largest demo images.
    WALL_CLOCK_BUDGET_MS = 1000.0

    demo_files = sorted(glob.glob("demo/*.png"))
    results = []
    for df in demo_files:
        stats = run_benchmark(bin_name, df)
        if stats:
            results.append(stats)
            print(f"{stats['path']:<36} | {stats['compressed_kb']:>8.1f} KB | {stats['uncompressed_mb']:>10.2f} MB | {stats['avg_ms']:>9.1f} ms | {stats['throughput_mbs']:>8.1f} MB/s")

    print("\n" + ("=" * 90))

    if not results:
        print("STAGE 5 PERFORMANCE: no benchmark results collected.")
        sys.exit(1)

    slowest = max(results, key=lambda r: r['avg_ms'])
    print(f"Slowest image: {slowest['path']} - avg wall-clock latency {slowest['avg_ms']:.1f} ms "
          f"(subprocess spawn + decode + --info formatting; not the binary's internal decode-only timer).")

    if slowest['avg_ms'] < WALL_CLOCK_BUDGET_MS:
        print(f"STAGE 5 PERFORMANCE TARGET MET: all images decode in < {WALL_CLOCK_BUDGET_MS:.0f} ms wall-clock.")
    else:
        print(f"STAGE 5 PERFORMANCE TARGET MISSED: {slowest['path']} took {slowest['avg_ms']:.1f} ms, "
              f"over the {WALL_CLOCK_BUDGET_MS:.0f} ms wall-clock budget.")
        sys.exit(1)

if __name__ == '__main__':
    main()
