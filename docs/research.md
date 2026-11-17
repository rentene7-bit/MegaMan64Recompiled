# Research harness

This is off unless CMake is configured with `-DMM64R_RESEARCH=ON`. A normal Release build does not compile it and does not read `MM64R_SCRIPT`. Output goes to `research/out/`, which is gitignored. Do not commit that folder, snapshots, diffs, or dumped textures. They are produced from the owner's ROM at runtime.

## Build on Windows (pc1)

From a checkout that already has the submodules and a working Release build:

```
cmake -S . -B build-research -G Ninja -DCMAKE_BUILD_TYPE=Release -DMM64R_RESEARCH=ON
cmake --build build-research --target MegaMan64Recompiled
```

Use the same generator and compiler you use for the normal game. The option only adds `src/research/script.cpp`, `src/research/research.cpp`, and the `MM64R_RESEARCH` define on the game target.

Call-set logging is a second switch. In `us.rev1.toml`:

```
trace_mode = true
```

Then rerun `N64Recomp` on `us.rev1.toml` and build again with `-DMM64R_RESEARCH=ON`. `include/trace.h` records each recompiled function while a script is between `calls_start` and `calls_stop`. Leave `trace_mode` false for playing. Rebuilding with `trace_mode` true and the CMake option off compiles the trace macros as empty, so it stays safe, but it also records nothing.

## Run

1. Put `portable.txt` next to the executable if you want saves and the ROM copy in that folder instead of AppData.
2. Accept the US ROM once in the launcher. The runtime stores it as `megaman.n64.us.1.0.z64` in the app folder. The hash it checks is `0x0c74c7489d01643b`.
3. Launch with a script:

```
set MM64R_SCRIPT=research\scripts\01_boot_new_game.txt
set MM64R_AUTOSTART=1
set MM64R_OUT=research\out
MegaMan64Recompiled.exe
```

On Linux, export the same variables. `MM64R_AUTOSTART=1` starts the stored ROM when it is valid, so the script does not have to click the launcher. The script command `autostart` does the same once that line runs.

`MM64R_OUT` defaults to `research/out`. Each run appends to `session.log`.

`MM64R_DUMP_TEXTURES` is read only by a research build. Set it to a directory and RT64 writes `<16 hex>.v5.tmem` files there. The submodule is not patched: after RT64 starts, the game assigns that directory on `Application::state`. A normal build ignores the variable.

## Script language

One command per line. `#` starts a comment. Buttons are `A B Z START DUP DDOWN DLEFT DRIGHT L R CUP CDOWN CLEFT CRIGHT`. Stick values are about -128 to 127.

| Command | Effect |
| --- | --- |
| `log <text>` | Write a line to `session.log`. |
| `timeout <frames>` | How long `wait_u*` may spin before the script continues. Default 600. |
| `wait <frames>` | Hold the current buttons and stick. |
| `hold <button>` / `release <button>` / `release_all` | Change the injected pad. While a script is active, real input is replaced. |
| `press <button> [frames]` | Hold, then release. Default 8 frames. |
| `stick <x> <y>` | Inject the left stick until the next `stick` or `release_all`. |
| `wait_u8/u16/u32 <addr> <op> <value>` | Block until the guest address matches. Ops: `eq ne lt gt le ge changed unchanged`. |
| `require_u8/u16/u32 <addr> <op> <value>` | If it does not match on the next frame, log `SKIP` and stop. |
| `echo <u8\|u16\|u32> <addr> <label>` | Log one value. |
| `poke <u8\|u16\|u32> <addr> <value>` | Write one guest value. The address is sign-extended the same way the recompiler does, so `0x80xxxxxx` lands in RDRAM. |
| `snapshot <name>` | Write an 8 MB RDRAM image to `snap_<name>.bin`. |
| `diff <a> <b>` | Changed bytes between two snapshots, guest addresses, capped at 500 ranges. |
| `diff_range <a> <b> <start> <length>` | Same, limited to a guest range. The file name gains `_<start>` so two ranges do not overwrite each other. |
| `watch <u8\|u16\|u32> <addr> <label>` | Log later changes. |
| `calls_start` / `calls_stop <label>` / `calls_diff <a> <b>` | Function-name sets. Empty until `trace_mode` is on. |
| `screenshot <name>` | Window grab (`<name>_window.ppm`) plus, if `set_fb` was used, the N64 framebuffer (`<name>_fb.ppm`). |
| `set_fb <addr>` | Guest address of the RGBA5551 framebuffer. |
| `autostart` | Ask the host to start the stored ROM. |

Memory reads use the recompiler's byte swap: a guest byte is at `(address - 0x80000000) ^ 3` in the host buffer, and a halfword at `^ 2`. `session.log` prints the host RDRAM pointer once the first graphics hook runs. For a precise writer, attach gdb or WinDbg and:

```
watch -l *(unsigned char*)(rdram_host + ((0x80ABCDEF - 0x80000000) ^ 3))
```

x86 has four hardware watchpoints. The script's `watch` lines are sampled once a frame and do not produce a backtrace; the call set for that same stretch is the coarse version of the backtrace.

## What to send back

Zip `research/out` after each script. Logs, diffs, call lists, and screenshots are the useful part. Do not commit the zip or the folder. Snapshots are full RDRAM and can contain the ROM's loaded data.

The scripts and the expected lines are in `research/scripts/` and `docs/research_hooks.md`.

The script parser has a standalone test:

```
g++ -std=c++20 -I include src/research/script.cpp src/research/script_test.cpp -o script_test
```
