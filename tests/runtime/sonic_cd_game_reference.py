#!/usr/bin/env python3
"""Headless original-disc QA through a separately supplied libretro core.

This is an independent reference tool, never a game/engine runtime dependency.
Supply an owned BIOS and CUE/BIN. No copyrighted input or state belongs in Git.
Audio callbacks consume emulated samples without opening a speaker device.
Plans declare pad input, RAM prerequisites and captures; they must not inject
the success flags of a behavior being validated. Main RAM exports use explicit
word swapping for the qualified little-endian Genesis Plus GX reference build.
"""
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path
import shutil
from PIL import Image


class GameInfo(C.Structure):
    _fields_ = [('path', C.c_char_p), ('data', C.c_void_p),
                ('size', C.c_size_t), ('meta', C.c_char_p)]


class SystemInfo(C.Structure):
    _fields_ = [('name', C.c_char_p), ('version', C.c_char_p),
                ('extensions', C.c_char_p), ('fullpath', C.c_bool), ('extract', C.c_bool)]


class Variable(C.Structure):
    _fields_ = [('key', C.c_char_p), ('value', C.c_char_p)]


class MemoryDescriptor(C.Structure):
    _fields_ = [('flags', C.c_uint64), ('ptr', C.c_void_p), ('offset', C.c_size_t),
                ('start', C.c_size_t), ('select', C.c_size_t), ('disconnect', C.c_size_t),
                ('length', C.c_size_t), ('address_space', C.c_char_p)]


class MemoryMap(C.Structure):
    _fields_ = [('descriptors', C.POINTER(MemoryDescriptor)), ('count', C.c_uint)]


