# Hook candidates

pc1 confirmed the addresses below on a research build of the US 1.0 recomp. "Confirmed" means the harness log matched the field. "TODO" means it is still unknown and must not be guessed. Wired behavior is called out in the last column.

Sources:

- GameShark list, RPGClassics shrine: https://shrines.rpgclassics.com/n64/megaman64/cheats.shtml
- Same codes, Almar's guides: https://almarsguides.com/retro/walkthroughs/N64/Games/MegaMan64/Gameshark/
- Project64 cheat file repeating those addresses: `Mega Man 64 (U).cht` in public cheat packs
- Control text transcribed in the GameFAQs script FAQ: https://gamefaqs.gamespot.com/n64/913960-mega-man-64/faqs/19143
- DashGL's BIN container notes are for Legends 2 stages, not this ROM's item tables: https://docs.dashgl.com/format/psx/megaman-legends-2/bin-archive
- Mega Man Legends 1 stage tiles (MDT / IDX / STG) are geometry, not chest contents: https://mmls.proboards.com/thread/6198/psx-megaman-legends-research-thread
- Speedrun route notes do not publish room IDs: https://www.speedrun.com/mml1/guides/l9s3m
- RT64 names a dumped texture `<16 hex digits>.v<version>.tmem` in `lib/rt64/src/hle/rt64_rdp_tmem.cpp`. The replacement file is those 16 digits plus `.png`.

A PC port of Legends would still help. Shop prices, upgrade costs, and weapon stats are recognizable integer runs. Convert a table to big-endian and search the N64 ROM after any decompression. Keep the extracted bytes out of this repo. This pass did not have that port, and a scan of the first 4 MB did not find a clean price table, so item and chest records stay unidentified.

## Candidates

| Hook | Address or function | Status | What pc1 showed | Wired? |
| --- | --- | --- | --- | --- |
| Player object | `0x802049B0` | Confirmed | Stable across a new game. | Already used by the damage and HP hooks. |
| Player HP | `0x80204A1E` (`player+0x6E`) u16 | Confirmed | 80 at new game. A 100% Continue had 160. | Yes. Hurt scaling already reads this field. |
| Zenny | `0x801BC406` | Confirmed | Matches the GameShark halfword. A 100% Continue had 10240. | No feature needs a write. Left as a known field. |
| Play time | `0x801BC3F6` tick counter | Confirmed | Counts while the game runs. `0x801BC3F4` is the neighboring half. | Not wired. |
| Buster part 0 | `0x80204E00` | Confirmed | The part byte, not the special weapon. | Not written. The wheel still must not poke it. |
| Owned special weapons | `0x80205640` | Confirmed | `0x8000` is the buster only. `poke` of `0xFFFF` persists. Roll still equips. A 100% Continue had `0xBFFC`. | Not written. Ownership is not the equipped weapon. |
| Equipped special weapon | `0x80204B20` u8, mirror `0x80204DFF` | Confirmed | On a 100% Continue this was 2, Machine Buster. Roll's Remove sets 0 and Equip sets 2. Poking 0, 2, 4, or 5 changes the HUD gauge and the arm model. `0x80204B21`, `0x80204B23`, and `0x80204B24` are not this byte. Other indexes were not named. | `[` and `]` write the next of 0, 2, 4, 5 only. 0 is shown as None and 2 as Machine Buster. 4 and 5 stay numbers. The mirror is not written. |
| Ammo | `0x80204EBA`, stride 8 | Confirmed | The published ammo halfwords. | Not wired. No remaster behavior is "show this ammo" yet. |
| Dialogue pointer | `0x801D8884` is redrawn from the segment start each tick. The revealed end is `0x801D88DC` (`+0x6C`). | Confirmed | One glyph per 2 frames. Step sizes on `+0x14` were 1, 2, 3, and 5. The start delay counts in `func_80061838`. The 29-count and the following 9-count are opcode handlers. | `func_80061838` decrements `+0x4` at `0x800619D0` and, when it expires on a byte below `0x84`, adds 1 to `+0x6C` at `0x80061A24`. The patch subtracts 3 from that countdown and then steps over up to two more bytes below `0x84`. Roll's pauses are opcode `0x9F` (`func_8006323C`, fixed 10, decrement `0x80063304`) and `0xA4` (`func_8006331C`, 16-bit duration, store `0x800633AC`). The same subtract-3 is on those two and on the other pure waits `func_80061F14`, `func_80062088`, and `func_8006219C`. Opcodes, including the newline handler, still run. |
| Dialogue start delay | `0x801D8874` u16, counts 5..1 | Confirmed | There is no separate text-speed variable. | Left at 5. It is the pause before the line, not the per-character rate. |
| Texture hashes | `<16 hex>.v5.tmem` from `MM64R_DUMP_TEXTURES` | Confirmed | 1327 unique textures on an early-game walk. A full pack replaced the corridor. A one- or three-hash pack was inconsistent. | Yes. The generated `rt64.json` uses `preload`, not `stream`. Stream applies after the first upload, and a second load on the next frame was clearing that request, so a texture uploaded once kept the native image. Preload finishes the file before the display list continues. An index this tree already wrote with `stream` is rewritten. A database with `operationFilters` is left alone. |
| Room or area id | TODO | Unknown | | Not wired. |
| Player position | Probable | Not confirmed | s32 16.16 fixed X/Y/Z at `0x801DDEE0` / `0x801DDEE4` / `0x801DDEE8`. Mirrors `0x80135E08` and integer `0x801AD878`. | Not wired. |
| Chest and door flags | TODO | Unknown | | Not wired. |
| Item and chest tables | TODO | Unknown | | Not wired. |
| Lock-on movement | TODO | Unknown | | Not wired. |
| Save states | Not an address | Architecture | RDRAM alone is not a save state. | Not wired. |
| Lights, outlines, CRT, shadows | Not in this RT64 pin | Renderer | | Not wired. |

## ROM scan notes

The main code segment forms `0x802049B0` in `func_80069D44` and in many other functions. It does not form `0x80205640` as an immediate. Pointer tables exist (the longest run in the first 2 MB is 334 words at ROM `0x8EDE0`, first word `0x800B3F18`). Those are code or data pointers, not a price list, and the words are not copied into this repo.

`func_80069860` writes 80 to both `player+0x6E` and `player+0xB0` on one spawn path. Treat 80 as the expected fresh HP, and treat any other positive value as "in game, but not that initializer".
