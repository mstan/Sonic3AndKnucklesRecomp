"""Versioned private CD resources; no copyrighted bytes are distributed.

SCDP content and SCDA music use a 64-byte header, count*28-byte directory,
4-byte-aligned items and a CDP1 length/CRC footer. Each item also has a CRC.
The qualified source identity is the SHA256 of ISO user sectors in volume
order, independent of CUE pregaps and shared/separate track storage.
"""
from pathlib import Path
import os
import struct
import zlib

US_SHA256 = 'db17110d5c9f80e7a0ec92ead5b23c9e524b53df9ff6b5069c7352e1d357925f'
WINDOW, WINDOW_SIZE = 0x480000, 0x180000
PCM_NAMES = tuple(f'SNCBNK{r}B.BIN' for r in (1, 3, 4, 5, 6, 7, 8)) + ('SNCBNKB1.BIN', 'SNCBNKB2.BIN')
CONTENT_NAMES = ('LEVELS', 'SCENES') + PCM_NAMES
AUDIO_NAMES = tuple(f'TRACK{n:02}' for n in range(2, 36))
HEADER, ENTRY = 64, 28


def _crc_file(f, size):
    f.seek(0)
    crc = 0
    while size:
        part = f.read(min(size, 1024 * 1024))
        if not part:
            raise ValueError('truncated CD resource')
        crc = zlib.crc32(part, crc)
        size -= len(part)
    return crc


def write_pack(path, magic, entries, source_sha256=US_SHA256):
    names = CONTENT_NAMES if magic == b'SCDP' else AUDIO_NAMES if magic == b'SCDA' else ()
    if not names or source_sha256 != US_SHA256:
        raise ValueError('unsupported CD resource/source revision')
    with Path(path).open('w+b') as f:
        f.write(struct.pack('>4sHH32sII16x', magic, 1, len(names), bytes.fromhex(source_sha256), WINDOW, WINDOW_SIZE))
        f.write(bytes(len(names) * ENTRY))
        directory = bytearray()
        actual = []
        for name, data in entries:
            actual.append(name)
            while f.tell() & 3:
                f.write(b'\0')
            directory += struct.pack('>16sIII', name.encode('ascii'), f.tell(), len(data), zlib.crc32(data))
            f.write(data)
        if tuple(actual) != names:
            raise ValueError('incomplete or reordered CD resource directory')
        size = f.tell()
        f.seek(HEADER)
        f.write(directory)
        crc = _crc_file(f, size)
        f.seek(size)
        f.write(struct.pack('>4sII', b'CDP1', size, crc))
        f.flush()
        os.fsync(f.fileno())


def validate_pack(path, audio=False):
    names = AUDIO_NAMES if audio else CONTENT_NAMES
    with Path(path).open('rb') as f:
        size = f.seek(0, 2)
        if size < HEADER + len(names) * ENTRY + 12 or size > (700 if audio else 32) * 1024 * 1024:
            raise ValueError('invalid CD resource size')
        f.seek(0)
        magic, version, count, source, base, window = struct.unpack('>4sHH32sII16x', f.read(HEADER))
        if (magic != (b'SCDA' if audio else b'SCDP') or version != 1 or count != len(names)
                or source.hex() != US_SHA256 or base != WINDOW or window != WINDOW_SIZE):
            raise ValueError('unsupported CD resource header or source revision')
        f.seek(48)
        if any(f.read(16)):
            raise ValueError('unsupported CD resource capability')
        directory = f.read(count * ENTRY)
        f.seek(size - 12)
        footer, payload, crc = struct.unpack('>4sII', f.read(12))
        if footer != b'CDP1' or payload != size - 12 or _crc_file(f, payload) != crc:
            raise ValueError('CD resource integrity check failed')
        end = HEADER + count * ENTRY
        result = {}
        for i, expected in enumerate(names):
            name, offset, length, item_crc = struct.unpack_from('>16sIII', directory, i * ENTRY)
            if (name != expected.encode().ljust(16, b'\0') or offset < end or offset & 3
                    or not length or offset > payload or length > payload - offset):
                raise ValueError('invalid CD resource item span or identity')
            if offset != ((end + 3) & ~3):
                raise ValueError('unexpected gap/overlap in CD resource')
            f.seek(end)
            if any(f.read(offset - end)):
                raise ValueError('nonzero CD resource padding')
            f.seek(offset)
            remaining, actual = length, 0
            while remaining:
                part = f.read(min(remaining, 1024 * 1024))
                if not part:
                    raise ValueError('truncated CD item')
                actual = zlib.crc32(part, actual)
                remaining -= len(part)
            if actual != item_crc or (audio and length % 2352):
                raise ValueError('CD item integrity/PCM frame check failed')
            result[expected] = (offset, length)
            end = offset + length
        if end != payload:
            raise ValueError('trailing CD resource bytes')
        return result
