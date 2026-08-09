# The Legend of Zelda: A Link to the Past 3DS

![The Legend of Zelda: A Link to the Past 3DS](Git3DS.png)

Nintendo 3DS dual-screen port of Zelda3, adapted for Old 3DS and New 3DS with
help from Codex.

[Leia em portugues (PT-BR)](README.pt-BR.md)

This project is based on open-source work from:

- Original reverse-engineered Zelda3 engine: https://github.com/snesrev/zelda3
- Android port base: https://github.com/Waterdish/zelda3-android
- Dual-screen Android branch used as the 3DS source base:
  https://github.com/samyost1/zelda3-android

No ROM or extracted game asset package is distributed in this repository. Each
user must provide their own legally obtained USA, unheadered ROM on their own
3DS SD card.

## Current status

- Current stable version: `v1.0`
- Repository: https://github.com/BC-Estrategias/zelda-alttp-3ds
- Releases page: https://github.com/BC-Estrategias/zelda-alttp-3ds/releases

## Nintendo 3DS features

- Top screen: 400x240 gameplay.
- Bottom screen: 320x240 live map, dungeon map, gear view, item selection and
  touch settings.
- First launch extracts `zelda3_assets.dat` locally from the user's ROM.
- Display modes: wide, stretched original and original aspect.
- Turbo speed: off, x2, x3, x4 or x5.
- New 3DS: hold `ZR` for turbo when turbo is enabled.
- Quick save state: `L + ZL`.
- Quick load state: `R + ZR`.
- Quick diagnostics: press `L + R + A` to create a dump with memory files plus
  top and bottom screenshots.
- PICA200/Citro2D presentation for both screens with nearest-neighbor sampling
  and RGB565 display output.
- Fixed-step 60 Hz gameplay timing with bounded catch-up.
- Parallel PPU scanline rendering on Core 0 and Core 1, plus Core 2 on New 3DS.
- HOME Menu banner uses a lightweight custom CGFX 3D logo model with the
  supplied hover sound converted to a short PCM WAV.

## Installation

Download the latest published build from the releases page:

https://github.com/BC-Estrategias/zelda-alttp-3ds/releases

Each release is expected to include:

- installable CIA
- Homebrew Launcher 3DSX
- QR code for FBI installation

After installing the CIA, create this directory on the SD card:

```text
sdmc:/3ds/Zelda 3DS/
```

Place a legally obtained USA, unheadered ROM there. The preferred filename is
`zelda3.sfc`, but the setup also accepts other `.sfc` or `.smc` filenames.

On first launch, press `A` to validate the ROM and extract the assets. The ROM
is read locally and is never copied into the CIA.

Audio requires:

```text
sdmc:/3ds/dspfirm.cdc
```

Luma3DS can create this file from the console's own firmware through Rosalina's
`Dump DSP firmware` command.

## Building

Requirements:

- `cmake`
- devkitARM, libctru and 3ds-cmake under `DEVKITPRO`
- `makerom` and `bannertool` for CIA packaging
- the vendored SDL2 source in `app/jni/SDL2`
- `banner.cgfx` is prebuilt in `platform/3ds/assets`

Build:

```sh
chmod +x platform/3ds/build.sh
platform/3ds/build.sh
```

The script builds the 3DSX and CIA under `build-3ds/game/`.

## Legal

This repository contains only source code, build scripts, redistributable port
assets and patch/extraction logic. It does not include a ROM, extracted game
assets, or `zelda3_assets.dat`.

Users are responsible for providing their own legally obtained compatible ROM.
