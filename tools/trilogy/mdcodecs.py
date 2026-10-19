"""Mega Drive Sonic compression formats: Nemesis / Enigma / Kosinski decoders,
Kosinski + KosM encoders.

Written from the decoder semantics of the retail cartridges (S3K Kos_Decomp,
Queue_Kos_Module); every encoder output is round-tripped through the decoder
here before it is accepted.
"""


# ---------------------------------------------------------------- Nemesis
def nemesis_decode(data, pos=0):
    """Return (bytes, end_pos)."""
    hdr = (data[pos] << 8) | data[pos + 1]
    pos += 2
    xor_mode = bool(hdr & 0x8000)
    ntiles = hdr & 0x7FFF
    table = {}
    nib = 0
    b = data[pos]; pos += 1
    while b != 0xFF:
        if b & 0x80:
            nib = b & 0x0F
            b = data[pos]; pos += 1
            continue
        count = ((b >> 4) & 7) + 1
        length = b & 0x0F
        code = data[pos]; pos += 1
        table[(length, code)] = (nib, count)
        b = data[pos]; pos += 1
    total_nibbles = ntiles * 64
    out_nibs = []
    bitpos = pos * 8

    def bit():
        nonlocal bitpos
        v = (data[bitpos >> 3] >> (7 - (bitpos & 7))) & 1
        bitpos += 1
        return v

    while len(out_nibs) < total_nibbles:
        code = 0
        length = 0
        while True:
            code = (code << 1) | bit()
            length += 1
            if length == 6 and code == 0x3F:
                v = 0
                for _ in range(7):
                    v = (v << 1) | bit()
                cnt = (v >> 4) + 1
                out_nibs.extend([v & 0xF] * cnt)
                break
            hit = table.get((length, code))
            if hit:
                out_nibs.extend([hit[0]] * hit[1])
                break
            if length > 8:
                raise ValueError("bad nemesis code at bit %d" % bitpos)
    out_nibs = out_nibs[:total_nibbles]
    out = bytearray()
    prev = 0
    for r in range(ntiles * 8):
        row = 0
        for n in out_nibs[r * 8:r * 8 + 8]:
            row = (row << 4) | n
        if xor_mode:
            row ^= prev
            prev = row
        out += row.to_bytes(4, 'big')
    end = (bitpos + 7) >> 3
    return bytes(out), end


# ---------------------------------------------------------------- Enigma
def enigma_decode(data, pos=0, start_tile=0):
    nbits = data[pos]
    flags = data[pos + 1]
    incr = ((data[pos + 2] << 8) | data[pos + 3])
    common = ((data[pos + 4] << 8) | data[pos + 5])
    bitpos = (pos + 6) * 8
    out = []

    def bits(n):
        nonlocal bitpos
        v = 0
        for _ in range(n):
            v = (v << 1) | ((data[bitpos >> 3] >> (7 - (bitpos & 7))) & 1)
            bitpos += 1
        return v

    def inline():
        v = 0
        for fb, wb in ((4, 15), (3, 14), (2, 13), (1, 12), (0, 11)):
            if flags & (1 << fb):
                if bits(1):
                    v |= 1 << wb
        v |= bits(nbits)
        return (v + start_tile) & 0xFFFF

    while True:
        if bits(1) == 0:
            if bits(1) == 0:
                cnt = bits(4) + 1
                for _ in range(cnt):
                    out.append(incr); incr = (incr + 1) & 0xFFFF
            else:
                cnt = bits(4) + 1
                out.extend([common] * cnt)
        else:
            mode = bits(2)
            cnt = bits(4)
            if mode == 0:
                v = inline()
                out.extend([v] * (cnt + 1))
            elif mode == 1:
                v = inline()
                for _ in range(cnt + 1):
                    out.append(v); v = (v + 1) & 0xFFFF
            elif mode == 2:
                v = inline()
                for _ in range(cnt + 1):
                    out.append(v); v = (v - 1) & 0xFFFF
            else:
                if cnt == 0xF:
                    break
                for _ in range(cnt + 1):
                    out.append(inline())
    return b''.join(w.to_bytes(2, 'big') for w in out)


