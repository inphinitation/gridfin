# Gridfin – notes for contributors and agents

C++23 game that emulates the look *and limits* of the Sega Saturn, for desktop (wgpu-native) and the web
(Emscripten + emdawnwebgpu). See README.md for setup and docs/SATURN.md for the hardware model.

## Build and test

- Native: `cmake --workflow --preset dev` (configure, build Debug, run tests). Binary: `build/native/bin/Debug/gridfin`.
- Web: `source <emsdk>/emsdk_env.sh && cmake --workflow --preset web`.
- CI equivalent with warnings as errors: `cmake --workflow --preset ci`.
- Headless smoke test (Linux, needs Xvfb + a Vulkan driver such as lavapipe):
  `xvfb-run ./build/native/bin/Debug/gridfin --frames 60 --screenshot out.bmp`. To check display filters, add
  `--filter crt --ntsc --capture-display` (saves the filtered window output instead of the framebuffer). Place the
  player with `--spawn x z yawDegrees` (e.g. `--spawn -4.5 5 0 --first-person` looks across the pond).
- Render the music offline: `./build/native/bin/Debug/gridfin --render-music out.wav 30` (no window needed).
- Format before committing: `git ls-files '*.cpp' '*.hpp' | xargs clang-format -i`.
  Wrap JavaScript inside `EM_JS` in `// clang-format off/on` (the formatter breaks `=>`).

## Architecture rules

- `src/saturn` and `src/game` (the `gridfin_engine` library) must not depend on SDL or WebGPU. Keep them unit-testable.
- Rendering goes through the hardware model: game code submits world-space quads/billboards to `saturn::Geometry`,
  which writes VDP1 commands into `Vdp1Vram`; backgrounds are configured through `Vdp2Registers`. Do not add
  GPU-side shortcuts that bypass the model (depth buffers, texture filtering, tiling textures, triangle meshes).
- Respect hardware limits: texture widths multiple of 8 (≤ 504) and heights ≤ 255, VRAM budgets, RGB555 colour,
  2047 commands per frame. Assets are generated procedurally into Saturn formats in `src/game/Assets.cpp`.
- VDP1 textures are registered in `saturn::TextureCache` (work RAM) and referenced by `TextureId`; never write
  textures into `Vdp1Vram` directly. Watch the VDP1 load in the window title: over 100% halves the frame rate.
- Lighting is CPU-side, through gouraud tables (`saturn::Lighting`: sun, at most 4 point lights, haze). Screen-wide
  tints and fades use the VDP2 colour offset registers. Animate colours by palette cycling (`TextureCache::setLut`),
  not by shader effects. Reflections are mirrored geometry between `Geometry::beginReflection/endReflection`,
  clipped to a CPU-computed VDP2 line window (`saturn::computeLineWindow`).
- Game content is data, not code: it lives in `data/base/` and is read through `data::Vfs` (folders or `.pak`
  archives, later mounts override earlier ones). Formats are documented in `docs/DATA.md` and `docs/SOUND.md`;
  update them with any format change. Loaders report problems as `file:line: message` and keep going.
- Scripts are Wren modules in the game data, run by `script::ScriptVm` (sandboxed, step and memory budgets; see
  `docs/SCRIPTING.md`). Engine features reach scripts only through bound foreign methods. Don't patch Wren beyond
  `cmake/PatchWren.cmake`.
- Audio goes through the SCSP model: samples live in `saturn::Scsp` sound RAM (512 KB, shared with the driver area
  and the reverb delay lines), music is MML played by `saturn::SoundDriver` on its 32 slots. No host-side mixing or
  effects; `platform/Audio` only copies the SCSP output to SDL.
- `shaders/vdp.wgsl` decodes the same bytes the CPU writes; if you change a memory layout, update both sides and the
  uniform mirrors (`FrameUniforms` in `src/gfx/SaturnRenderer.cpp`).
- Code against the standard `webgpu.h`; anything implementation specific must be behind `GRIDFIN_WGPU_NATIVE` or
  `__EMSCRIPTEN__`. WebGPU initialisation is asynchronous; never block the main loop (the browser cannot).
- WGSL reserved words bite (`layout`, `enable`, ...); run the native build to validate shaders.
