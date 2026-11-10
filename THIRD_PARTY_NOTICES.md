# Third-Party Notices

## Zeal

Gusto is a RoF2 compatibility project based on the Zeal project.

Zeal:
Copyright (c) 2024 Clint

The procedural tag geometry in `src/client/rof2/tag_models.cpp` and the tag
protocol/field semantics in `src/client/rof2/tag_state.cpp` are adapted from
Zeal 1.4.8's `tag_arrows.cpp` and `nameplate.cpp`. Gusto supplies the RoF2
integration and Direct3D 9 renderer. Map projection, height filtering, combat
text presentation and camera smoothing in `hud_model.cpp` / `hud_overlay.cpp`
also adapt Zeal 1.4.8 behavior (`zone_map`, `floating_damage`, `camera_math`
and nameplate resource-bar / HP-update presentation).

Zeal is distributed under the MIT License. Its license terms are
reproduced below:

MIT License

Copyright (c) 2024 Clint

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## Hailstone

The modern minimap layout, Midnight colors, font sizes, north indicator and
zoom controls are adapted from `hailstone-client/ui/modern/minimap.lua` and
`ui/modern/lib/palettes.lua`. Gusto reimplements the presentation in Direct3D 9
and uses RoF2 memory layouts rather than Hailstone runtime interfaces.

MIT License

Copyright (c) 2026 gennro

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## Named pipes and JSON

The pipe type numbering, JSON envelope and player/group/raid/label/gauge field
semantics in `named_pipe.cpp`, `pipe_model.cpp` and `pipe_snapshot.cpp` adapt
Zeal 1.4.8's `named_pipe.cpp` / `named_pipe.h`, under the Zeal MIT license above.
Gusto supplies the RoF2 layouts and a bounded, polled overlapped transport.

`include/gusto/third_party/json.hpp` is JSON for Modern C++ 3.11.3, vendored
from the local Zeal source (including its warning-suppression changes).

MIT License

Copyright (c) 2013-2023 Niels Lohmann

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
