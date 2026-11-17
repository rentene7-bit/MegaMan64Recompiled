# Steam Deck controller template

`controller_neptune.vdf` is a Steam Input template for the Deck (Neptune). It is not applied by an installer. In-game bindings stay the source of truth; this file only adds the back grips and a mouse on the right trackpad.

| Deck control | Steam Input output |
| --- | --- |
| Face buttons, sticks, D-pad, L1/R1, L2/R2, Start, View | The same gamepad buttons |
| R4 (upper right grip) | A |
| R5 (lower right grip) | B (East) |
| L4 (upper left grip) | Y |
| L5 (lower left grip) | Left stick click |
| Right trackpad | Mouse, click is left click |
| Left trackpad | Unbound |
| Gyro | Off |

Steam names the upper grips `button_back_*_upper` and the lower grips `button_back_left` / `button_back_right`. If a hardware check shows L4/L5 or R4/R5 swapped, exchange those two bindings in the `switches` group.

## Install for a non-Steam shortcut

1. In Steam, use **Games → Add a Non-Steam Game** and choose the `MegaMan64Recompiled` executable.
2. Copy this file to the template folder. On a Deck that is usually:

   `~/.local/share/Steam/controller_base/templates/MegaMan64Recompiled_controller_neptune.vdf`

   `~/.local` is hidden. In Dolphin, open the menu and turn on **Show Hidden Files**.
3. Restart Steam if it was already open.
4. Open the shortcut, select the controller icon, and choose the template **Mega Man 64 Recompiled**.

If the template does not appear, Steam has already created a per-shortcut folder under:

`~/.local/share/Steam/steamapps/common/Steam Controller Configs/<account id>/config/<shortcut id>/`

Copy the file there as `controller_neptune.vdf` and restart Steam. The shortcut id is the number Steam uses for that non-Steam game.

## Check that it took

In the layout view, before launching:

- R4 shows A, R5 shows B, L4 shows Y, and L5 shows left-stick click.
- The right trackpad is a mouse and its click is left mouse button. Use that click on the ROM file dialog.
- The left trackpad and gyro have no binding.

In game, after **Reset to defaults** (or on a new `controls.json`): A jumps, B interacts, X shoots, Y is the special weapon, L1 and right-stick-left turn left, R1 and right-stick-right turn right, L2 locks on, R2 shoots, L3 opens the map, and R3 looks around.
