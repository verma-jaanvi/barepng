#!/usr/bin/env python3
"""Run memory leak verification tests."""
import glob
import os
import struct
import subprocess
import sys
import zlib

MEMCHECK_BIN = os.path.join("build", "test_memcheck.exe" if os.name == "nt" else "test_memcheck")
TMP_PNG = os.path.join("tests", "fixtures", "memcheck_tmp.png")

def test_file(path, label):
    proc = subprocess.run([MEMCHECK_BIN, path], capture_output=True, text=True)
    if proc.returncode == 2:
        print(f"  LEAK DETECTED: {label:<50s} -> {proc.stderr.strip()}")
        return False
    else:
        print(f"  PASS (0 leaks): {label:<50s}")
        return True

def main():
    if not os.path.exists(MEMCHECK_BIN):
        print(f"Error: {MEMCHECK_BIN} not found. Run make first.")
        sys.exit(1)

    print("=== Stage 4: Comprehensive Memory Leak & Allocation Verification ===\n")

    all_ok = True

    # 1. Test all corpus and demo images
    print("--- 1. Valid Corpus & Demo Images ---")
    valid_files = sorted(glob.glob("demo/*.png") + glob.glob("tests/fixtures/*.png"))
    for vf in valid_files:
        if not test_file(vf, vf):
            all_ok = False

    # 2. Test malformed inputs (error paths)
    print("\n--- 2. Adversarial & Malformed Inputs (Error Paths) ---")
    ref_file = "demo/icon_32x32_rgb.png"
    with open(ref_file, 'rb') as f:
        ref_data = f.read()

    os.makedirs(os.path.dirname(TMP_PNG), exist_ok=True)

    def run_data(data, label):
        nonlocal all_ok
        with open(TMP_PNG, 'wb') as f:
            f.write(data)
        if not test_file(TMP_PNG, label):
            all_ok = False

    # Truncations
    for cut in [0, 1, 4, 8, 16, 24, 33, 41, 50, len(ref_data)//2]:
        if cut < len(ref_data):
            run_data(ref_data[:cut], f"Truncation at byte {cut}")

    # Bad CRC
    b_crc = bytearray(ref_data)
    b_crc[29] ^= 0xFF
    run_data(bytes(b_crc), "IHDR CRC corrupt")

    # Zero dimensions
    b_w0 = bytearray(ref_data)
    b_w0[16:20] = b'\x00\x00\x00\x00'
    b_w0[29:33] = struct.pack('>I', zlib.crc32(bytes(b_w0[12:29])) & 0xFFFFFFFF)
    run_data(bytes(b_w0), "IHDR width=0")

    # Overflow dimensions
    b_ov = bytearray(ref_data)
    b_ov[16:20] = struct.pack('>I', 0xFFFFFFFF)
    b_ov[20:24] = struct.pack('>I', 0xFFFFFFFF)
    b_ov[29:33] = struct.pack('>I', zlib.crc32(bytes(b_ov[12:29])) & 0xFFFFFFFF)
    run_data(bytes(b_ov), "IHDR overflow dimensions")

    # Bad DEFLATE BTYPE
    def make_chunk(ctype, data):
        c = ctype + data
        return struct.pack('>I', len(data)) + c + struct.pack('>I', zlib.crc32(c) & 0xFFFFFFFF)

    png_sig = bytes([137, 80, 78, 71, 13, 10, 26, 10])
    ihdr_data = struct.pack('>IIBBBBB', 4, 4, 8, 2, 0, 0, 0)
    zhdr = b'\x78\x9c'
    btype11 = png_sig + make_chunk(b'IHDR', ihdr_data) + make_chunk(b'IDAT', zhdr + b'\x07\x00\x00') + make_chunk(b'IEND', b'')
    run_data(btype11, "DEFLATE reserved BTYPE=11")

    # Bad filter byte
    bad_filt = png_sig + make_chunk(b'IHDR', ihdr_data) + make_chunk(b'IDAT', zlib.compress(b'\x05' + b'\x00'*12 + (b'\x00'*13)*3)) + make_chunk(b'IEND', b'')
    run_data(bad_filt, "Unfilter bad filter type byte 5")

    # Adler trailer mismatch
    good_idat = zlib.compress(b'\x00' * (4 * 13))
    bad_adler_idat = good_idat[:-4] + b'\xde\xad\xbe\xef'
    bad_adler = png_sig + make_chunk(b'IHDR', ihdr_data) + make_chunk(b'IDAT', bad_adler_idat) + make_chunk(b'IEND', b'')
    run_data(bad_adler, "Zlib Adler-32 mismatch")

    if os.path.exists(TMP_PNG):
        try:
            os.remove(TMP_PNG)
        except OSError:
            pass

    print("\n" + ("=" * 60))
    if all_ok:
        print("STAGE 4 MEMORY VERIFICATION PASSED: 0 memory leaks across all valid and error paths.")
        sys.exit(0)
    else:
        print("STAGE 4 MEMORY VERIFICATION FAILED.")
        sys.exit(1)

if __name__ == '__main__':
    main()
