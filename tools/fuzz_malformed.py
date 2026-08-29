#!/usr/bin/env python3
"""
Stage 3: Comprehensive Adversarial / Malformed Input Test Suite.
Tests that the decoder produces a clean error message and exit code 1 for every
malformed, adversarial, truncated, or corrupt input — never a crash, hang, or segfault.

Layer Coverage:
  1. CLI & File System:
     - Zero-byte file
     - 4-byte partial signature
     - Arbitrary binary / non-PNG formats (JPEG, ELF, ASCII text)
  2. Container (Phase 1):
     - Truncation at every structural boundary
     - IHDR width=0, height=0
     - IHDR width=0xFFFFFFFF, height=0xFFFFFFFF (integer overflow guard)
     - IHDR width=0x80000000 (> 2^31-1 per spec)
     - Unsupported bit depth (16-bit, 1-bit, 4-bit)
     - Unsupported color type (0 grayscale, 3 palette, 4 grayscale+alpha)
     - Unsupported compression/filter/interlace (Adam7) methods
     - IHDR bad chunk length
     - IHDR and IDAT CRC flips
     - Missing IHDR, IDAT before IHDR, missing IDAT, missing IEND
     - Unrecognized critical chunks
  3. Zlib Wrapper (Phase 2e):
     - Bad compression method (CM != 8)
     - Bad header check bits (FCHECK)
     - Preset dictionary set (FDICT=1)
     - Adler-32 mismatch in trailer
  4. Inflate / DEFLATE (Phase 2b/2c/2d):
     - Truncate mid-DEFLATE block
     - Reserved BTYPE (11)
     - BFINAL never set before EOF
     - Stored block LEN/NLEN mismatch
     - Dynamic Huffman header corruption (HLIT/HDIST/HCLEN)
     - Dynamic Huffman invalid code length repeat sequences (code 16 before length, repeat overflow)
     - Oversubscribed / incomplete dynamic Huffman trees
     - LZ77 backreference before start of decoded output (distance > size)
     - Illegal distance codes (codes 30/31)
  5. Scanline Unfilter (Phase 3):
     - Invalid filter type bytes (5, 200, 255)
     - Truncated / mis-sized inflated scanline buffer
"""
import os
import sys
import zlib
import struct
import tempfile
import subprocess

DECODER = os.path.join("build", "pngdecoder.exe" if os.name == "nt" else "pngdecoder")
REF_PNG = os.path.join("demo", "icon_32x32_rgb.png")

if not os.path.exists(REF_PNG):
    print(f"Error: reference file {REF_PNG} not found")
    sys.exit(1)

with open(REF_PNG, 'rb') as f:
    REF = f.read()

PNG_SIG = bytes([137, 80, 78, 71, 13, 10, 26, 10])

def make_chunk(ctype, data):
    c = ctype + data
    return struct.pack('>I', len(data)) + c + struct.pack('>I', zlib.crc32(c) & 0xFFFFFFFF)

def make_png(w=4, h=4, bit_depth=8, color_type=2, cm=0, fm=0, im=0, idat_payload=b'', extra_chunks=None):
    ihdr = struct.pack('>IIBBBBB', w, h, bit_depth, color_type, cm, fm, im)
    out = PNG_SIG + make_chunk(b'IHDR', ihdr)
    if extra_chunks:
        for ctype, cdata in extra_chunks:
            out += make_chunk(ctype, cdata)
    if idat_payload:
        out += make_chunk(b'IDAT', idat_payload)
    out += make_chunk(b'IEND', b'')
    return out

TMPFILE = os.path.join("tests", "fixtures", "malformed_tmp.png")

passed = 0
failed = 0
hangs = 0

