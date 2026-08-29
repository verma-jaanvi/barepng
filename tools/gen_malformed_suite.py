#!/usr/bin/env python3
"""
Generates the committed tests/malformed/ directory containing all 51 adversarial
test cases for reproducible offline testing.
"""
import os
import sys
import zlib
import struct

OUT_DIR = os.path.join("tests", "malformed")
REF_PNG = os.path.join("demo", "icon_32x32_rgb.png")

os.makedirs(OUT_DIR, exist_ok=True)

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

def write_case(name, data):
    path = os.path.join(OUT_DIR, name)
    with open(path, 'wb') as f:
        f.write(data)
    print(f"  Created: {name} ({len(data)} bytes)")

print("=== Generating committed tests/malformed/ test suite ===")

# 1. CLI / Non-PNG
write_case("01_empty.png", b'')
write_case("02_partial_sig.png", b'\x89PNG')
write_case("03_fake_jpeg.png", b'\xFF\xD8\xFF\xE0\x00\x10JFIF\x00')
write_case("04_fake_elf.png", b'\x7FELF\x02\x01\x01\x00')
write_case("05_fake_text.png", b'This is a plain text file, not a PNG image.\n')

# 2. Container Truncations
for cut in [1, 4, 7, 8, 9, 16, 24, 33, 41, 50, 100, 58, 116]:
    if cut < len(REF):
        write_case(f"06_trunc_byte_{cut:03d}.png", REF[:cut])

iend_pos = REF.rfind(b'IEND')
if iend_pos > 0:
    write_case("07_trunc_no_iend.png", REF[:iend_pos - 4])

# 3. IHDR Dimensions & Integer Overflows
b_w0 = bytearray(REF)
b_w0[16:20] = b'\x00\x00\x00\x00'
b_w0[29:33] = struct.pack('>I', zlib.crc32(bytes(b_w0[12:29])) & 0xFFFFFFFF)
write_case("08_ihdr_width_0.png", bytes(b_w0))

b_h0 = bytearray(REF)
b_h0[20:24] = b'\x00\x00\x00\x00'
b_h0[29:33] = struct.pack('>I', zlib.crc32(bytes(b_h0[12:29])) & 0xFFFFFFFF)
write_case("09_ihdr_height_0.png", bytes(b_h0))

b_ov = bytearray(REF)
b_ov[16:20] = struct.pack('>I', 0xFFFFFFFF)
b_ov[20:24] = struct.pack('>I', 0xFFFFFFFF)
b_ov[29:33] = struct.pack('>I', zlib.crc32(bytes(b_ov[12:29])) & 0xFFFFFFFF)
write_case("10_ihdr_overflow_dim.png", bytes(b_ov))

b_max = bytearray(REF)
b_max[16:20] = struct.pack('>I', 0x80000000)
b_max[29:33] = struct.pack('>I', zlib.crc32(bytes(b_max[12:29])) & 0xFFFFFFFF)
write_case("11_ihdr_dim_2pow31.png", bytes(b_max))

# 4. Unsupported formats per SCOPE.md
dummy_idat = zlib.compress(b'\x00'*40)
write_case("12_ihdr_bitdepth_16.png", make_png(4, 4, bit_depth=16, color_type=2, idat_payload=dummy_idat))
write_case("13_ihdr_bitdepth_1.png",  make_png(4, 4, bit_depth=1,  color_type=2, idat_payload=dummy_idat))
write_case("14_ihdr_colortype_0.png", make_png(4, 4, bit_depth=8,  color_type=0, idat_payload=dummy_idat))
write_case("15_ihdr_colortype_3.png", make_png(4, 4, bit_depth=8,  color_type=3, idat_payload=dummy_idat))
write_case("16_ihdr_colortype_4.png", make_png(4, 4, bit_depth=8,  color_type=4, idat_payload=dummy_idat))
write_case("17_ihdr_interlace_adam7.png", make_png(4, 4, bit_depth=8, color_type=2, im=1, idat_payload=dummy_idat))
write_case("18_ihdr_cm_1.png",        make_png(4, 4, bit_depth=8,  color_type=2, cm=1, idat_payload=dummy_idat))
write_case("19_ihdr_fm_1.png",        make_png(4, 4, bit_depth=8,  color_type=2, fm=1, idat_payload=dummy_idat))

