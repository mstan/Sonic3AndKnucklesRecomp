"""Build a pinned software-only S3-family Clone Driver; never fetch game assets."""
from pathlib import Path
import argparse, hashlib, json, platform, re, stat, struct, subprocess, urllib.parse, urllib.request

LOAD_ADDRESS = 0x400000
SOURCE_COMMIT = '13ca55d1f9bf2268f440616404cc65a3b08a6ad6'
FIELDS = ('payload_size initialise update queue_music queue_sfx queue_extra '
          'guarded_update pcm_idle ram_address ram_size queue_offset pause_offset '
          'tempo_offset sample_table music_index sound_index psg_index modenv_index '
          'voice_bank stop_all dac_map_s3 dac_map_sk sega_id sample_count '
          'megapcm_load_driver megapcm_load_table').split()
HEADER = struct.Struct('>4s' + 'I' * len(FIELDS))

def digest(data):
    return hashlib.sha256(data).hexdigest()

def fetch(cache, name, spec, repository, commit, offline):
    p = cache / name
    if p.is_file() and p.stat().st_size == spec['size'] and digest(p.read_bytes()) == spec['sha256']:
        return p
    if offline:
        raise RuntimeError('Missing or damaged pinned input: ' + name)
    url = 'https://raw.githubusercontent.com/' + repository + '/' + commit + '/' + urllib.parse.quote(name, safe='/')
    with urllib.request.urlopen(url, timeout=60) as stream:
        data = stream.read(spec['size'] + 1)
    if len(data) != spec['size'] or digest(data) != spec['sha256']:
        raise RuntimeError('Pinned input identity mismatch: ' + name)
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_bytes(data)
    return p

def write(p, text):
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(text, encoding='utf-8')

