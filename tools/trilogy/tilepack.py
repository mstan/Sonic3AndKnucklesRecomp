"""Per-act level art packing.

Only the tiles an act can actually show are kept: tiles of blocks reached by
the act's chunks, tiles of level-art objects (platforms, ledges...), and the
zone's animated-art ranges (pinned at their original slots because the
animators write fixed tiles). Tiles are recoloured per palette line into the
S3K conventions; identical results share a slot. Everything is packed from
tile 1 upward, leaving one contiguous free region for object art.
"""


def be16(b, a):
    return (b[a] << 8) | b[a + 1]


def remap_tile(px, table):
    return bytes(((table[b >> 4] << 4) | table[b & 15]) for b in px)


class Packer:
    def __init__(self, vram, tables, limit, pinned):
        self.vram = vram
        self.tables = tables
        self.limit = limit
        self.taken = set([0])
        self.reserved = set()     # S3K art loaded there by PLC (never level art, not in end())
        self.image = bytearray(limit * 32)
        self.slot = {}            # (old tile, line) -> new tile
        self.bypix = {}           # pixel data -> new tile (sharing)
        for start, n in pinned:   # animated art: keep slots, identity colours
            for k in range(n):
                t = start + k
                self.taken.add(t)
                for line in range(4):
                    self.slot[(t, line)] = t

    def relocate(self, start, n):
        """Animated art whose original slots lie past the limit: n contiguous
        slots of its own (identity colours, like pinned art). Returns the base."""
        base = self._find(n)
        for k in range(n):
            self.taken.add(base + k)
            for line in range(4):
                self.slot[(start + k, line)] = base + k
        return base

    def _px(self, t, line):
        return remap_tile(bytes(self.vram[t * 32:t * 32 + 32]), self.tables[line])

    def place_group(self, start, n, line):
        """Contiguous run (mapping piece tiles) -> contiguous new slots."""
        if all((start + k, line) in self.slot for k in range(n)):
            first = self.slot[(start, line)]
            if all(self.slot[(start + k, line)] == first + k for k in range(n)):
                return
        base = self._find(n)
        for k in range(n):
            t = base + k
            self.taken.add(t)
            px = self._px(start + k, line)
            self.image[t * 32:t * 32 + 32] = px
            self.slot[(start + k, line)] = t

    def place_single(self, t, line):
        if (t, line) in self.slot:
            return
        px = self._px(t, line)
        if px in self.bypix:
            self.slot[(t, line)] = self.bypix[px]
            return
        n = self._find(1)
        self.taken.add(n)
        self.image[n * 32:n * 32 + 32] = px
        self.slot[(t, line)] = n
        self.bypix[px] = n

    def _find(self, n):
        t = 1
        while t + n <= self.limit:
            if all((t + k) not in self.taken and (t + k) not in self.reserved for k in range(n)):
                return t
            t += 1
        raise SystemExit("level art does not fit in VRAM")

    def reserve(self, start, n):
        self.reserved.update(range(start, start + n))

    def end(self):
        return max(self.taken) + 1

    def art(self, pinned_src):
        img = bytearray(self.image[:self.end() * 32])
        for t, px in pinned_src.items():
            if t < len(img) // 32:
                img[t * 32:t * 32 + 32] = px
        return bytes(img)
