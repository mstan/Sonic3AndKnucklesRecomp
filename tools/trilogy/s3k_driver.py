"""Read the retail S3K Z80 sound driver out of the cartridge (as SndDrvInit
uploads it) and walk its SMPS data. Used by the music converter."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from mdcodecs import kosinski_decode  # noqa: E402

Z80_SOUND_DRIVER = 0x0F6960        # Kosinski -> Z80 $0000
Z80_SOUND_DRIVER_DATA = 0x0F7760   # Kosinski -> Z80 $1300 (z80_SoundDriverPointers)
Z80_DATA_BASE = 0x1300
VOLENV_PTRS = 0x1387               # z80_VolEnvPointers
MUSIC_BANKS = 0x0B65               # z80_MusicBanks (bits 15-22 of 68k address)
MUSIC_PTRS = 0x1618                # z80_MusicPointers
SFX_PTRS = 0x167E                  # z80_SFXPointers ($33..)
SND_BANK = 0x0F8000                # SFX bank

# S3K (S&K driver) coordination flag parameter byte counts
S3K_FLAG_LEN = {0xE0: 1, 0xE1: 1, 0xE2: 1, 0xE3: 0, 0xE4: 1, 0xE5: 2, 0xE6: 1, 0xE7: 0,
                0xE8: 1, 0xE9: 0, 0xEA: 1, 0xEB: 3, 0xEC: 1, 0xED: 1, 0xEE: 2, 0xEF: 1,
                0xF0: 4, 0xF1: 2, 0xF2: 0, 0xF3: 1, 0xF4: 1, 0xF5: 1, 0xF6: 2, 0xF7: 4,
                0xF8: 2, 0xF9: 0, 0xFA: 0, 0xFB: 1, 0xFC: 2, 0xFD: 1, 0xFE: 4}
S3K_META_LEN = {0: 1, 1: 1, 2: 1, 3: 3, 4: 1, 5: 4, 6: 2, 7: 0}


def z80_image(rom):
    ram = bytearray(0x2000)
    a, _ = kosinski_decode(rom, Z80_SOUND_DRIVER)
    ram[:len(a)] = a
    b, _ = kosinski_decode(rom, Z80_SOUND_DRIVER_DATA)
    ram[Z80_DATA_BASE:Z80_DATA_BASE + len(b)] = b
    return ram


def le16(b, a):
    return b[a] | (b[a + 1] << 8)


def volenv_count(ram):
    first = min(le16(ram, VOLENV_PTRS + i * 2) for i in range(8))
    return (first - VOLENV_PTRS) // 2


def walk_track(rom, bank, ptr, on_flag, seen=None):
    """Follow one S3K track from Z80 pointer ptr in 68k bank; call
    on_flag(op, params) for each coordination flag."""
    seen = seen if seen is not None else set()
    todo = [ptr]
    while todo:
        p = todo.pop()
        while True:
            if p in seen or not (0x8000 <= p <= 0xFFFF):
                break
            seen.add(p)
            b = rom[bank + (p - 0x8000)]
            if b < 0xE0:
                p += 1
                continue
            if b == 0xFF:
                sub = rom[bank + (p + 1 - 0x8000)]
                n = 2 + S3K_META_LEN.get(sub, 0)
            else:
                n = 1 + S3K_FLAG_LEN[b]
            params = bytes(rom[bank + (p + 1 - 0x8000):bank + (p + n - 0x8000)])
            on_flag(b, params)
            if b in (0xF6, 0xFC):
                todo.append(params[0] | (params[1] << 8))
                break
            if b in (0xF2, 0xE3, 0xF9):
                break
            if b == 0xF8:
                todo.append(params[0] | (params[1] << 8))
            if b == 0xF7:
                todo.append(params[2] | (params[3] << 8))
            if b == 0xEB:
                todo.append(params[1] | (params[2] << 8))
            p += n


if __name__ == '__main__':
    rom = Path(sys.argv[1]).read_bytes()
    ram = z80_image(rom)
    print('volenv count', volenv_count(ram))
    used = set()
    for sid in range(0x33, 0xDC):
        ptr = le16(ram, SFX_PTRS + (sid - 0x33) * 2)
        hdr = SND_BANK + (ptr - 0x8000)
        nch = rom[hdr + 3]
        for c in range(nch):
            ch = hdr + 4 + c * 6
            tptr = rom[ch + 2] | (rom[ch + 3] << 8)
            if rom[ch + 1] & 0x80:      # PSG channel
                used.add(('hdr', 0))
            walk_track(rom, SND_BANK, tptr,
                       lambda op, prm: used.add(prm[0]) if op == 0xF5 else None)
    print('PSG envelopes used by SFX:', sorted(x for x in used if isinstance(x, int)))
