"""Private-ROM visual reference through an independently supplied libretro core.

No audio output, window, ROM assets or emulator binary is shipped by this tool.
The owner supplies the ROM and core; captures and optional RAM cheats are local.
"""
import ctypes as C
from pathlib import Path
from PIL import Image


class GameInfo(C.Structure):
    _fields_ = [('path', C.c_char_p), ('data', C.c_void_p),
                ('size', C.c_size_t), ('meta', C.c_char_p)]


class Variable(C.Structure):
    _fields_ = [('key', C.c_char_p), ('value', C.c_char_p)]


class Reference:
    def __init__(self, core, rom):
        self.core = C.CDLL(str(Path(core).resolve()))
        self.options = {}
        self.directory = str(Path(core).resolve().parent).encode()
        self.format = 0
        self.buttons = 0
        self.image = None
        self.frame = 0
        def environment(cmd, data):
            cmd &= 0xffff
            if cmd in (9, 30, 31):
                C.cast(data, C.POINTER(C.c_char_p))[0] = self.directory
                return True
            if cmd == 3:
                C.cast(data, C.POINTER(C.c_bool))[0] = True
                return True
            if cmd == 10:
                self.format = C.cast(data, C.POINTER(C.c_uint))[0]
                return self.format in (0, 1, 2)
            if cmd == 16:
                variables = C.cast(data, C.POINTER(Variable))
                i = 0
                while variables[i].key:
                    self.options[variables[i].key] = variables[i].value.split(b'; ', 1)[-1].split(b'|')[0]
                    i += 1
                return True
            if cmd == 15:
                var = C.cast(data, C.POINTER(Variable))
                var[0].value = self.options.get(var[0].key)
                return var[0].value is not None
            if cmd == 17:
                C.cast(data, C.POINTER(C.c_bool))[0] = False
                return True
            return cmd in (11, 18, 35)
        def video(data, width, height, pitch):
            if not data: return
            raw = C.string_at(data, pitch * height)
            if self.format == 1:
                self.image = Image.frombytes('RGB', (width, height), raw, 'raw', 'BGRX', pitch)
            else:
                # Both 15-bit and 565 frames are little endian on this host.
                import numpy as np
                pixels = np.frombuffer(raw, dtype='<u2').reshape(height, pitch // 2)[:, :width]
                green = 6 if self.format == 2 else 5
                rgb = np.empty((height, width, 3), dtype=np.uint8)
                rgb[:, :, 0] = ((pixels >> (green + 5)) & 31) * 255 // 31
                rgb[:, :, 1] = ((pixels >> 5) & ((1 << green) - 1)) * 255 // ((1 << green) - 1)
                rgb[:, :, 2] = (pixels & 31) * 255 // 31
                self.image = Image.fromarray(rgb)
        def input_state(port, device, index, button):
            return int(port == 0 and bool(self.buttons & (1 << button)))
        self.callbacks = [C.CFUNCTYPE(C.c_bool, C.c_uint, C.c_void_p)(environment),
            C.CFUNCTYPE(None, C.c_void_p, C.c_uint, C.c_uint, C.c_size_t)(video),
            C.CFUNCTYPE(None, C.c_int16, C.c_int16)(lambda l, r: None),
            C.CFUNCTYPE(C.c_size_t, C.c_void_p, C.c_size_t)(lambda data, n: n),
            C.CFUNCTYPE(None)(lambda: None),
            C.CFUNCTYPE(C.c_int16, C.c_uint, C.c_uint, C.c_uint, C.c_uint)(input_state)]
        names = ['environment', 'video_refresh', 'audio_sample', 'audio_sample_batch', 'input_poll', 'input_state']
        for name, callback in zip(names, self.callbacks):
            getattr(self.core, 'retro_set_' + name)(callback)
        self.core.retro_init()
        self.rom = C.create_string_buffer(rom)
        # Recent cores request full paths and intentionally ignore info.data.
        cartridge = Path(core).resolve().parent / 'reference-cartridge.bin'
        cartridge.write_bytes(rom)
        self.rom_path = str(cartridge).encode()
        info = GameInfo(self.rom_path, C.cast(self.rom, C.c_void_p), len(rom), None)
        self.core.retro_load_game.argtypes = [C.POINTER(GameInfo)]
        self.core.retro_load_game.restype = C.c_bool
        if not self.core.retro_load_game(C.byref(info)):
            raise RuntimeError('Reference core rejected cartridge')
        self.core.retro_get_memory_data.argtypes = [C.c_uint]
        self.core.retro_get_memory_data.restype = C.c_void_p
        self.core.retro_get_memory_size.argtypes = [C.c_uint]
        self.core.retro_get_memory_size.restype = C.c_size_t
        size = self.core.retro_get_memory_size(2)
        if size != 65536: raise RuntimeError(f'Unexpected reference RAM size: {size}')
        self.ram = (C.c_ubyte * size).from_address(self.core.retro_get_memory_data(2))

    def step(self, frames, buttons=0):
        self.buttons = buttons
        for _ in range(frames):
            self.core.retro_run()
            self.frame += 1

    def capture(self, path):
        self.image.save(path)

    def read_ram(self):
        """Canonical 68000 byte order (Genesis Plus GX uses host words)."""
        raw = bytes(self.ram)
        result = bytearray(65536)
        result[0::2], result[1::2] = raw[1::2], raw[0::2]
        return bytes(result)

    def write_ram(self, address, data):
        for offset, value in enumerate(data):
            self.ram[(address + offset) ^ 1] = value

    def cheat(self, index, code):
        """Apply an explicitly recorded core cheat; never alter the ROM file."""
        self.core.retro_cheat_set.argtypes = [C.c_uint, C.c_bool, C.c_char_p]
        self.core.retro_cheat_set(index, True, code.encode())

    def serialize(self):
        self.core.retro_serialize_size.restype = C.c_size_t
        size = self.core.retro_serialize_size()
        data = C.create_string_buffer(size)
        self.core.retro_serialize.argtypes = [C.c_void_p, C.c_size_t]
        self.core.retro_serialize.restype = C.c_bool
        if not self.core.retro_serialize(data, size):
            raise RuntimeError('Reference core could not serialize')
        return bytes(data)

    def close(self):
        self.core.retro_unload_game()
        self.core.retro_deinit()
