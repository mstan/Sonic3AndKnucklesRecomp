# Event music

The **Sound** section of Mods has five independent theme selectors shared by
Sonic 3, Sonic & Knuckles and Sonic 3 & Knuckles. All are disabled by default.

| Selector | Replacements |
|---|---|
| 1-UP Theme | Sonic 1 / Sonic 2; Sonic 3 prototype; Sonic 3; Sonic & Knuckles |
| Game Over Theme | Sonic 1 / Sonic 2; Sonic 3 & Knuckles; Sonic CD (US) |
| Continue Screen Theme | Sonic 1 / Sonic 2; Sonic 3; Sonic & Knuckles |
| Act Clear Theme | Sonic 1 / Sonic 2; Sonic 2 Simon Wai prototype; Sonic 3 prototype; Sonic 3 & Knuckles; Sonic CD (US) |
| Invincibility Theme | Sonic 1; Sonic 2 Aug 21 prototype; Sonic 2; Sonic 3; Sonic & Knuckles; Sonic CD (US) |

Choose a theme in its dropdown. If it needs another cartridge, the corresponding
ROM picker appears directly below the choice. Select your raw ROM or ZIP; the
game extracts and verifies it automatically, then enables that theme. A verified
donor is shared with every other matching choice and, in normal installations,
with all three games. A cartridge already present in the running game needs no
additional input. You can select choices while their feature is disabled, and
enable a prepared choice later with its checkbox. **Original** disables that
event's replacement.

These options enable Clone Driver automatically. Restart to apply changes.
They currently support local play in the original campaigns; imported campaigns
and netplay retain their existing driver restrictions. Unselected music and
cartridge sound effects are retained. A 1-UP jingle returns to the preceding
music, and standalone Sonic 3's Super theme is retained when replacing its
Invincibility theme.

## Donors

Use the supported original Sonic 1 JUE revision 0 or Sonic 2 World cartridge,
or an original Sonic 3 / Sonic & Knuckles cartridge for that game's variants.
The **Sonic 1 / Sonic 2** choice accepts either supported cartridge; providing
Sonic 1 also prepares its separate Invincibility choice, and the same applies
to Sonic 2. The existing November 1993 soundtrack restoration and the event
selectors reuse a verified prototype donor where available.

The prototype choices identify these builds:

- [Sonic 2, Simon Wai prototype](https://hiddenpalace.org/Sonic_the_Hedgehog_2_(Simon_Wai_prototype))
- [Sonic 2, August 21, 1992 prototype](https://hiddenpalace.org/Sonic_the_Hedgehog_2_(Aug_21,_1992_prototype))
- [Sonic 3, November 3, 1993 prototype](https://hiddenpalace.org/Sonic_the_Hedgehog_3_(Nov_3,_1993_prototype))

The same documentation links appear on the corresponding mod choices. Renamed
files are accepted by their contents; unsupported or modified ROMs are rejected
without replacing a previous valid donor.

For Sonic CD, provide the supported US disc as a CUE/BIN set, SGCD image or ZIP
containing its audio tracks. One import prepares Game Over, Act Clear and
Invincibility. A data-only ISO cannot provide CD-DA music. The original track
assignments are documented in the
[Sonic CD sound definitions](https://github.com/sonicretro/scddisasm/blob/f8cf3f0af7bf419d18182ba98554f09307ad3cc6/src/_Include/Sound.inc).

No ROM, disc recording, instrument, sequence or sample is included in the game
package. Imports remain in the player's private data library. Native save
files and progress are independent of these sound choices.