class OriginalGame:
    def __init__(self, core, cue, bios, directory):
        self.directory = directory.resolve(); self.directory.mkdir(parents=True, exist_ok=True)
        shutil.copy2(bios, self.directory / 'bios_CD_U.bin')
        self.path_buffer = str(self.directory).encode()
        self.library = C.CDLL(str(core)); self.callbacks = []
        self.options = {}; self.frame = 0; self.keys = set(); self.pixels = None
        self.pixel_format = 0; self.audio_frames = 0; self.maps = []
        self.events = []; self.callback_errors = []

        def environment(command, data):
            try:
                if command in (9, 30, 31):
                    C.cast(data, C.POINTER(C.c_char_p))[0] = self.path_buffer
                    return True
                if command == 10:
                    self.pixel_format = C.cast(data, C.POINTER(C.c_uint))[0]
                    return self.pixel_format in (0, 1, 2)
                if command == 3:
                    C.cast(data, C.POINTER(C.c_bool))[0] = True; return True
                if command == 52:
                    C.cast(data, C.POINTER(C.c_uint))[0] = 0; return True
                if command == 16:
                    variables = C.cast(data, C.POINTER(Variable)); index = 0
                    while variables[index].key:
                        key, description = variables[index].key, variables[index].value
                        self.options[key] = description.split(b'; ', 1)[1].split(b'|', 1)[0]
                        index += 1
                    return True
                if command == 15:
                    variable = C.cast(data, C.POINTER(Variable)).contents
                    variable.value = self.options.get(variable.key)
                    return variable.value is not None
                if command == 17:
                    C.cast(data, C.POINTER(C.c_bool))[0] = False; return True
                if command == 65572:
                    memory = C.cast(data, C.POINTER(MemoryMap)).contents
                    self.maps = [dict(name=d.address_space.decode() if d.address_space else '',
                        ptr=d.ptr, offset=d.offset, start=d.start, length=d.length, flags=d.flags)
                        for d in memory.descriptors[:memory.count]]
                    return True
                return False
            except Exception as exc:
                self.callback_errors.append(repr(exc)); return False

        def video(data, width, height, pitch):
            if data:
                self.pixels = (C.string_at(data, pitch * height), width, height, pitch, self.pixel_format)

        def audio(left, right):
            self.audio_frames += 1

        def audio_batch(data, count):
            self.audio_frames += count; return count

        def pad(port, device, index, button):
            return int(port == 0 and button in self.keys)

        specs = (
            ('environment', C.CFUNCTYPE(C.c_bool, C.c_uint, C.c_void_p), environment),
            ('video_refresh', C.CFUNCTYPE(None, C.c_void_p, C.c_uint, C.c_uint, C.c_size_t), video),
            ('audio_sample', C.CFUNCTYPE(None, C.c_int16, C.c_int16), audio),
            ('audio_sample_batch', C.CFUNCTYPE(C.c_size_t, C.POINTER(C.c_int16), C.c_size_t), audio_batch),
            ('input_poll', C.CFUNCTYPE(None), lambda: None),
            ('input_state', C.CFUNCTYPE(C.c_int16, C.c_uint, C.c_uint, C.c_uint, C.c_uint), pad))
        for name, signature, function in specs:
            callback = signature(function); self.callbacks.append(callback)
            setter = getattr(self.library, 'retro_set_' + name)
            setter.argtypes = [signature]; setter(callback)
        self.library.retro_get_system_info.argtypes = [C.POINTER(SystemInfo)]
        info = SystemInfo(); self.library.retro_get_system_info(C.byref(info))
        self.identity = dict(name=info.name.decode(), version=info.version.decode(),
            core_sha256=hashlib.sha256(core.read_bytes()).hexdigest(),
            bios_sha256=hashlib.sha256(bios.read_bytes()).hexdigest(),
            cue_sha256=hashlib.sha256(cue.read_bytes()).hexdigest())
        self.library.retro_init()
        self.library.retro_load_game.argtypes = [C.POINTER(GameInfo)]
        self.library.retro_load_game.restype = C.c_bool
        game = GameInfo(str(cue).encode(), None, 0, None)
        if not self.library.retro_load_game(C.byref(game)):
            raise RuntimeError('Independent core could not load the owned disc')
        self.library.retro_get_memory_data.argtypes = [C.c_uint]
        self.library.retro_get_memory_data.restype = C.c_void_p
        self.library.retro_get_memory_size.argtypes = [C.c_uint]
        self.library.retro_get_memory_size.restype = C.c_size_t
        self.library.retro_serialize_size.restype = C.c_size_t
        self.library.retro_serialize.argtypes = [C.c_void_p, C.c_size_t]
        self.library.retro_serialize.restype = C.c_bool
        self.library.retro_unserialize.argtypes = [C.c_void_p, C.c_size_t]
        self.library.retro_unserialize.restype = C.c_bool

    def run(self, frames, keys=()):
        self.keys = set(keys)
        for _ in range(frames):
            self.library.retro_run(); self.frame += 1
        self.keys.clear()
        if self.callback_errors: raise RuntimeError(self.callback_errors)

    def ram_pointer(self):
        size = self.library.retro_get_memory_size(2)
        pointer = self.library.retro_get_memory_data(2)
        if size != 65536 or not pointer: raise RuntimeError(('Unexpected main RAM export', size))
        return pointer

    def ram(self):
        raw = C.string_at(self.ram_pointer(), 65536)
        guest = bytearray(65536); guest[0::2] = raw[1::2]; guest[1::2] = raw[0::2]
        return bytes(guest)

    def write(self, address, data):
        pointer = self.ram_pointer()
        for i, byte in enumerate(data):
            C.c_uint8.from_address(pointer + ((address + i) & 65535 ^ 1)).value = byte
        self.events.append(dict(frame=self.frame, prerequisite_ram_write=hex(address), data=data.hex()))

    def save(self, path):
        size = self.library.retro_serialize_size(); data = C.create_string_buffer(size)
        if not self.library.retro_serialize(data, size): raise RuntimeError('Reference save-state failed')
        path.write_bytes(data.raw)

    def load(self, path):
        data = path.read_bytes(); buffer = C.create_string_buffer(data)
        if not self.library.retro_unserialize(buffer, len(data)): raise RuntimeError('Reference state restore failed')
        self.events.append(dict(frame=self.frame, reference_state_loaded=str(path)))

    def capture(self, path):
        if self.pixels is None: raise RuntimeError('No original video frame')
        data, width, height, pitch, fmt = self.pixels
        if fmt == 1:
            im = Image.frombytes('RGB', (width, height), data, 'raw', 'BGRX', pitch)
        else:
            import numpy as np
            words = np.frombuffer(data, dtype='<u2').reshape(height, pitch // 2)[:, :width]
            if fmt == 2:
                rgb = np.stack((((words >> 11) & 31) * 255 // 31,
                                ((words >> 5) & 63) * 255 // 63, (words & 31) * 255 // 31), axis=2)
            else:
                rgb = np.stack((((words >> 10) & 31) * 255 // 31,
                                ((words >> 5) & 31) * 255 // 31, (words & 31) * 255 // 31), axis=2)
            im = Image.fromarray(rgb.astype('uint8'), 'RGB')
        im.save(path)
        path.with_suffix('.ram').write_bytes(self.ram())

    def close(self):
        self.library.retro_unload_game(); self.library.retro_deinit()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('core', 'cue', 'bios', 'plan', 'out'):
        p.add_argument('--' + name, required=True, type=lambda s: Path(s).resolve())
    a = p.parse_args(); a.out.mkdir(parents=True, exist_ok=False)
    plan = json.loads(a.plan.read_text()); game = OriginalGame(a.core, a.cue, a.bios, a.out / 'system')
    try:
        print(json.dumps(game.identity), flush=True)
        for step in plan:
            if 'load' in step: game.load(Path(step['load']))
            if 'write' in step:
                for address, data in step['write'].items(): game.write(int(address, 16), bytes.fromhex(data))
            if 'frames' in step: game.run(step['frames'], step.get('keys', ()))
            if 'capture' in step: game.capture(a.out / (step['capture'] + '.png'))
            if 'save' in step: game.save(a.out / (step['save'] + '.state'))
            print(json.dumps(dict(frame=game.frame, step=step)), flush=True)
        (a.out / 'result.json').write_text(json.dumps(dict(identity=game.identity, frames=game.frame,
            emulated_audio_frames=game.audio_frames, callback_errors=game.callback_errors,
            memory_maps=game.maps, prerequisite_events=game.events, plan=plan), indent=2) + '\n')
    finally: game.close()


if __name__ == '__main__': main()
