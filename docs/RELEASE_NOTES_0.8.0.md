# 0.8.0

- Adds optional Clone Driver + Mega PCM audio to Sonic 3, Sonic 3 & Knuckles
  and Sonic & Knuckles. Music, sound effects, instruments and samples are
  prepared locally from the supported player ROM.
- Keeps the original sound driver as the default. Clone Driver supports local
  play in original campaigns with the standard Z80 backend. Imported campaigns
  and netplay continue to use the original driver.
- Includes driver sound RAM and pending queues in current-build save/load and
  rewind. Native battery saves and existing settings are preserved.
- Works with the existing High refresh rate interpolation at 120 Hz, 144 Hz
  or display refresh rate, while retaining the original game and audio speed.

Enable the sound driver in Mods and restart the application. See
[Sound driver mods](SOUND_DRIVER_MODS.md) for details and build options.
