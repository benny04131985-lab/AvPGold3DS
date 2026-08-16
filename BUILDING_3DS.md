# Building AvP3DS v1.0.0

The final release build uses the project's `Makefile.3ds`.

Requirements:

- devkitPro / devkitARM
- Nintendo 3DS libraries used by this source tree (libctru, Citro3D/Citro2D)
- GNU Make
- the self-contained SDL2 3DS headers/static library under `Libraries/SDL2/`

From the repository root:

```bash
make -f Makefile.3ds -j4
```

Expected primary output:

```text
AvP_Gold.3dsx
```

The final public source snapshot contains the marker:

```text
AVP3DS-V1.0.0-FINALSHIP1
```

That marker identifies the release source copy where hardware-validation
benchmark/HEADROOM callsites have been deactivated. The development source tree
used to collect the final hardware evidence is intentionally left untouched by
the release builder.
