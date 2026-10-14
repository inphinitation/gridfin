# Saturn fidelity notes

Gridfin aims to look like it runs on a Sega Saturn by working within the console's limits rather than by imitating
the look in post-processing. This page lists what is modelled and where the emulation knowingly differs.

## Frame structure

| | Saturn | Gridfin |
| --- | --- | --- |
| Resolution | 320x224 (NTSC, non-interlaced), shown at 4:3 | Same; projection uses non-square pixels (pixel aspect 0.93) |
| Colour | 15-bit RGB (RGB555), palettes in CRAM | Every colour is RGB555; the present pass quantises the output to 32768 colours |
| Composition | VDP1 framebuffer over VDP2 planes | VDP2 pass, then VDP1 commands, then scaling to the window |

## VDP1 (sprites and polygons): `src/saturn/Vdp1.*`, `shaders/vdp.wgsl`

- **VRAM is a byte-exact 512 KB image.** The command table (2048 × 32 bytes), gouraud tables (8 bytes per
  command), textures and lookup tables all live in it, at hardware sizes. Running out of VRAM fails just as it would
  on the console. The fragment shader reads textures directly from this image.
- **Command format.** Commands use the real field layout: CMDCTRL, CMDLINK, CMDPMOD, CMDCOLR, CMDSRCA, CMDSIZE,
  XA–YD and CMDGRDA. Supported commands are normal sprite, scaled sprite, distorted sprite (textured quad), polygon
  and the end code.
- **Quads, not triangles.** A quad maps its texture with an inverse *bilinear* transform across all four corners,
  which is how VDP1 walks a distorted sprite. Quads bend instead of breaking along a diagonal, and there is no
  perspective correction.
- **Each texture is applied once per quad, with no tiling and no filtering.** Large surfaces are therefore cut into
  tiles (`World::kTileSize`), as Saturn games did.
- **Texture rules.** Width is a multiple of 8 (at most 504) and height at most 255. Colour modes: 4bpp bank, 4bpp
  lookup table, 8bpp bank (64/128/256), and 16bpp RGB. Colour 0 (or 0x0000 in RGB mode) is transparent unless SPD is set.
- **Integer vertices.** The CPU snaps vertices to whole pixels, which gives the familiar wobble. Coordinates must fit
  VDP1's 13-bit range.
- **Gouraud shading** adds per-corner RGB555 offsets (centred on 16) in 5-bit steps. Lighting and distance haze are
  computed per corner on the CPU and written to gouraud tables.
- **Colour calculation:** replace, shadow (halves the framebuffer, used for the blob shadow), half-luminance,
  half-transparency, and mesh (checkerboard "screen door" transparency).
- **No depth buffer.** `saturn::Geometry` buckets polygons into a 1024-entry Z ordering table by centre, farthest
  or nearest depth, with per-material bias, and emits them back to front. Sorting artefacts are expected.
- **No near-plane clipping.** A polygon with any vertex in front of the near plane is dropped.
- **Budget.** 2047 drawable commands per frame. Overflow is counted and shown in the window title.

## VDP2 (backgrounds): `src/saturn/Vdp2.*`, `shaders/vdp.wgsl`

- **512 KB VRAM image and 2048-entry CRAM** (colour RAM mode 1). Bitmaps must use hardware sizes (512/1024 × 256/512).
- **Back screen with a line colour table:** the sky is one colour per scanline. The game recomputes the table each
  frame for the camera pitch.
- **NBG0:** an 8bpp bitmap of clouds and mountains with transparent colour 0. Its bottom row rests on the horizon and
  it scrolls with the camera yaw.
- **RBG0:** the rotation plane rendered as an infinite ground. The texture repeats, there are no mipmaps (so the
  distance shimmers), and the plane fades into the haze colour in 32 colour-calculation ratio steps.

## Known deviations

- Memory images are **little-endian**; the Saturn is big-endian. Field layouts are otherwise the same.
- RBG0 intersects a ray per pixel instead of evaluating per-line rotation coefficient tables. The result is the same
  for a camera without roll.
- Rasterisation follows GPU coverage rules: VDP1 includes edge pixels, so adjacent quads overlap by a pixel there.
  VDP1 overdraw and anti-aliasing quirks of distorted sprites are not reproduced.
- End codes in textures, high-speed shrink, user clipping, polylines/lines, sprite priority/colour-calculation bits
  and the VDP1 framebuffer double buffering/erase timing are not modelled.
- VDP2 features beyond the three layers above (cell/character patterns, more scroll planes, windows, sprite
  priorities, line scroll, mosaic, colour offset) are not modelled yet.
- No frame-rate cap: the game runs at the display's refresh rate with a fixed 60 Hz simulation. Many Saturn 3D games
  ran at 30 fps.
- The polygon budget is a fixed command-table size, not a measurement of VDP1 fill rate.
