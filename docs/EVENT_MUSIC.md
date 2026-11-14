# Music customization

The **Sound** section of Mods has **Shared Themes** and one campaign section
for the running game: **Sonic 3**, **Sonic & Knuckles**, or **Sonic 3 & Knuckles**.
Common sounds, including Data Select, are available in all three games.
All tracks default to **Base game**.

Choose each song independently in its dropdown. A zone or act offers only
**Base game** and **that same zone or act's November 1993 prototype theme**.
Sky Sanctuary also offers Sonic 3's unused version of that same song.
Other zone music cannot be exchanged with other zones or other Sonic games.
Lava Reef Act 2 and Hidden Palace share one song and one dropdown.
Competition, bonus stages and menus have their own applicable rows. The Competition menu offers explicit Retail and Prototype choices. Credits are in Shared Themes, with Sonic 3 retail, Sonic & Knuckles retail and Sonic 3 prototype choices.

**Shared Themes** contains the event, boss, title and other shared music:

| Selector | Replacements |
|---|---|
| 1-UP | Sonic 1 / Sonic 2; Sonic 3 prototype; Sonic 3; Sonic & Knuckles |
| Game Over | Sonic 1 / Sonic 2; Sonic 3 & Knuckles; Sonic CD (US) |
| Continue Screen | Sonic 1 / Sonic 2; Sonic 3; Sonic & Knuckles |
| Act Clear | Sonic 1 / Sonic 2; Sonic 2 Simon Wai prototype; Sonic 3 prototype; Sonic 3 & Knuckles; Sonic CD (US) |
| Invincibility | Sonic 1; Sonic 2 August 21 prototype; Sonic 2; Sonic 3; Sonic & Knuckles; Sonic CD (US); unused November 1993 prototype theme |
| Drowning | Sonic 1 / Sonic 2; Sonic 3 prototype |
| Main Boss | Sonic 1; Sonic 2; Sonic 2 Simon Wai prototype; Sonic 3 / Sonic & Knuckles; Sonic 3 prototype; Sonic CD (US) |
| Mini-boss | Sonic 1; Sonic 2; Sonic 2 Simon Wai prototype; Sonic 3; Sonic & Knuckles; Sonic 3 prototype; Sonic CD (US) |
| Title Screen | Sonic 3; Sonic & Knuckles; Sonic 3 prototype |
| Special Stage | Sonic 3 prototype; Sonic 2; Sonic 2 Simon Wai prototype |
| Knuckles | Sonic 3; Sonic & Knuckles; Sonic 3 prototype |
| Data Select | Sonic 3 prototype; Sonic 3; Sonic & Knuckles; Sonic 2 Options; Sonic 2 Simon Wai Level Select |
| Credits | Sonic 3 (Retail); Sonic & Knuckles (Retail); Sonic 3 Prototype |
| Final Boss | Sonic 1; Sonic 2; Sonic 2 Simon Wai prototype; Sonic 3; Sonic & Knuckles; Sonic 3 prototype; Sonic CD (US) |

The table includes stored retail overrides. A retail choice identical to
**Base game** is omitted from the dropdown unless it is already selected or
is needed to override the whole-prototype soundtrack. Existing selections
remain readable and retain their effect. Moving Data Select to Shared also
preserves its previous section's enabled state until Shared is changed.

Main boss and mini-boss choices are independent. For example, choose the
Simon Wai boss tune for mini-bosses and retail Sonic 2 for main bosses.
The mini-boss control covers each cartridge's applicable mini-boss queues.
Sonic 3 can use either the S&K or prototype mini-boss and S&K title music; S&K and the
combined game can use Sonic 3's mini-boss and title music.

Only donor ROMs or discs required by the selected replacements appear below
the dropdowns. Each donor appears once per section and is shared with other
sections and normal installations of all three games. A cartridge already
present in the running game needs no additional input. Select a raw ROM or
ZIP; extraction and identity verification run automatically. Importing a valid
ROM enables that section once every selected replacement has its needed donor.
A selection can be prepared before its ROM is supplied, but the section cannot
be enabled until all its requirements are verified.

Disable a section to restore its normal music while retaining every selected
replacement. Choose **Base game** for a single row to stop replacing that song.
Existing event choices are retained when updating from the earlier controls.

