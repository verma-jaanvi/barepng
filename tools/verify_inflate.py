#!/usr/bin/env python3
"""
Stage 2a: Ground-truth Inflate Output Verification.
Extracts concatenated IDAT chunks from PNG files, decompresses with Python's zlib,
and compares byte-for-byte against pngdecoder's --dump-inflate output.
"""
import os
import sys
import glob
import zlib
import struct
import tempfile
import subprocess

def extract_idat(png_path):
    """Extract and concatenate IDAT chunk data from a PNG file."""
    with open(png_path, 'rb') as f:
        data = f.read()

    PNG_SIG = b'\x89PNG\r\n\x1a\n'
    if not data.startswith(PNG_SIG):
        raise ValueError(f"Not a valid PNG signature: {png_path}")

    offset = 8
    idat_chunks = []
    while offset + 8 <= len(data):
        length, chunk_type = struct.unpack('>I4s', data[offset:offset+8])
        offset += 8
        if offset + length + 4 > len(data):
            break
        chunk_data = data[offset:offset+length]
        offset += length + 4  # skip data + 4-byte CRC

        if chunk_type == b'IDAT':
            idat_chunks.append(chunk_data)
        elif chunk_type == b'IEND':
            break

    if not idat_chunks:
        raise ValueError(f"No IDAT chunks found in {png_path}")

    return b''.join(idat_chunks)

def verify_file(pngdecoder_bin, png_path, verbose=True):
    idat = extract_idat(png_path)
    # Python zlib ground truth
    ground_truth = zlib.decompress(idat)

    with tempfile.NamedTemporaryFile(delete=False) as tmp:
        tmp_path = tmp.name

    try:
        cmd = [pngdecoder_bin, png_path, '--dump-inflate', tmp_path, '--info']
        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if proc.returncode != 0:
            if verbose:
                print(f"  FAIL (decoder returned {proc.returncode}): {png_path}")
                print(f"       stderr: {proc.stderr.decode('utf-8', errors='replace').strip()}")
            return False

        with open(tmp_path, 'rb') as f:
            mine = f.read()

        if mine == ground_truth:
            if verbose:
                print(f"  PASS: {png_path:<40} ({len(ground_truth):>8} bytes inflated, exact match)")
            return True
        else:
            if verbose:
                print(f"  FAIL (mismatch): {png_path}")
                print(f"       expected {len(ground_truth)} bytes, got {len(mine)} bytes")
                # find first mismatch offset
                min_len = min(len(ground_truth), len(mine))
                mismatch_idx = None
                for i in range(min_len):
                    if ground_truth[i] != mine[i]:
                        mismatch_idx = i
                        break
                if mismatch_idx is not None:
                    print(f"       first mismatch at offset {mismatch_idx} (0x{mismatch_idx:x}): expected 0x{ground_truth[mismatch_idx]:02x}, got 0x{mine[mismatch_idx]:02x}")
                elif len(ground_truth) != len(mine):
                    print(f"       data matches up to prefix length {min_len}, but size differs")
            return False
    finally:
        if os.path.exists(tmp_path):
            os.remove(tmp_path)

def main():
    bin_name = 'build/pngdecoder.exe' if os.name == 'nt' else 'build/pngdecoder'
    if len(sys.argv) > 1:
        bin_name = sys.argv[1]

    if not os.path.exists(bin_name):
        print(f"Error: binary '{bin_name}' not found. Run make first.")
        sys.exit(1)

    # Collect test files
    files = sorted(glob.glob('demo/*.png') + glob.glob('tests/fixtures/*.png'))
    if not files:
        print("No PNG files found in demo/ or tests/fixtures/")
        sys.exit(1)

    print(f"=== Running Stage 2a: Ground-Truth Inflate Verification ({len(files)} files) ===")
    all_ok = True
    for f in files:
        ok = verify_file(bin_name, f, verbose=True)
        if not ok:
            all_ok = False

    print("\n" + ("=" * 60))
    if all_ok:
        print(f"ALL {len(files)} FILES PASSED: Inflate output is 100% byte-for-byte identical to zlib ground truth.")
        sys.exit(0)
    else:
        print("SOME FILES FAILED INFLATE VERIFICATION.")
        sys.exit(1)

if __name__ == '__main__':
    main()
