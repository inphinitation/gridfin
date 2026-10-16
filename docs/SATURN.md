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
- **Gouraud shading** adds per-corner RGB555 offsets (centred on 16) in 5-bit steps. It only applies to pixels that
  end up as direct RGB colours (RGB textures, lookup-table entries with the RGB bit, RGB polygons). Colour bank
  pixels are left alone, as on the hardware. Lighting and distance haze are computed per corner on the CPU and
  written to gouraud tables.
- **Colour calculation:** replace, shadow (halves the framebuffer, used for the blob shadow), half-luminance,
  half-transparency, and mesh (checkerboard "screen door" transparency).
- **No depth buffer.** `saturn::Geometry` buckets polygons into a 1024-entry Z ordering table by centre, farthest
  or nearest depth, with per-material bias, and emits them back to front. Sorting artefacts are expected.
- **No near-plane clipping.** A polygon with any vertex in front of the near plane is dropped.
- **Budget.** 2047 drawable commands per frame. Overflow is counted and shown in the window title.

## VDP1 drawing time: `src/saturn/Vdp1Timing.*`

Every frame's command list gets an estimated VDP1 drawing time, compared against one field
(28.64 MHz / 59.94 Hz ≈ 477,000 cycles):

- each command costs a fixed fetch/decode overhead, plus a gouraud table fetch when shading is on;
- quads and sprites are drawn line by line. A textured quad walks **at least one line per texture row** however
  small it is on screen, so tall textures on distant polygons are expensive, as on the real chip;
- every line costs its length in pixels plus a setup cost; shadow and half-transparency read the framebuffer and
  cost about twice as much per pixel;
- pixels outside the screen are stepped over but not written, at an eighth of the cost. The visible share is
  estimated from the primitive's bounding box. A huge primitive near the camera is still expensive, as on the
  real chip, but no longer costs as if every off-screen pixel were drawn.

Billboards (scaled sprites) switch to mesh transparency within 1.5 m of the camera and are culled within 0.8 m,
the way Saturn games faded sprites out before they could fill the screen.

When the estimate exceeds one field the frame stays on screen for as many fields as it needs, so the game drops
to 30 fps (or lower) the way Saturn games did. The window title shows the load; **F4** switches the enforcement
off for comparison. The constants are tuned approximations, not cycle-exact timings.

## Texture streaming: `src/saturn/TextureCache.*`

- Textures are registered into **work RAM** (a 1 MB budget, the low work RAM bank) in their VRAM format.
- VDP1 VRAM's texture area (about 432 KB after the command and gouraud tables) is a **cache**. A texture is copied
  in by "DMA" the first time a visible polygon uses it. Polygons are culled before their texture is requested, so
  only what is on screen is transferred.
- Each frame may transfer at most **32 KB** (`TextureCache::dmaBytesPerFrame`), roughly what fits in vertical blank.
- When the cache is full, the least recently used textures that the current frame does not need are evicted.
  Freed blocks are merged with their neighbours.
- A quad whose texture has not arrived yet is drawn as a flat polygon in the texture's average colour. A sprite
  is skipped until its texture arrives, so it pops in.
- The renderer uploads only the VRAM byte ranges that changed.

## Lighting: `saturn::Lighting` in `src/saturn/Geometry.*`

There is no lighting hardware; everything goes through the gouraud tables, as in SGL-era games:

- a directional **sun** (ambient + diffuse · N·L, per polygon face);
- up to four coloured **point lights** (`Lighting::kMaxPointLights`), added per corner and per colour channel with a
  quadratic falloff to their radius. Gouraud can only brighten by 15/31, so lights saturate rather than blowing out.
  The game lights the braziers nearest the player and lets them flicker;
- **distance haze** in gouraud steps: towards the pale sky by day, towards black at dusk.

Unlit materials (the fires) keep their texture colours. Sprites are lit as a whole from their middle. VDP2 layers
can't be lit: the dusk look on the backgrounds comes from the colour offset below.