Replacements enable Clone Driver automatically. Restart to apply changes.
They support local play in original campaigns; imported campaigns and netplay
retain their driver restrictions. Base game leaves the current soundtrack
unchanged. A previously saved whole-prototype soundtrack remains in effect;
its Saved default soundtrack option under Clone Driver can restore Base game without
clearing individual choices. New selections use the individual track rows.
Restore Base game there when mixing individual prototype tracks with the
retail OST. Cartridge sound effects are retained. The unused prototype tune
is an Invincibility choice in all three games. Previously saved standalone
Sonic 3 Super replacements retain their separate row until cleared.

## Music player

With Clone Driver enabled, press **A+C** together at the title screen or level
select to open the music player. It plays this game's tracks with the selected
soundtrack replacements and shows the current source beside the track list.
Use the game buttons configured in Input settings:

- **Up/Down:** choose a track.
- **Left/Right:** choose all channels or an individual DAC, FM or PSG channel.
- **B:** play from the beginning.
- **C:** pause or resume.
- **A:** stop.
- **Start:** return to the game menu.

Changing the track restarts a playing song. Channel selection mutes only the
output; all music sequences and synthesis clocks continue at the same position.
Channel audition uses the standard chip synthesis output. Empty channel entries
are skipped. Recorded CD audio plays its full stereo mix. Current-build states
retain the player selection and playback, including a paused or isolated track.

## Custom music files

Each available music row also offers **Custom music**. Select it, then import
the file in the matching resource below the dropdown. Selections are saved
separately for each game. Restart to apply them. Native sound effects remain
available with custom music.

- **SMPS BIN:** a self-contained Sonic 3 Z80 music bank, based at `$8000`,
  with embedded instruments and at most 32 KiB. Samples and envelopes use
  the running cartridge's repertoire.
- **SMPS ASM:** the supported Sonic 3 SMPS2ASM data directives with
  `smpsHeaderStartSong 3`. Labels, notes, instruments, common coordination
  commands, loops and calls are assembled locally. External files, arbitrary
  assembly instructions and unsupported driver extensions are rejected.
- **VGM/VGZ:** one NTSC Genesis YM2612 and PSG, with ordinary register writes,
  waits, loops, uncompressed DAC banks and finite DAC streams. Other chips,
  compressed sample banks, timer-driven CSM and unsupported stream modes are rejected.
- **WAV:** uncompressed mono or stereo PCM, 8–192 kHz and 8–32 bit, normalized
  to stereo 44.1 kHz. Recordings are limited to about three minutes. Stage
  recordings loop from their beginning; short event recordings play once.

The 1-UP row accepts synthesized SMPS jingles that restore the previous music.
WAV recordings play their full stereo mix. SMPS and VGM support channel audition
in the music player. Import errors retain the previous valid selection.

## Donors

Use Sonic 1 JUE revision 0, Sonic 2 World revision 1, or a supported original
Sonic 3 / Sonic & Knuckles cartridge for its variants. **Sonic 1 / Sonic 2**
accepts either supported cartridge. Providing Sonic 1 also prepares its
Invincibility and boss choices; Sonic 2 works the same way for its variants.
S&K and prototype mini-boss choices retain their distinct volume balances. The shared retail main-boss choice uses the running cartridge without another donor.

The prototype builds and their source documentation are:

- [Sonic 2, Simon Wai prototype](https://hiddenpalace.org/Sonic_the_Hedgehog_2_(Simon_Wai_prototype))
- [Sonic 2, August 21, 1992 prototype](https://hiddenpalace.org/Sonic_the_Hedgehog_2_(Aug_21,_1992_prototype))
- [Sonic 3, November 3, 1993 prototype](https://hiddenpalace.org/Sonic_the_Hedgehog_3_(Nov_3,_1993_prototype))

Each section links to these donor details. The existing whole-prototype music
and restoration features reuse the same verified prototype donor. Renamed
files are accepted by contents; mismatched or modified inputs do not replace
an existing valid donor.

For Sonic CD, provide the supported US disc as a CUE/BIN set, SGCD or ZIP with
audio tracks. One import prepares Game Over, Act Clear, Invincibility, Main
Boss, Mini-boss and Final Boss. Older libraries retain their event recordings;
import the disc again to add the boss tracks. Main Boss and Mini-boss use the
disc's boss recording, and Final Boss uses its final-boss recording.
Data-only ISOs cannot provide CD-DA music. Track assignments are documented in
the [Sonic CD sound definitions](https://github.com/sonicretro/scddisasm/blob/f8cf3f0af7bf419d18182ba98554f09307ad3cc6/src/_Include/Sound.inc).

No ROM, disc recording, instrument, sequence or sample is distributed in the
packages. Imports remain in the player's private library. Native saves and
progress are independent of these music choices.
