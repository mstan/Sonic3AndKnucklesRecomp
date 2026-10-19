"""Bounded Sonic CD BIN/CUE reader.

Adapted from sonsegajp/SonicAnthology (8f5726a), PolyForm Noncommercial
1.0.0. Supports one MODE1/2352 data track followed by BINARY AUDIO tracks.
INDEX 00 is stored pregap; PREGAP is virtual and consumes no file bytes.
Other sector/track formats are rejected explicitly, never guessed.
"""
from dataclasses import dataclass
from pathlib import Path
import re
import struct

RAW, USER = 2352, 2048
MAX_READ = 128 * 1024 * 1024


@dataclass(frozen=True)
class Track:
    number: int
    kind: str
    path: Path
    start: int
    end: int
    pregap: int = 0

    @property
    def size(self):
        return self.end - self.start


def _time(value):
    m = re.fullmatch(r'(\d+):(\d{2}):(\d{2})', value)
    if not m:
        raise ValueError('invalid CUE timestamp: ' + value)
    mm, ss, ff = map(int, m.groups())
    if ss >= 60 or ff >= 75 or mm > 999:
        raise ValueError('invalid CUE timestamp: ' + value)
    return ((mm * 60 + ss) * 75 + ff) * RAW


def cue_tracks(cue):
    """Parse and bound every track before opening the filesystem."""
    cue = Path(cue)
    current_file, current = None, None
    entries = []
    for line_no, line in enumerate(cue.read_text(encoding='utf-8-sig').splitlines(), 1):
        line = line.strip()
        if not line:
            continue
        cmd, _, args = line.partition(' ')
        # Tabs are legal whitespace in CUE sheets.
        parts = line.split(None, 1)
        cmd, args = parts[0].upper(), parts[1] if len(parts) == 2 else ''
        if cmd in ('REM', 'TITLE', 'PERFORMER', 'SONGWRITER', 'CATALOG', 'ISRC', 'FLAGS', 'CDTEXTFILE'):
            continue
        if cmd == 'FILE':
            m = re.fullmatch(r'"([^"]+)"\s+BINARY', args, re.I)
            if not m:
                raise ValueError('only quoted BINARY FILE tracks are supported')
            current_file = (cue.parent / m[1]).resolve(strict=True)
            current = None
        elif cmd == 'TRACK':
            m = re.fullmatch(r'(\d+)\s+(MODE1/2352|AUDIO)', args, re.I)
            if not current_file or not m:
                raise ValueError('only MODE1/2352 and AUDIO tracks are supported')
            number = int(m[1])
            if number != len(entries) + 1 or number > 99:
                raise ValueError('tracks must be numbered consecutively from 01')
            current = dict(number=number, kind=m[2].upper(), path=current_file, indexes={}, pregap=0)
            entries.append(current)
        elif cmd == 'INDEX':
            m = re.fullmatch(r'(00|01)\s+(\S+)', args)
            if current is None or not m or int(m[1]) in current['indexes']:
                raise ValueError('missing track, duplicate or unsupported INDEX')
            current['indexes'][int(m[1])] = _time(m[2])
        elif cmd == 'PREGAP':
            if current is None or current.get('has_pregap'):
                raise ValueError('missing track or duplicate PREGAP')
            current['pregap'] = _time(args)
            current['has_pregap'] = True
        else:
            raise ValueError(f'unsupported CUE command at line {line_no}: {cmd}')
    if not entries or entries[0]['kind'] != 'MODE1/2352' or any(t['kind'] != 'AUDIO' for t in entries[1:]):
        raise ValueError('expected one MODE1/2352 data track followed by AUDIO tracks')
    tracks = []
    finished_files = set()
    for i, t in enumerate(entries):
        indexes = t['indexes']
        if 1 not in indexes or (0 in indexes and indexes[0] > indexes[1]):
            raise ValueError('missing INDEX 01 or reversed pregap')
        size = t['path'].stat().st_size
        if size % RAW:
            raise ValueError('truncated raw sector: ' + str(t['path']))
        if t['path'] in finished_files:
            raise ValueError('non-contiguous reuse of a CUE FILE')
        end = size
        if i + 1 < len(entries) and entries[i + 1]['path'] == t['path']:
            following = entries[i + 1]['indexes']
            if 1 not in following:
                raise ValueError('missing INDEX 01')
            end = following.get(0, following[1])
        else:
            finished_files.add(t['path'])
        start = indexes[1]
        if not 0 <= indexes.get(0, start) <= start < end <= size:
            raise ValueError('track bounds overlap, are empty or exceed the BIN')
        tracks.append(Track(t['number'], t['kind'], t['path'], start, end, t['pregap']))
    return tracks