def assemble(source, assembler, p2bin, out):
    out.mkdir(parents=True, exist_ok=False)
    inputs = out / 'inputs'
    relocation_count = 0
    for name in source:
        text = source[name].read_text(encoding='utf-8')
        # The original port assumes a 16-bit absolute work-RAM address. This
        # isolated cartridge RAM uses long addresses; instruction sizes remain
        # unchanged except for the deliberately relocated address operands.
        rows = []
        for line in text.splitlines():
            if 'Clone_Driver_RAM' in line or 'Graphics_flags' in line:
                line, n = re.subn(r'\)\.w\b', ').l', line)
                relocation_count += n
            rows.append(line)
        text = '\n'.join(rows) + '\n'
        if name == 'Sound/Engine/Sonic 2 Clone Driver v2.asm':
            # A new song resets its DAC track pan to stereo. MegaPCM keeps a
            # separate pan shadow for sample/SFX restoration; reset that too,
            # or a song without a pan command inherits the preceding song's
            # left/right-only DAC setting.
            marker = '.bgm_loadMusic:\n'
            assert text.count(marker) == 1, 'Upstream music loader changed'
            text = text.replace(marker, marker + '\tMPCM_setPan #$C0\n', 1)
            # The logo sample is not a music track: it bypasses the song
            # loader that restores PCM volume after a demo fade. Restore its
            # own full-volume stereo output before requesting the sample.
            marker = 'PlaySegaSound:\n'
            assert text.count(marker) == 1, 'Upstream logo playback changed'
            text = text.replace(marker, marker + '\tMPCM_setVol #0\n\tMPCM_setPan #$C0\n', 1)
            # Retail SMPS Z80 keeps the full unsigned transposed note. Low
            # bass notes deliberately wrap into octaves beyond seven. Clone's
            # 7-bit mask instead turns them into high audible notes, and its
            # 96-entry table also permits reads into following instructions.
            # Encode the retail first-octave frequencies and octave OR for all
            # 256 byte indices (Clone indexes C as 1 rather than 0).
            start = text.index('FMSetFreq:\n')
            end = text.index('; End of function FMSetFreq', start)
            segment = text[start:end]
            assert segment.count('andi.w\t#$7F,d5') == 1, 'Upstream FM note lookup changed'
            text = text[:start] + segment.replace('andi.w\t#$7F,d5', 'andi.w\t#$FF,d5') + text[end:]
            octave = (0x284, 0x2AB, 0x2D3, 0x2FE, 0x32D, 0x35C,
                      0x38F, 0x3C5, 0x3FF, 0x43C, 0x47C, 0x4C0)
            frequencies = [octave[((i-1)&255)%12] | ((((i-1)&255)//12)<<11) for i in range(256)]
            start = text.index('FMFrequencies:\n') + len('FMFrequencies:\n')
            end = start
            while text[end:].startswith('\tdc.w '):
                end = text.index('\n', end) + 1
            assert end > start, 'Upstream FM frequency table changed'
            text = text[:start] + ''.join('\tdc.w ' + ','.join(f'${n:04X}' for n in frequencies[i:i+12]) + '\n' for i in range(0,256,12)) + text[end:]
            # Two channel maps stored sign-extended 16-bit work-RAM pointers.
            # Widen their entries, then scale the original byte index using
            # LEA rather than arithmetic that would change condition flags.
            text, tables = re.subn(
                r'(SFX_(?:BGM|SFX)ChannelRAM:\n)((?:\s*dc\.w[^\n]*\n)+)',
                lambda m: m[1] + m[2].replace('dc.w', 'dc.l'), text)
            assert tables == 2, 'Upstream channel map layout changed'
            text, loads = re.subn(
                r'^(\s*)movea\.w\s+\((a[0-6]),(d[0-7]\.w)\),(a[0-6])([^\n]*)$',
                lambda m: f'{m[1]}lea ({m[2]},{m[3]}),{m[4]}\n\tmovea.l ({m[4]},{m[3]}),{m[4]}{m[5]}',
                text, flags=re.M)
            assert loads == 8, 'Upstream channel map consumers changed'
            # These eight musical modulation envelopes are game data. Keep
            # only the pointer layout; derive their bytes from the player's
            # ROM alongside PSG envelopes, instruments and sequences.
            start = text.index('; These have been converted to SMPS 68k format:')
            end = text.index('    endif', start)
            text = text[:start] + ''.join(f'ModEnv_{i:02X}: dc.b [128]0\n' for i in range(8)) + text[end:]
        write(inputs / name, text)
    write(inputs / 'Sound/Music.asm', 'MusicIndex:\n' + ''.join(f'ptr_mus{i:02X}: dc.l 0\n' for i in range(1, 0x34)) + 'ptr_musend:\n')
    sound_ids = source['Sound/Sound IDs.asm'].read_text()
    last_sfx = max(int(n, 16) for n in re.findall(r'ptr_snd([0-9A-F]{2})\b', sound_ids))
    write(inputs / 'Sound/SFX.asm', 'SoundIndex:\n' + ''.join(f'ptr_snd{i:02X}: dc.l 0\n' for i in range(0x40, last_sfx + 1)) + 'ptr_sndend:\n')
    write(inputs / 'Sound/Special SFX.asm', 'SpecSoundIndex: dc.l 0\n')
    write(inputs / 'Sound/FM Universal Voice Bank.asm', '    dc.b [800]0\n' * 4)
    env_equates = source['Sound/SMPS2ASM - PSG Volume Envelope Equates.asm'].read_text()
    envs = list(dict.fromkeys(re.findall(r'SMPS_id\((ptr_(?:s3|sk)psg\w+)\)', env_equates)))
    # Runtime conversion preserves retail PSG envelope IDs. Equate labels are
    # aliases of those slots; their upstream include order is not an asset ID.
    psg = 'PSG_Index:\n'
    for number in range(1, 52):
        psg += ''.join(name + ':\n' for name in envs if int(name[-2:], 16) == number)
        psg += '    dc.l 0\n'
    write(inputs / 'Sound/PSG Volume Envelopes.asm', psg)
    descriptors = source['Sound/MegaPCM - DAC Table.asm'].read_text()
    samples = sorted(set(re.findall(r'dcSample\s+\w+\s*,\s*(\w+)', descriptors)))
    write(inputs / 'Sound/SamplePlaceholders.asm', ''.join(f'{name} = $600000\n{name}_End = {name}+16\n' for name in samples))
    write(inputs / 'NativeDACMaps.asm', Path(__file__).with_name('native_dac_order.asm').read_text())
    wrapper = Path(__file__).with_name('driver.asm')
    for command, log in [([str(assembler), '-xx', '-n', '-q', '-A', '-L', '-U', '-i', str(inputs), '-o', str(out/'driver.p'), '-olist', str(out/'driver.lst'), '-E', str(out/'driver-errors.log'), str(wrapper)], 'assembler.log'), ([str(p2bin), '-p=FF', str(out/'driver.p'), str(out/'image.bin')], 'p2bin.log')]:
        with (out/log).open('w') as stream:
            subprocess.run(command, cwd=out, stdout=stream, stderr=subprocess.STDOUT, check=True, timeout=120)
    raw = (out/'image.bin').read_bytes()
    assert LOAD_ADDRESS + HEADER.size <= len(raw) < LOAD_ADDRESS + 0x20000
    assert raw[:LOAD_ADDRESS] == b'\xff' * LOAD_ADDRESS
    payload = raw[LOAD_ADDRESS:]
    magic, *values = HEADER.unpack_from(payload)
    info = dict(zip(FIELDS, values))
    assert magic == b'S3CR' and info['payload_size'] == len(payload)
    assert info['ram_address'] == 0x900000 and 0 < info['ram_size'] <= 0x800
    for key in ('initialise', 'update', 'queue_music', 'queue_sfx', 'queue_extra', 'guarded_update', 'pcm_idle', 'stop_all'):
        assert LOAD_ADDRESS + HEADER.size <= info[key] < len(raw) and not info[key] & 1, key
    (out/'clone-driver.bin').write_bytes(payload)
    manifest = {'source': SOURCE_COMMIT, 'asset_mode': 'rom', 'load_address': LOAD_ADDRESS, 'sha256': digest(payload), 'relocated_absolute_operands': relocation_count, **info}
    write(out/'program.json', json.dumps(manifest, indent=2) + '\n')
    return manifest

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cache', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--assembler', type=Path)
    ap.add_argument('--p2bin', type=Path)
    ap.add_argument('--offline', action='store_true')
    args = ap.parse_args()
    lock = json.loads(Path(__file__).with_name('sources.lock.json').read_text())
    assert lock['commit'] == SOURCE_COMMIT
    cache = args.cache.resolve()
    source = {name: fetch(cache/'source', name, spec, lock['repository'], lock['commit'], args.offline) for name, spec in lock['sources'].items()}
    if bool(args.assembler) != bool(args.p2bin):
        ap.error('Supply --assembler and --p2bin together')
    if args.assembler:
        assembler, p2bin = args.assembler.resolve(strict=True), args.p2bin.resolve(strict=True)
    else:
        host = platform.system() + '-' + platform.machine().lower()
        host = {'Windows-amd64': 'Windows-x86_64', 'Linux-x86_64': 'Linux-x86_64'}.get(host)
        if not host:
            ap.error('Supply native AS/p2bin on this build host')
        names = ('asw.exe', 'p2bin.exe') if host.startswith('Windows') else ('asl', 'p2bin')
        tools = [fetch(cache/'tools', 'build_tools/'+host+'/'+name, lock['tools']['files']['build_tools/'+host+'/'+name], lock['tools']['repository'], lock['tools']['commit'], args.offline) for name in names]
        for tool in tools:
            if not host.startswith('Windows'):
                tool.chmod(tool.stat().st_mode | stat.S_IXUSR)
        assembler, p2bin = tools
    print(json.dumps(assemble(source, assembler, p2bin, args.output.resolve()), indent=2))

if __name__ == '__main__':
    main()
