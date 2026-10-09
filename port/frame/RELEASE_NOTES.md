Halo: Combat Evolved (OpenCE) for the **Steam Frame**: a native ARM64 Linux build.

It runs OpenCE's Android game image (ILP32 AArch64) in a small Linux host (`port/frame`), with OpenGL ES 3.2 through Mesa on the Frame and SDL3.

**No game data is included.** You need your own Xbox disc image of Halo: Combat Evolved. Put it next to `run.sh` (or in `~/.local/share/halo-ce/`) and the first start copies its maps out. Full instructions are in `README-frame.txt`.

- Install: unzip to `~/devkit-game/Halo_CE` and add `run.sh` to Steam (no compatibility tool), or install it with Frametanium.
- Based on OpenCE build-157 (73dc01d).
- Tested on a Steam Frame: menus, controller, campaign and multiplayer menus.