def _both(data, offset, width):
    little = int.from_bytes(data[offset:offset + width], 'little')
    big = int.from_bytes(data[offset + width:offset + width * 2], 'big')
    if little != big:
        raise ValueError('inconsistent ISO both-endian field')
    return little


class Disc:
    def __init__(self, cue):
        self.cue = Path(cue)
        self.track_info = cue_tracks(cue)
        # Retain the reference inspector's four-column interface.
        self.tracks = [(t.number, t.kind, t.path, t.start) for t in self.track_info]
        self._track = self.track_info[0]
        self._sectors = self._track.size // RAW
        pvd = self.sector(16)
        if pvd[:7] != b'\x01CD001\x01':
            raise ValueError('no ISO 9660 primary volume descriptor')
        if _both(pvd, 128, 2) != USER:
            raise ValueError('unsupported ISO logical block size')
        volume = _both(pvd, 80, 4)
        if not 17 <= volume <= self._sectors:
            raise ValueError('ISO volume exceeds the data track')
        self._sectors = volume
        self.volume_id = pvd[40:72].decode('ascii').rstrip(' \0')
        self.files = {}
        self._visited = set()
        root = pvd[156:190]
        if root[0] != 34 or root[32:34] != b'\x01\0' or not root[25] & 2:
            raise ValueError('invalid ISO root record')
        self._walk(root, '', 0)

    def sector(self, lba, count=1):
        if (not isinstance(lba, int) or not isinstance(count, int) or lba < 0 or count < 0
                or lba > self._sectors or count > self._sectors - lba or count > MAX_READ // USER):
            raise ValueError('sector read outside data track/volume or allocation limit')
        out = bytearray()
        with self._track.path.open('rb') as f:
            for i in range(count):
                f.seek(self._track.start + (lba + i) * RAW + 16)
                user = f.read(USER)
                if len(user) != USER:
                    raise ValueError('truncated data sector')
                out.extend(user)
        return bytes(out)

    def _extent(self, rec):
        if len(rec) < 34 or rec[0] != len(rec) or rec[1] or rec[26] or rec[27] or rec[25] & 0x80:
            raise ValueError('invalid or unsupported ISO extent record')
        lba, size = _both(rec, 2, 4), _both(rec, 10, 4)
        if _both(rec, 28, 2) != 1:
            raise ValueError('unsupported ISO volume sequence')
        count = (size + USER - 1) // USER
        if lba > self._sectors or count > self._sectors - lba or size > MAX_READ:
            raise ValueError('ISO extent outside volume or allocation limit')
        return lba, size

    def _walk(self, rec, prefix, depth):
        lba, size = self._extent(rec)
        if depth > 16 or len(self._visited) >= 1024 or (lba, size) in self._visited:
            raise ValueError('cyclic, duplicate or excessively deep ISO directories')
        self._visited.add((lba, size))
        blob = self.sector(lba, (size + USER - 1) // USER)[:size]
        p = 0
        while p < size:
            n = blob[p]
            if not n:
                p = (p // USER + 1) * USER
                continue
            if n < 34 or n & 1 or n > size - p or n > USER - p % USER:
                raise ValueError('malformed or sector-crossing ISO directory record')
            r = blob[p:p + n]
            if not r[32] or 33 + r[32] > n:
                raise ValueError('invalid ISO filename length')
            extent = self._extent(r)
            name = r[33:33 + r[32]]
            if name not in (b'\0', b'\1'):
                name = name.decode('ascii').split(';')[0]
                if not name or name in ('.', '..') or any(c in name for c in '/\\:\0'):
                    raise ValueError('unsafe ISO filename')
                name = prefix + name
                if r[25] & 2:
                    self._walk(r, name + '/', depth + 1)
                else:
                    if name in self.files or len(self.files) >= 16384:
                        raise ValueError('duplicate or excessive ISO files')
                    self.files[name] = extent
            p += n

    def read(self, name):
        lba, size = self.files[name]
        return self.sector(lba, (size + USER - 1) // USER)[:size]

    def system_area(self):
        return self.sector(0, 16)

    def audio(self, number):
        for t in self.track_info:
            if t.number == number and t.kind == 'AUDIO':
                if t.size > MAX_READ:
                    raise ValueError('audio track exceeds allocation limit')
                with t.path.open('rb') as f:
                    f.seek(t.start)
                    data = f.read(t.size)
                if len(data) != t.size:
                    raise ValueError('truncated audio track')
                return data
        raise KeyError(number)


if __name__ == '__main__':
    import argparse
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('cue')
    args = ap.parse_args()
    disc = Disc(args.cue)
    for name, (lba, size) in sorted(disc.files.items(), key=lambda kv: kv[1][0]):
        print(f'{name:24} lba {lba:6} {size:8}')