bad_ihdr_chunk = struct.pack('>I', 10) + b'IHDR' + b'\x00'*10 + struct.pack('>I', zlib.crc32(b'IHDR' + b'\x00'*10) & 0xFFFFFFFF)
write_case("20_ihdr_bad_length_10.png", PNG_SIG + bad_ihdr_chunk + make_chunk(b'IEND', b''))

bad_crit = PNG_SIG + make_chunk(b'IHDR', struct.pack('>IIBBBBB', 4, 4, 8, 2, 0, 0, 0)) + make_chunk(b'ZZZZ', b'critical') + make_chunk(b'IEND', b'')
write_case("21_crit_chunk_zzzz.png", bad_crit)

# 5. CRC flips
for pos in [29, 30, 31, 32]:
    b_crc = bytearray(REF)
    b_crc[pos] ^= 0xAA
    write_case(f"22_ihdr_crc_flip_byte_{pos}.png", bytes(b_crc))

# 6. Zlib & DEFLATE corruptions
valid_zhdr = b'\x78\x9c'
write_case("23_zlib_cm_1.png", make_png(4, 4, idat_payload=b'\x01\xda' + b'\x00'*20))
write_case("24_zlib_fcheck_bad.png", make_png(4, 4, idat_payload=b'\x78\x00' + b'\x00'*20))
write_case("25_zlib_fdict_set.png", make_png(4, 4, idat_payload=b'\x78\xbc' + b'\x00'*20))
write_case("26_zlib_short_header.png", make_png(4, 4, idat_payload=b'\x78'))
write_case("27_deflate_btype_11.png", make_png(4, 4, idat_payload=valid_zhdr + b'\x07' + b'\x00'*8))
write_case("28_deflate_bad_stored_nlen.png", make_png(4, 4, idat_payload=valid_zhdr + b'\x01\x05\x00\x00\x00' + b'hello' + b'\x00'*4))
write_case("29_deflate_no_bfinal_eof.png", make_png(4, 4, idat_payload=valid_zhdr + b'\x00\x00\x00\xff\xff'))
write_case("30_deflate_corrupt_dyn_hdr.png", make_png(4, 4, idat_payload=valid_zhdr + b'\x05\x00\x00\x00\x00\x00\x00'))
write_case("31_deflate_bad_backref.png", make_png(4, 4, idat_payload=valid_zhdr + b'\x03\x02\x00\x00' + b'\x00'*4))

valid_4x4_idat = zlib.compress(b'\x00' * (4 * (4 * 3 + 1)))
write_case("32_zlib_bad_adler.png", make_png(4, 4, idat_payload=valid_4x4_idat[:-4] + b'\xde\xad\xbe\xef'))

# 7. Scanline Unfilter corruptions
write_case("33_unfilter_bad_type_5.png", make_png(4, 4, idat_payload=zlib.compress(b'\x05' + b'\x00'*12 + (b'\x00'*13)*3)))
write_case("34_unfilter_bad_type_200.png", make_png(4, 4, idat_payload=zlib.compress(b'\xc8' + b'\x00'*12 + (b'\x00'*13)*3)))
write_case("35_unfilter_short_scanline.png", make_png(4, 4, idat_payload=zlib.compress(b'\x00' * (4 * 13 - 1))))
write_case("36_unfilter_long_scanline.png", make_png(4, 4, idat_payload=zlib.compress(b'\x00' * (4 * 13 + 1))))

print(f"\nGenerated all test cases in {OUT_DIR}/ successfully.")
