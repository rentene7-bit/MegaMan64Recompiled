# Hook candidates

Static pass on the US 1.0 ROM (SHA1 `f24fe0aff01aec018e2dd558ec4f076cf328129f`). Nothing here is wired. Addresses are guest RAM unless noted as a function. Confidence is high only where a published code and the disassembly name the same field.

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

| Hook | Address or function | Evidence | Confidence | pc1 script |
| --- | --- | --- | --- | --- |
| Player object | `0x802049B0`, passed as `a0` to `func_80069A64` from `func_80069D44` | The code builds this pointer with `lui`/`addiu`. Spawn sets halfword `+0x6E` to 80. | High | `01_boot_new_game.txt` echoes it |
| Player HP | `0x80204A1E` (`player+0x6E`) | GameShark `81204A1E 0050` is that address. The hurt function `func_8006E838` subtracts from `+0x6E`. Already scaled, not part of this pass. | High | `echo u16 0x80204A1E` should print 80 after a fresh spawn |
| Buster part slots | `0x80204E00` through `0x80204E02` equipped, `0x80204E04` onward inventory | GameShark byte codes. `0x80204E00` is `player+0x450`. `func_80060590` builds the absolute address. `func_80056608` reads offset `0x450`. The published enum starts at 0 = empty. These are parts, not the special weapon. | High for the bytes, not for "which part is equipped right now" until a diff | `02_inventory_and_weapon.txt` watches `buster_part_0` |
| Special-weapon ownership | `0x80205640` halfword, `player+0xC90` | GameShark `81205640 FFFF`, "have all special weapons", and the note that Roll still has to equip them. No instruction in the main code segment uses the immediate `0xC90` or `0x5640` as that address, so the writer is probably an overlay or a block copy. | Medium for the address, low for the writer | `02` watches `owned_weapons`. It should not change when cycling equipment. |
| Equipped special weapon | Unknown byte in `0x802049B0..0x802059B0` | Ownership is a bitfield. Ammo records start at `0x80204EBA` (`player+0x50A`) and step by 8 for the twelve published weapons (machine buster through shining laser). The equipped index should select one record. Poking the ownership halfword desyncs the model; the wheel has to call the writer. | Low until the diff | `02` diffs `idle`/`cleft`/`cleft_again`. The byte that flips and flips back is the candidate. |
| Ammo records | `0x80204EBA` then +8, twelve times | The twelve infinite-ammo GameShark halfwords are exactly 8 bytes apart. | High for the array, low for which function spends ammo | `02` watches `ammo_0` |
| Zenny and play time | `0x801BC406` zenny, `0x801BC3F4` / `0x801BC3F6` time | GameShark `811BC406` and `811BC3F4`. `func_8004B7B8` builds `0x801BC3F4`. This is a status block, not proof of a room id. | High for those fields | `04_walk_door.txt` diffs `0x801BC000` for `0x1000` |
| Room id / door transition | Unknown byte in `0x801BC000..0x801BD000`, writer unknown | No published room id. A door should change one byte that a second walk in the same room does not. | Low | `04_walk_door.txt` plus `calls_diff walk walk_same_room` |
| Fast travel | Same writer as the door, not found | The support-car / airship trip should hit that function once a door has identified it. | Low | No separate script until `04` names the function |
| Text printer and delay | Not `func_80050330` | That function counts down `player+0xA5` and steps 8-byte action commands. It also runs for attacks and landings, so it is not a safe text-speed hook. Dialogue is not ASCII in the main ROM. The manual's interact button is C-down. | Low | `03_talk_once.txt`. The climbing value in the player-object diff is the character index; the countdown just before it is the delay. |
| Chest and door flags | Unknown bits, likely in the `0x801BC000` status block or a save image | No counter is published. One chest should flip one bit. The writer then reveals the record format. | Low | Stand in front of a chest and run `03` or a copy that presses C-down. Compare `diff_range` on `0x801BC000`. |
| Item and chest tables | No ROM offset | No price-like table in the first 4 MB. Stage geometry docs are PS1/Legends 2 and do not give N64 item records. Prefer a give-item hook over editing packed stage data, once a write watch on an inventory slot (`0x80205650` normal items, `0x8020564C` special items, both GameShark) names the function. | Low | After `02`, a later script can `watch u16 0x80205650 items` while a chest is opened |
| Boss start | `func_80049948` already receives the bar in `a0`; actor pointer at `+0x20`; HP at actor `+0x6E` | The encounter setup is whoever writes that pointer from null to non-null. Not visible from the title. | Medium for the bar, low for the starter | Needs a save at a boss door. `calls_start` before the fight and `calls_stop boss`. |
| Lock-on | Z bit `0x2000`. Player buttons were read at `player+0x7C` inside `func_80069A64`. The zero-stores next to state byte `+0x74` are animation resets, not a stick mute. | No GameShark lock flag. Movement while locked is the difference between the walk call set and the Z-held call set, plus whether `player+0x14` keeps changing. | Low | `05_lockon.txt` |
| Save states | Not a single address | RDRAM is 8 MB (`osMemSize` is set to that). Threads, audio queues, and RT64 caches live outside it. A same-session snapshot at one main-loop point can work later. Practice mode can instead warp plus restore the save block. `quicksaving.cpp` stays compiled out. | High that a raw RDRAM dump is not a save state | `snapshot` is the raw image, for diffs only |
| Texture hashes | RT64 developer panel, "Start dumping textures" | No game function. Dump names carry the runtime hash. Upscale in place and keep the 16 hex digits. Palette animation emits several hashes. | High for the method | `06_texture_walk.txt` while dumping is on |
| CRT | Final blit in RT64, not the game | This pin has no CRT setting. A fork would change the swapchain blit shader. | High that it is renderer work | None |
| Outlines | No marker between world and HUD | Needs an extended display-list command at that boundary, then a depth edge pass. | Medium that the game does not already have it | None |
| Dynamic lights | Actor light list, writer unknown | Cheapest later hook is before the actor draw, found by watching the display-list head. Not in `UserConfiguration`. | Low for the function, high that the current pin cannot toggle it | None |
| Shadows | No shadow map in this pin | Do this last. | High that it is not a small patch | None |

## ROM scan notes

The main code segment forms `0x802049B0` in `func_80069D44` and in many other functions. It does not form `0x80205640` as an immediate. Pointer tables exist (the longest run in the first 2 MB is 334 words at ROM `0x8EDE0`, first word `0x800B3F18`). Those are code or data pointers, not a price list, and the words are not copied into this repo.

`func_80069860` writes 80 to both `player+0x6E` and `player+0xB0` on one spawn path. Treat 80 as the expected fresh HP, and treat any other positive value as "in game, but not that initializer".
