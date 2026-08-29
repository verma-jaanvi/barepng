"""
Phase 6: Malformed-input fuzzing.
Tests that the decoder produces a clean error + exit 1 for every bad input,
never a crash, segfault, or silent wrong result.

Categories:
  1. Truncation at key boundaries (signature, IHDR, IDAT, mid-block)
  2. Bit-flip at key locations (signature, IHDR CRC, IDAT body)
  3. Structural corruption (wrong length field, bad BTYPE, extra junk)
  4. Zero-byte files / empty input
  5. Valid PNG header but deliberately invalid DEFLATE
"""
import subprocess, struct, zlib, os, sys

DECODER = os.path.join("build", "pngdecoder.exe" if os.name == "nt" else "pngdecoder")
REF_PNG = os.path.join("demo", "icon_32x32_rgb.png")

with open(REF_PNG, 'rb') as f:
    REF = f.read()

TMPFILE = os.path.join("tests", "fixtures", "malformed_tmp.png")

passed = 0
failed = 0
crashes = 0

def run(data, label):
    global passed, failed, crashes
    with open(TMPFILE, 'wb') as f:
        f.write(data)
    try:
        r = subprocess.run([DECODER, TMPFILE, "--info"],
                           capture_output=True, timeout=5)
        if r.returncode == 1:
            # Good: clean error
            msg = (r.stderr.decode(errors='replace') + r.stdout.decode(errors='replace')).strip()
            # Remove path prefix for cleaner output
            msg = msg.split('\n')[0].replace(DECODER + ': ', '').replace(TMPFILE + ': ', '')[:80]
            print(f"  PASS  {label:<45s}  -> {msg}")
            passed += 1
        elif r.returncode == 0:
            print(f"  FAIL  {label:<45s}  -> unexpectedly succeeded (exit 0)")
            failed += 1
        else:
            print(f"  FAIL  {label:<45s}  -> non-1 exit code {r.returncode}")
            failed += 1
    except subprocess.TimeoutExpired:
        print(f"  HANG  {label:<45s}  -> timed out (infinite loop?)")
        crashes += 1
    except Exception as e:
        print(f"  ERR   {label:<45s}  -> {e}")
        crashes += 1

print("=== Malformed-input gauntlet ===\n")

