# AvP3DS controls

The Nintendo 3DS build uses a fixed native input bridge rather than the desktop
PC control-configuration menus.

Core handheld controls proven by the final source:

- Circle Pad: player movement / strafe
- C-nub: mouse-look style camera control
- START: in-game menu / pause path
- Native 3DS face, shoulder and D-pad buttons are translated through the fixed
  3DS input mapping in `src/main.c` / `src/main_3ds.c`.

The v1.0.0 ship build applies a 65% multiplier to player forward/back/strafe
locomotion after species/run/walk/encumbrance rules. Turning and jump speed are
not scaled by that ship tuning.
