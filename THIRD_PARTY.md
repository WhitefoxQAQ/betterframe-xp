# Third-party code

The original code in this repository is under the MIT License (`LICENSE`).
Code taken from other projects keeps its own license, as listed below.

- `layer/` is derived from FrameColor's modified vkBasalt
  (https://github.com/beko-kerbecotton/framecolor, `third_party/vkbasalt`), itself from
  vkBasalt (https://github.com/DadSchoorse/vkBasalt), zlib license — see `layer/LICENSE`.
  Modified: LUT effect removed, PostFx effect added, layer name / env vars renamed.
- CAS logic in `layer/src/shader/postfx.frag.glsl`: AMD FidelityFX CAS, via vkBasalt
  `src/shader/cas.frag.glsl`. Full license text below.
- Deband logic in `layer/src/shader/postfx.frag.glsl`: inspired by haasn's deband shader,
  via vkBasalt `src/shader/deband.frag.glsl`; rewritten/simplified. Full license text below.

## panel/ (betterframe-xp-panel)
- `panel/src/{draw,vk_texture,vr_overlay}.*` : FrameColor (MIT, `panel/LICENSE.framecolor`). Dashboard key/name changed to `local.betterframe-xp` / `BetterFrame XP`.
- `panel/third_party/openvr/openvr.h` : Valve OpenVR SDK (BSD-3-Clause, `panel/third_party/openvr/LICENSE`).
- `panel/src/gpu_usage.*` : the author's own separate tool, `frame-diag-overlay` (not part of this repository).
- Eye-tracking correction switches (`VrOverlay::readGazeSettings` / `setSteamVrBool` in `panel/src/vr_overlay.*`, `Gaze` in `panel_main.cpp`): taken from the author's own `frame-diag-overlay` tool (not part of this repository). `betterframectl gaze` / `uninstall` do the same through SteamVR's `vrcmd`. They only call OpenVR's `IVRSettings::GetBool` / `SetBool` on `steamvr.gazeDepReprojectEnable` / `steamvr.gazeDepEyeToHeadEnable`.

- The update tab (`panel/src/update_check.*`, `contrib/betterframe-xp-update`) follows the idea of
  frameeyeosc's updater (https://github.com/sasaken1102r/frameeyeosc, MIT): ask GitHub for the latest
  release and install it from the panel. It was written from scratch for this project; no code was copied.

---

## Full license texts

### AMD FidelityFX CAS (via vkBasalt `src/shader/cas.frag.glsl`)

```
Copyright (c) 2017-2019 Advanced Micro Devices, Inc. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation
files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy,
modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE
```

### haasn deband (via vkBasalt `src/shader/deband.frag.glsl`)

```
Deband shader by haasn
https://github.com/haasn/gentoo-conf/blob/xor/home/nand/.mpv/shaders/deband-pre.glsl

Copyright (c) 2015 Niklas Haas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
