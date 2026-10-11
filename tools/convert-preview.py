"""Losslessly convert our 32-bit BMP test renders to PNG, using only the standard library."""
import struct
import sys
import zlib
from pathlib import Path


def convert(path):
    source = path.read_bytes()
    offset = struct.unpack_from('<I', source, 10)[0]
    width, height = struct.unpack_from('<ii', source, 18)
    bits = struct.unpack_from('<H', source, 28)[0]
    if source[:2] != b'BM' or bits != 32 or width <= 0:
        raise ValueError('Expected an uncompressed 32-bit BMP')
    rows = []
    for y in range(abs(height)):
        sy = y if height < 0 else height - y - 1
        row = source[offset + sy * width * 4:offset + (sy + 1) * width * 4]
        rgba = bytearray(row)
        rgba[0::4], rgba[2::4] = row[2::4], row[0::4]
        rows.append(b'\0' + rgba)

    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))

    output = b'\x89PNG\r\n\x1a\n'
    output += chunk(b'IHDR', struct.pack('>IIBBBBB', width, abs(height), 8, 6, 0, 0, 0))
    output += chunk(b'IDAT', zlib.compress(b''.join(rows))) + chunk(b'IEND', b'')
    path.with_suffix('.png').write_bytes(output)


if __name__ == '__main__':
    for arg in sys.argv[1:]:
        convert(Path(arg))