# --- Category 1: Truncation ---
for cut in [0, 1, 4, 7, 8, 9, 16, 24, 33, 41, 50, 100, 200, len(REF)//2, len(REF)-1]:
    if cut >= len(REF):
        continue  # not a truncation — skip
    run(REF[:cut], f"truncate at byte {cut}")

# --- Category 2: Empty / tiny ---
run(b'', "empty file (0 bytes)")
run(b'\x89PNG', "4-byte partial signature")
run(b'\x89PNG\r\n\x1a\n', "8-byte signature only, no chunks")

# --- Category 3: Bad signature ---
bad_sig = bytearray(REF)
bad_sig[0] ^= 0xFF
run(bytes(bad_sig), "bad signature byte 0")

bad_sig2 = bytearray(REF)
bad_sig2[3] = ord('J')
run(bytes(bad_sig2), "PNG->JNG signature")

# --- Category 4: CRC corruption ---
# IHDR CRC is at bytes 8+4+4+13 = 29..32
for byte_pos in [29, 30, 31, 32]:
    b = bytearray(REF)
    b[byte_pos] ^= 0xAA
    run(bytes(b), f"IHDR CRC flip at byte {byte_pos}")

# IDAT CRC: find IDAT chunk offset
cursor = 8
idat_crc_pos = None
while cursor + 8 <= len(REF):
    length = struct.unpack('>I', REF[cursor:cursor+4])[0]
    ctype  = REF[cursor+4:cursor+8]
    if ctype == b'IDAT':
        idat_crc_pos = cursor + 8 + length
        break
    cursor += 12 + length

if idat_crc_pos:
    b = bytearray(REF)
    b[idat_crc_pos] ^= 0xFF
    run(bytes(b), "IDAT CRC flip (first byte of CRC)")

# --- Category 5: IDAT body corruption (flips inside compressed data) ---
# Find IDAT data start
cursor = 8
idat_data_start = None
idat_data_len   = None
while cursor + 8 <= len(REF):
    length = struct.unpack('>I', REF[cursor:cursor+4])[0]
    ctype  = REF[cursor+4:cursor+8]
    if ctype == b'IDAT':
        idat_data_start = cursor + 8
        idat_data_len   = length
        break
    cursor += 12 + length

if idat_data_start:
    for offset in [2, 5, 10, idat_data_len//2]:
        b = bytearray(REF)
        b[idat_data_start + offset] ^= 0xFF
        run(bytes(b), f"IDAT data flip at offset +{offset}")

# --- Category 6: IHDR field corruption ---
# IHDR data starts at byte 16 (8 sig + 4 len + 4 type)
IHDR_DATA = 16

# Zero dimensions
b = bytearray(REF)
b[IHDR_DATA:IHDR_DATA+4] = b'\x00\x00\x00\x00'  # width = 0
# Recompute IHDR CRC
ihdr_chunk = bytes(b[IHDR_DATA-4:IHDR_DATA+13])  # type + data
new_crc = struct.pack('>I', zlib.crc32(ihdr_chunk) & 0xFFFFFFFF)
b[IHDR_DATA+13:IHDR_DATA+17] = new_crc
run(bytes(b), "IHDR: width=0 (with valid CRC)")

# Unsupported bit depth (already tested in Phase 5, confirm still fails)
b = bytearray(REF)
b[IHDR_DATA+8] = 16  # bit_depth = 16
ihdr_chunk = bytes(b[IHDR_DATA-4:IHDR_DATA+13])
b[IHDR_DATA+13:IHDR_DATA+17] = struct.pack('>I', zlib.crc32(ihdr_chunk) & 0xFFFFFFFF)
run(bytes(b), "IHDR: bit_depth=16 (valid CRC)")

# --- Category 7: Deliberately invalid DEFLATE stream ---
def make_simple_png(idat_payload):
    """Wrap arbitrary bytes as the IDAT payload of a 4x4 RGB PNG."""
    ihdr_data = struct.pack('>IIBBBBB', 4, 4, 8, 2, 0, 0, 0)
    def chunk(name, data):
        c = name + data
        return struct.pack('>I', len(data)) + c + struct.pack('>I', zlib.crc32(c) & 0xFFFFFFFF)
    return (bytes([137, 80, 78, 71, 13, 10, 26, 10])
            + chunk(b'IHDR', ihdr_data)
            + chunk(b'IDAT', idat_payload)
            + chunk(b'IEND', b''))

# Bad zlib header (CM != 8)
run(make_simple_png(b'\x01\xda' + b'\x00'*20), "bad zlib header (CM=1)")
# Valid zlib header but truncated DEFLATE body
valid_hdr = b'\x78\x9c'  # zlib header: CM=8, CINFO=7, no dict, default compress
run(make_simple_png(valid_hdr + b'\x00'*2), "valid zlib hdr, truncated DEFLATE")
# BTYPE=11 (reserved, invalid)
run(make_simple_png(valid_hdr + b'\x07' + b'\x00'*8), "BTYPE=11 reserved")

# --- Category 8: Missing IEND ---
# Truncate right before IEND chunk
iend_pos = REF.rfind(b'IEND')
if iend_pos > 0:
    run(REF[:iend_pos - 4], "truncated: missing IEND")

# --- Summary ---
print(f"\n{passed} passed, {failed} FAILED, {crashes} HANGS")
if TMPFILE and os.path.exists(TMPFILE):
    os.remove(TMPFILE)

sys.exit(0 if failed == 0 and crashes == 0 else 1)
