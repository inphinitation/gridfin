# Gridfin – notes for contributors and agents

C++23 game that emulates the look *and limits* of the Sega Saturn, for desktop (wgpu-native) and the web
(Emscripten + emdawnwebgpu). See README.md for setup and docs/SATURN.md for the hardware model.

## Build and test

- Native: `cmake --workflow --preset dev` (configure, build Debug, run tests). Binary: `build/native/bin/Debug/gridfin`.
- Web: `source <emsdk>/emsdk_env.sh && cmake --workflow --preset web`.
- CI equivalent with warnings as errors: `cmake --workflow --preset ci`.
- Headless smoke test (Linux, needs Xvfb + a Vulkan driver such as lavapipe):
  `xvfb-run ./build/native/bin/Debug/gridfin --frames 60 --screenshot out.bmp`.
- Format before committing: `git ls-files '*.cpp' '*.hpp' | xargs clang-format -i`.
  Wrap JavaScript inside `EM_JS` in `// clang-format off/on` (the formatter breaks `=>`).

## Architecture rules

- `src/saturn` and `src/game` (the `gridfin_engine` library) must not depend on SDL or WebGPU. Keep them unit-testable.
- Rendering goes through the hardware model: game code submits world-space quads/billboards to `saturn::Geometry`,
  which writes VDP1 commands into `Vdp1Vram`; backgrounds are configured through `Vdp2Registers`. Do not add
  GPU-side shortcuts that bypass the model (depth buffers, texture filtering, tiling textures, triangle meshes).
- Respect hardware limits: texture widths multiple of 8 (≤ 504) and heights ≤ 255, VRAM budgets, RGB555 colour,
  2047 commands per frame. Assets are generated procedurally into Saturn formats in `src/game/Assets.cpp`.
- `shaders/vdp.wgsl` decodes the same bytes the CPU writes; if you change a memory layout, update both sides and the
  uniform mirrors (`FrameUniforms` in `src/gfx/SaturnRenderer.cpp`).
- Code against the standard `webgpu.h`; anything implementation specific must be behind `GRIDFIN_WGPU_NATIVE` or
  `__EMSCRIPTEN__`. WebGPU initialisation is asynchronous; never block the main loop (the browser cannot).
- WGSL reserved words bite (`layout`, `enable`, ...); run the native build to validate shaders.