**Palette cycling:** `TextureCache::setLut` rewrites a texture's 16-colour lookup table in VRAM. The fire sprite
rotates its colours 15 times a second, which makes bands of light climb the flame. Each rewrite is 32 bytes of DMA,
counted against the frame's budget.

## VDP2 (backgrounds): `src/saturn/Vdp2.*`, `shaders/vdp.wgsl`

- **512 KB VRAM image and 2048-entry CRAM** (colour RAM mode 1). Bitmaps must use hardware sizes (512/1024 × 256/512).
- **Back screen with a line colour table:** the sky is one colour per scanline. The game recomputes the table each
  frame for the camera pitch.
- **NBG0:** an 8bpp bitmap of clouds and mountains with transparent colour 0. Its bottom row rests on the horizon and
  it scrolls with the camera yaw.
- **RBG0:** the rotation plane rendered as an infinite ground. The texture repeats, there are no mipmaps (so the
  distance shimmers), and the plane fades into the haze colour in 32 colour-calculation ratio steps.
- **Colour offset:** two registers, A and B, each a signed (-256..255) value added to the 8-bit red, green and blue.
  Every layer (back screen, NBG0, RBG0 and the VDP1 sprite layer) can enable an offset and choose A or B. The game
  uses it to fade in from black at start-up (both registers) and for the dusk tint of the backgrounds (register A,
  backgrounds only, so the firelit polygons keep their own colours).

## Display: `shaders/ntsc.wgsl`, `shaders/present.wgsl`

The VDP output is identical in every mode; these filters only model how it reached the player's eyes.

- **NTSC composite (F6).** Every scanline is encoded into a composite signal: luma plus chroma on a 3.58 MHz
  subcarrier, about 0.53 cycles per Saturn dot, sampled 4 times per dot. The phase flips every line and every
  frame. It is then decoded like a TV: a low-pass filter for luma (some subcarrier leaks through, so dot crawl
  appears), and demodulation with a much narrower filter for chroma (colour bleeds over several dots, and sharp luma
  edges produce rainbow fringes). Mesh transparency's checkerboard turns into real transparency, as on a composite
  TV.
- **CRT (F2, last step).** Calculated in linear light:
  - each dot is drawn with a Gaussian beam spot about a dot wide, so neighbouring dots melt together;
  - each scanline has a Gaussian vertical profile that widens with brightness, so bright lines bloom into the gaps
    and dark lines leave visible scanlines;
  - a faint halation glow surrounds bright areas;
  - an aperture grille splits the light into red, green and blue stripes. It is only drawn when each dot is at
    least about three window pixels wide, to avoid moiré.
- **Scanlines (F2).** Sharp dots with light or dark gaps between lines.

The filters combine: NTSC plus CRT is the "composite cable into a consumer TV" look. Both cost noticeable GPU time on
very weak GPUs; the sharp mode is the cheapest.

## Known deviations

- Memory images are **little-endian**; the Saturn is big-endian. Field layouts are otherwise the same.
- RBG0 intersects a ray per pixel instead of evaluating per-line rotation coefficient tables. The result is the same
  for a camera without roll.
- Rasterisation follows GPU coverage rules: VDP1 includes edge pixels, so adjacent quads overlap by a pixel there.
  VDP1 overdraw and anti-aliasing quirks of distorted sprites are not reproduced.
- End codes in textures, high-speed shrink, user clipping, polylines/lines, sprite priority/colour-calculation bits
  and the VDP1 framebuffer double buffering/erase timing are not modelled.
- VDP2 features beyond the three layers above (cell/character patterns, more scroll planes, windows, sprite
  priorities, line scroll, mosaic) are not modelled yet.
- Colour offset is applied per pixel to the sprite layer before half-transparency blending; the hardware applies it
  to the final composited sprite layer. The difference only shows on half-transparent polygons.
- No frame-rate cap: the game runs at the display's refresh rate with a fixed 60 Hz simulation. Many Saturn 3D games
  ran at 30 fps.
- VDP1 timing is an estimate, as are the DMA budget and the work RAM split. Bus contention between the CPUs, DMA
  and VDP1 is not modelled.
