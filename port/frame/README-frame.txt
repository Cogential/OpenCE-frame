Halo: Combat Evolved (OpenCE) for the Steam Frame
=================================================

A native ARM64 Linux build of OpenCE (github.com/OpenCommunityEdition/OpenCE),
from github.com/Cogential/OpenCE-frame. It runs the game image of OpenCE's
Android port (32-bit-pointer ARM64 code) in a small Linux host.

The game data is not included. You need your own Xbox disc image of
Halo: Combat Evolved (.iso or .xiso, any region).

Install
-------
1. Copy this Halo_CE folder to the Frame, e.g. ~/devkit-game/Halo_CE, and add
   run.sh to the Steam library (no compatibility tool needed).
2. Put the disc image next to run.sh, or in ~/.local/share/halo-ce/.
   The first start copies its maps/ folder into ~/.local/share/halo-ce
   (about 1.8 GB; this takes a minute). You can delete the image afterwards.
   Or copy a maps/ folder there yourself.

Files
-----
~/.local/share/halo-ce/       maps/, config.toml (settings), save/, debug.txt
~/.local/share/halo-ce/launch.log   the launcher's and the host's log

Controller look speed: Settings > (your profile) > Controls > Look Sensitivity.

This build does not update itself; get new releases from the GitHub page
(or Frametanium).
