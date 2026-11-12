# Music customization

The **Sound** section of Mods has **Shared Themes**, **Sonic 3 Campaign** and
**Sonic & Knuckles Campaign**. Each cartridge shows the campaigns it contains;
the combined game shows all three sections. All tracks default to **Base game**.

Choose each song independently in its dropdown. A zone or act offers only
**Base game** and **that same zone or act's November 1993 prototype theme**.
Zone music cannot be exchanged with other zones or other Sonic games.
Lava Reef Act 2 and Hidden Palace share one song and one dropdown.
Competition, bonus stages and menus have their own applicable rows. The Competition menu offers explicit Retail and Prototype choices. Credits are in Shared Themes, with Sonic 3 retail, Sonic & Knuckles retail and Sonic 3 prototype choices.

**Shared Themes** contains the event, boss, title and other shared music:

| Selector | Replacements |
|---|---|
| 1-UP | Sonic 1 / Sonic 2; Sonic 3 prototype; Sonic 3; Sonic & Knuckles |
| Game Over | Sonic 1 / Sonic 2; Sonic 3 & Knuckles; Sonic CD (US) |
| Continue Screen | Sonic 1 / Sonic 2; Sonic 3; Sonic & Knuckles |
| Act Clear | Sonic 1 / Sonic 2; Sonic 2 Simon Wai prototype; Sonic 3 prototype; Sonic 3 & Knuckles; Sonic CD (US) |
| Invincibility | Sonic 1; Sonic 2 August 21 prototype; Sonic 2; Sonic 3; Sonic & Knuckles; Sonic CD (US) |
| Drowning | Sonic 1 / Sonic 2; Sonic 3 prototype |
| Main Boss | Sonic 1; Sonic 2; Sonic 2 Simon Wai prototype; Sonic 3; Sonic & Knuckles; Sonic 3 prototype |
| Mini-boss | Sonic 1; Sonic 2; Sonic 2 Simon Wai prototype; Sonic 3; Sonic & Knuckles / Sonic 3 prototype |
| Title Screen | Sonic 3; Sonic & Knuckles; Sonic 3 prototype |
| Special Stage, Knuckles, Emerald | Corresponding Sonic 3 prototype music |
| Super (standalone Sonic 3) | Sonic 3 prototype |
| Credits | Sonic 3 (Retail); Sonic & Knuckles (Retail); Sonic 3 Prototype |
| Final Boss (S&K campaigns) | Sonic 1; Sonic 2; Sonic 2 Simon Wai prototype; Sonic 3; Sonic & Knuckles; Sonic 3 prototype |

Main boss and mini-boss choices are independent. For example, choose the
Simon Wai boss tune for mini-bosses and retail Sonic 2 for main bosses.
The mini-boss control covers each cartridge's applicable mini-boss queues.
Sonic 3 can use the S&K/prototype mini-boss and S&K title music; S&K and the
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
unchanged: a separately enabled whole-prototype soundtrack mod still applies.
Disable that mod when mixing selected prototype tracks with the retail OST.
Cartridge sound effects are retained. Sonic 3's normal Super music remains
separate from Invincibility unless its Super row is explicitly replaced.

## Donors

Use Sonic 1 JUE revision 0, Sonic 2 World revision 1, or a supported original
Sonic 3 / Sonic & Knuckles cartridge for its variants. **Sonic 1 / Sonic 2**
accepts either supported cartridge. Providing Sonic 1 also prepares its
Invincibility and boss choices; Sonic 2 works the same way for its variants.
The shared S&K/prototype mini-boss choice accepts either matching cartridge.

The prototype builds and their source documentation are:

- [Sonic 2, Simon Wai prototype](https://hiddenpalace.org/Sonic_the_Hedgehog_2_(Simon_Wai_prototype))
- [Sonic 2, August 21, 1992 prototype](https://hiddenpalace.org/Sonic_the_Hedgehog_2_(Aug_21,_1992_prototype))
- [Sonic 3, November 3, 1993 prototype](https://hiddenpalace.org/Sonic_the_Hedgehog_3_(Nov_3,_1993_prototype))

Each section links to these donor details. The existing whole-prototype music
and restoration features reuse the same verified prototype donor. Renamed
files are accepted by contents; mismatched or modified inputs do not replace
an existing valid donor.

For Sonic CD, provide the supported US disc as a CUE/BIN set, SGCD or ZIP with
audio tracks. One import prepares Game Over, Act Clear and Invincibility.
Data-only ISOs cannot provide CD-DA music. Track assignments are documented in
the [Sonic CD sound definitions](https://github.com/sonicretro/scddisasm/blob/f8cf3f0af7bf419d18182ba98554f09307ad3cc6/src/_Include/Sound.inc).

No ROM, disc recording, instrument, sequence or sample is distributed in the
packages. Imports remain in the player's private library. Native saves and
progress are independent of these music choices.