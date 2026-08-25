UPDATE - I have been made aware of a bug that causes hitching, frame stuttering, and unfavorable performance. There is a situation with the original music/fmv files that is at fault along with a work-around that was not distributed in the GitHub version. I will release an initial hotfix to address the performance issue, and will follow-up with a solution to the retail music/fmv directly. Thank you for you patience as I get this sorted.

# AvP3DS

Nintendo 3DS port of **Aliens versus Predator / AvP Gold**, prepared from the
finished hardware-validated stereo source tree.

Current release: **v1.0.0**

## v1.0.0 scope

- Single-player Marine, Predator and Alien campaigns.
- Native 3DS/Citro3D upper-screen world renderer.
- True stereoscopic 3D with flat menu/death/sight presentation where required.
- Species-specific lower-screen HUD presentation.
- Save/load/restart and clean return/exit paths.
- Final 3DS menu lockdown removes unsupported desktop multiplayer/network,
  video-mode/detail and PC input-configuration routes.
- Final handheld locomotion tuning keeps the original species speed ratios while
  scaling player forward/back/strafe movement to 65% of the previous port speed.

## Original game data

Commercial Aliens versus Predator game data is **not** part of the intended
public source/release payload. Use your own legally obtained game data with the
layout expected by this port.

## Building

See [BUILDING_3DS.md](BUILDING_3DS.md).

## Installing

See [INSTALLING.md](INSTALLING.md).

## Known limitations

See [KNOWN_ISSUES.md](KNOWN_ISSUES.md).

## Upstream / licensing

This tree preserves the upstream license/notice files present in the source
snapshot. See [UPSTREAM_AND_LICENSE.md](UPSTREAM_AND_LICENSE.md).