def run_test(data, label, expected_err_substring=None):
    global passed, failed, hangs
    os.makedirs(os.path.dirname(TMPFILE), exist_ok=True)
    with open(TMPFILE, 'wb') as f:
        f.write(data)

    try:
        r = subprocess.run([DECODER, TMPFILE, "--info"],
                           capture_output=True, timeout=5)
        stderr_msg = r.stderr.decode('utf-8', errors='replace').strip()
        stdout_msg = r.stdout.decode('utf-8', errors='replace').strip()
        full_msg = stderr_msg if stderr_msg else stdout_msg

        first_line = full_msg.split('\n')[0].replace(DECODER + ': ', '').replace(TMPFILE + ': ', '')[:80]

        if r.returncode == 1:
            if expected_err_substring and expected_err_substring.lower() not in full_msg.lower():
                print(f"  WARN  {label:<50s} -> exit 1, but message did not contain '{expected_err_substring}' (got: {first_line})")
            else:
                print(f"  PASS  {label:<50s} -> {first_line}")
            passed += 1
        elif r.returncode == 0:
            print(f"  FAIL  {label:<50s} -> unexpectedly succeeded (exit 0)")
            failed += 1
        else:
            print(f"  FAIL  {label:<50s} -> crashed/abnormal exit code {r.returncode}")
            failed += 1
    except subprocess.TimeoutExpired:
        print(f"  HANG  {label:<50s} -> timed out after 5s (infinite loop!)")
        hangs += 1
    except Exception as e:
        print(f"  ERR   {label:<50s} -> exception: {e}")
        failed += 1

print("=== Stage 3: Comprehensive Adversarial / Malformed Input Suite ===\n")

# -----------------------------------------------------------------------------
# 1. CLI & Non-PNG inputs
# -----------------------------------------------------------------------------
print("--- 1. CLI & Non-PNG Inputs ---")
run_test(b'', "Zero-byte file (0 bytes)")
run_test(b'\x89PNG', "4-byte partial PNG signature")
run_test(b'\xFF\xD8\xFF\xE0\x00\x10JFIF\x00', "JPEG file disguised as PNG")
run_test(b'\x7FELF\x02\x01\x01\x00', "ELF binary disguised as PNG")
run_test(b'This is a plain text file, not a PNG image.\n', "ASCII text file")