# ---------------------------------------------------------------- Kosinski
def kosinski_decode(data, pos=0):
    """Exact model of S3K Kos_Decomp. Returns (bytes, end_pos)."""
    out = bytearray()
    desc = data[pos] | (data[pos + 1] << 8); pos += 2
    left = 16

    def getbit():
        nonlocal desc, left, pos
        b = desc & 1
        desc >>= 1
        left -= 1
        if left == 0:
            desc = data[pos] | (data[pos + 1] << 8); pos += 2
            left = 16
        return b

    while True:
        if getbit():
            out.append(data[pos]); pos += 1
            continue
        if getbit():
            lo = data[pos]; hi = data[pos + 1]; pos += 2
            off = (0xE000 | ((hi & 0xF8) << 5) | lo) - 0x10000
            cnt = hi & 7
            if cnt:
                cnt += 2
            else:
                c = data[pos]; pos += 1
                if c == 0:
                    break
                if c == 1:
                    continue
                cnt = c + 1
        else:
            cnt = (getbit() << 1)
            cnt |= getbit()
            cnt += 2
            off = data[pos] - 0x100; pos += 1
        for _ in range(cnt):
            out.append(out[len(out) + off])
    return bytes(out), pos


def kosinski_encode(src):
    """Greedy LZ Kosinski encoder honouring the immediate-descriptor-refetch
    quirk (a new descriptor follows the 16th consumed bit at once)."""
    src = bytes(src)
    out = bytearray()
    desc_at = 0
    out += b'\0\0'
    nbits = 0
    desc = 0

    def putbit(b):
        nonlocal desc, nbits, desc_at
        if b:
            desc |= 1 << nbits
        nbits += 1
        if nbits == 16:
            out[desc_at] = desc & 0xFF
            out[desc_at + 1] = desc >> 8
            desc_at = len(out)
            out.extend(b'\0\0')
            desc = 0
            nbits = 0

    # hash chains for 3-byte prefixes
    heads = {}
    n = len(src)
    i = 0
    WINDOW = 0x2000
    MAXLEN = 0x100

    def insert(k):
        if k + 2 < n:
            key = src[k:k + 3]
            heads.setdefault(key, []).append(k)

    while i < n:
        best_len = 0
        best_off = 0
        # short inline candidates (2..5 bytes, offset up to 256)
        if i + 1 < n:
            key = src[i:i + 3] if i + 2 < n else None
            cands = heads.get(key, []) if key else []
            for tries, j in enumerate(reversed(cands)):
                if i - j > WINDOW or tries > 96:
                    break
                l = 0
                lim = min(MAXLEN, n - i)
                while l < lim and src[j + l] == src[i + l]:
                    l += 1
                if l > best_len:
                    best_len, best_off = l, i - j
                    if l == lim:
                        break
            if best_len < 2:
                # 2-byte match only usable inline (offset <= 256)
                for j in range(max(0, i - 256), i):
                    if src[j] == src[i] and src[j + 1] == src[i + 1]:
                        best_len, best_off = 2, i - j
        use = False
        if best_len >= 2 and best_off <= 256 and best_len <= 5:
            putbit(0); putbit(0)
            c = best_len - 2
            putbit((c >> 1) & 1); putbit(c & 1)
            out.append((0x100 - best_off) & 0xFF)
            use = True
        elif best_len >= 3:
            putbit(0); putbit(1)
            off = (0x2000 - best_off)  # 13-bit two's complement
            lo = off & 0xFF
            hi5 = (off >> 8) & 0x1F
            if best_len <= 9:
                out.append(lo)
                out.append((hi5 << 3) | (best_len - 2))
            else:
                out.append(lo)
                out.append(hi5 << 3)
                out.append(best_len - 1)
            use = True
        if use:
            for k in range(i, i + best_len):
                insert(k)
            i += best_len
        else:
            putbit(1)
            out.append(src[i])
            insert(i)
            i += 1
    # terminator: full match with count byte 0
    putbit(0); putbit(1)
    out += b'\x00\xF0\x00'
    out[desc_at] = desc & 0xFF
    out[desc_at + 1] = desc >> 8
    dec, _ = kosinski_decode(out)
    if dec != src:
        raise AssertionError("kosinski round-trip failed")
    return bytes(out)


def kosm_encode(src):
    """S3K Kosinski-moduled: size word, then $1000-byte modules each padded so
    the next starts on a 16-byte boundary relative to the previous start."""
    src = bytes(src)
    if len(src) > 0x8000 or len(src) & 1:
        raise ValueError("KosM source must be even and <= $8000")
    size = len(src)
    out = bytearray(((0xA000 if size == 0x8000 else size)).to_bytes(2, 'big'))
    for m in range(0, size, 0x1000):
        part = kosinski_encode(src[m:m + 0x1000])
        out += part
        while len(part) & 0xF:
            part += b'\0'
            out += b'\0'
    return bytes(out)