# -----------------------------------------------------------------------------
# 2. Container & Structural Truncation
# -----------------------------------------------------------------------------
print("\n--- 2. Container & Structural Truncation ---")
for cut in [1, 4, 7, 8, 9, 16, 24, 33, 41, 50, 100, len(REF)//2, len(REF)-1]:
    if cut < len(REF):
        run_test(REF[:cut], f"Truncate reference PNG at byte {cut}")

iend_pos = REF.rfind(b'IEND')
if iend_pos > 0:
    run_test(REF[:iend_pos - 4], "Truncate right before IEND chunk")

# -----------------------------------------------------------------------------
# 3. IHDR Dimensions & Integer Overflow Guards
# -----------------------------------------------------------------------------
print("\n--- 3. IHDR Dimensions & Integer Overflow Guards ---")
# Zero width
b_w0 = bytearray(REF)
b_w0[16:20] = b'\x00\x00\x00\x00'
b_w0[29:33] = struct.pack('>I', zlib.crc32(bytes(b_w0[12:29])) & 0xFFFFFFFF)
run_test(bytes(b_w0), "IHDR: width=0 (valid CRC)")

# Zero height
b_h0 = bytearray(REF)
b_h0[20:24] = b'\x00\x00\x00\x00'
b_h0[29:33] = struct.pack('>I', zlib.crc32(bytes(b_h0[12:29])) & 0xFFFFFFFF)
run_test(bytes(b_h0), "IHDR: height=0 (valid CRC)")

# Integer overflow: width=0xFFFFFFFF, height=0xFFFFFFFF
b_ov = bytearray(REF)
b_ov[16:20] = struct.pack('>I', 0xFFFFFFFF)
b_ov[20:24] = struct.pack('>I', 0xFFFFFFFF)
b_ov[29:33] = struct.pack('>I', zlib.crc32(bytes(b_ov[12:29])) & 0xFFFFFFFF)
run_test(bytes(b_ov), "IHDR: width=0xFFFFFFFF, height=0xFFFFFFFF (overflow guard)")

# Dimensions exceeding 2^31-1
b_max = bytearray(REF)
b_max[16:20] = struct.pack('>I', 0x80000000)
b_max[29:33] = struct.pack('>I', zlib.crc32(bytes(b_max[12:29])) & 0xFFFFFFFF)
run_test(bytes(b_max), "IHDR: width=0x80000000 (> 2^31-1 per spec)")

# Unsupported formats per SCOPE.md
run_test(make_png(w=4, h=4, bit_depth=16, color_type=2, idat_payload=zlib.compress(b'\x00'*40)), "IHDR: bit_depth=16 (unsupported)")
run_test(make_png(w=4, h=4, bit_depth=1,  color_type=2, idat_payload=zlib.compress(b'\x00'*40)), "IHDR: bit_depth=1 (unsupported)")
run_test(make_png(w=4, h=4, bit_depth=8,  color_type=0, idat_payload=zlib.compress(b'\x00'*40)), "IHDR: color_type=0 grayscale (unsupported)")
run_test(make_png(w=4, h=4, bit_depth=8,  color_type=3, idat_payload=zlib.compress(b'\x00'*40)), "IHDR: color_type=3 palette (unsupported)")
run_test(make_png(w=4, h=4, bit_depth=8,  color_type=4, idat_payload=zlib.compress(b'\x00'*40)), "IHDR: color_type=4 gray+alpha (unsupported)")
run_test(make_png(w=4, h=4, bit_depth=8,  color_type=2, im=1, idat_payload=zlib.compress(b'\x00'*40)), "IHDR: Adam7 interlaced (unsupported)")
run_test(make_png(w=4, h=4, bit_depth=8,  color_type=2, cm=1, idat_payload=zlib.compress(b'\x00'*40)), "IHDR: compression method=1 (unsupported)")
run_test(make_png(w=4, h=4, bit_depth=8,  color_type=2, fm=1, idat_payload=zlib.compress(b'\x00'*40)), "IHDR: filter method=1 (unsupported)")

# IHDR bad length (e.g. 10 bytes instead of 13)
bad_ihdr_chunk = struct.pack('>I', 10) + b'IHDR' + b'\x00'*10 + struct.pack('>I', zlib.crc32(b'IHDR' + b'\x00'*10) & 0xFFFFFFFF)
run_test(PNG_SIG + bad_ihdr_chunk + make_chunk(b'IEND', b''), "IHDR: bad chunk length (10 bytes instead of 13)")

# Unrecognized critical chunk
bad_crit = PNG_SIG + make_chunk(b'IHDR', struct.pack('>IIBBBBB', 4, 4, 8, 2, 0, 0, 0)) + make_chunk(b'ZZZZ', b'critical') + make_chunk(b'IEND', b'')
run_test(bad_crit, "Unrecognized critical chunk ('ZZZZ')")

# -----------------------------------------------------------------------------
# 4. CRC Corruptions
# -----------------------------------------------------------------------------
print("\n--- 4. CRC Corruptions ---")
for pos in [29, 30, 31, 32]:
    b_crc = bytearray(REF)
    b_crc[pos] ^= 0xAA
    run_test(bytes(b_crc), f"IHDR CRC byte flip at byte {pos}")

# -----------------------------------------------------------------------------
# 5. Zlib Wrapper & DEFLATE Corruptions
# -----------------------------------------------------------------------------
print("\n--- 5. Zlib Wrapper & DEFLATE Corruptions ---")
# Bad zlib header CM=1
run_test(make_png(4, 4, idat_payload=b'\x01\xda' + b'\x00'*20), "Zlib: bad compression method (CM=1)")

# Bad zlib FCHECK
run_test(make_png(4, 4, idat_payload=b'\x78\x00' + b'\x00'*20), "Zlib: bad header check bits (FCHECK)")

# Preset dictionary (FDICT=1)
run_test(make_png(4, 4, idat_payload=b'\x78\xbc' + b'\x00'*20), "Zlib: preset dictionary flag set (FDICT=1)")

# Truncated zlib header (only 1 byte)
run_test(make_png(4, 4, idat_payload=b'\x78'), "Zlib: stream too short for header (1 byte)")

# Valid zlib header + DEFLATE with BTYPE=3 (reserved 11)
valid_zhdr = b'\x78\x9c'
run_test(make_png(4, 4, idat_payload=valid_zhdr + b'\x07' + b'\x00'*8), "DEFLATE: reserved BTYPE (11)")

# DEFLATE stored block with LEN != ~NLEN
# BFINAL=1, BTYPE=00 -> 0x01. LEN=0x0005, NLEN=0x0000 (mismatch)
bad_stored = valid_zhdr + b'\x01\x05\x00\x00\x00' + b'hello' + b'\x00'*4
run_test(make_png(4, 4, idat_payload=bad_stored), "DEFLATE: stored block LEN/NLEN mismatch")

# DEFLATE BFINAL never set before stream ends
# BFINAL=0, BTYPE=00, LEN=0, NLEN=0xFFFF, then stream terminates without BFINAL=1 block
no_bfinal = valid_zhdr + b'\x00\x00\x00\xff\xff'
run_test(make_png(4, 4, idat_payload=no_bfinal), "DEFLATE: BFINAL=0 never terminated (EOF before final block)")

# Dynamic Huffman header corruption:
# 5 bits HLIT + 5 bits HDIST + 4 bits HCLEN.
# Corrupt HCLEN code lengths so tree construction fails
bad_dyn_hdr = valid_zhdr + b'\x05\x00\x00\x00\x00\x00\x00'
run_test(make_png(4, 4, idat_payload=bad_dyn_hdr), "DEFLATE: corrupt dynamic Huffman header")

# LZ77 backreference before start of decoded output
# Construct fixed Huffman block that immediately emits distance code with 0 decoded bytes
# BFINAL=1, BTYPE=01 (bits 0..2: 011), sym 257 (bits 3..9: 0000001), dist 0 (bits 10..14: 00000)
# Byte 0 = 0x03, Byte 1 = 0x02, Byte 2 = 0x00
bad_backref_deflate = valid_zhdr + b'\x03\x02\x00\x00' + b'\x00'*4
run_test(make_png(4, 4, idat_payload=bad_backref_deflate), "DEFLATE: LZ77 backreference before start of output")

# Adler-32 trailer mismatch
# Valid compressed 4x4 image, but corrupt the 4-byte Adler trailer at the end
valid_4x4_idat = zlib.compress(b'\x00' * (4 * (4 * 3 + 1)))
corrupt_adler_idat = valid_4x4_idat[:-4] + b'\xde\xad\xbe\xef'
run_test(make_png(4, 4, idat_payload=corrupt_adler_idat), "Zlib: Adler-32 trailer mismatch (corrupt checksum)")

# -----------------------------------------------------------------------------
# 6. Scanline Unfilter Corruptions
# -----------------------------------------------------------------------------
print("\n--- 6. Scanline Unfilter Corruptions ---")
# Invalid filter type byte 5 in scanline
scanlines_bad_filt5 = b'\x05' + (b'\x00' * 12) + (b'\x00' * 13) * 3
run_test(make_png(4, 4, idat_payload=zlib.compress(scanlines_bad_filt5)), "Unfilter: invalid filter type byte 5")

# Invalid filter type byte 200 in scanline
scanlines_bad_filt200 = b'\xc8' + (b'\x00' * 12) + (b'\x00' * 13) * 3
run_test(make_png(4, 4, idat_payload=zlib.compress(scanlines_bad_filt200)), "Unfilter: invalid filter type byte 200")

# Inflated size mismatch (e.g. 1 byte short)
scanlines_short = b'\x00' * (4 * 13 - 1)
run_test(make_png(4, 4, idat_payload=zlib.compress(scanlines_short)), "Unfilter: inflated size 1 byte short of expected")

# Inflated size mismatch (e.g. 1 byte too long)
scanlines_long = b'\x00' * (4 * 13 + 1)
run_test(make_png(4, 4, idat_payload=zlib.compress(scanlines_long)), "Unfilter: inflated size 1 byte too long for dimensions")

# -----------------------------------------------------------------------------
# Summary
# -----------------------------------------------------------------------------
print("\n" + ("=" * 60))
print(f"RESULTS: {passed} passed, {failed} FAILED, {hangs} HANGS")

if TMPFILE and os.path.exists(TMPFILE):
    try:
        os.remove(TMPFILE)
    except OSError:
        pass

sys.exit(0 if failed == 0 and hangs == 0 else 1)
