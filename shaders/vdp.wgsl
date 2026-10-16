// Sega Saturn VDP1 + VDP2 emulation.
//
// Both VDPs read their real memory images: VDP1 VRAM (command table, gouraud
// tables, textures, lookup tables), VDP2 VRAM (bitmaps, line colour table) and
// colour RAM. All colour math happens in 5 bits per channel (RGB555).

struct Frame {
    screen: vec4f,     // width, height, local origin x, local origin y
    focal: vec4f,      // focal x, focal y, time, unused
    camPos: vec4f,
    camRight: vec4f,
    camUp: vec4f,
    camForward: vec4f,
    ground: vec4f,     // ground height, texels per unit, fade start, fade end
    sky: vec4f,        // NBG0 scroll x, horizon row, unused, unused
    vdp2: vec4u,       // ground bitmap address, cloud bitmap address, line colour table address, layer enable bits
    sizes: vec4u,      // ground size (w | h << 16), cloud size, ground bank | cloud bank << 16, haze colour
    offsetA: vec4i,    // colour offset A (r, g, b in 8-bit steps), layer enable bits
    offsetB: vec4i,    // colour offset B (r, g, b in 8-bit steps), layer select bits (1 = use B)
    water: vec4u,      // window table address, reflection line scroll address, water colour, ratio | enabled << 8
}

// Layer bits of the colour offset registers.
const kLayerBack = 1u;
const kLayerNbg0 = 2u;
const kLayerRbg0 = 4u;
const kLayerSprite = 8u;
const kLayerNbg1 = 16u;

@group(0) @binding(0) var<uniform> frame: Frame;
@group(0) @binding(1) var<storage, read> vdp1Vram: array<u32>;
@group(0) @binding(2) var<storage, read> vdp2Vram: array<u32>;
@group(0) @binding(3) var<storage, read> cram: array<u32>;

// --- Memory access -----------------------------------------------------------

fn vdp1Byte(address: u32) -> u32 {
    return (vdp1Vram[address >> 2u] >> ((address & 3u) * 8u)) & 0xFFu;
}

fn vdp1Word(address: u32) -> u32 {
    return (vdp1Vram[address >> 2u] >> ((address & 2u) * 8u)) & 0xFFFFu;
}

fn vdp2Byte(address: u32) -> u32 {
    return (vdp2Vram[address >> 2u] >> ((address & 3u) * 8u)) & 0xFFu;
}

fn vdp2Word(address: u32) -> u32 {
    return (vdp2Vram[address >> 2u] >> ((address & 2u) * 8u)) & 0xFFFFu;
}

fn cramWord(index: u32) -> u32 {
    let i = index & 0x7FFu;
    return (cram[i >> 1u] >> ((i & 1u) * 16u)) & 0xFFFFu;
}

fn signed16(value: u32) -> f32 {
    return f32(bitcast<i32>(value << 16u) >> 16u);
}

/// Unpacks RGB555 into 0..31 per channel.
fn rgb5(word: u32) -> vec3f {
    return vec3f(f32(word & 31u), f32((word >> 5u) & 31u), f32((word >> 10u) & 31u));
}

/// Colour words with the MSB set are direct RGB, everything else indexes colour RAM.
fn resolveColor(code: u32) -> vec3f {
    if (code & 0x8000u) != 0u {
        return rgb5(code);
    }
    return rgb5(cramWord(code));
}

/// VDP2 colour offset: adds a signed 8-bit offset to a layer's colour (here in 0..31 units).
fn colorOffset(color: vec3f, layer: u32) -> vec3f {
    if (u32(frame.offsetA.w) & layer) == 0u {
        return color;
    }
    let offset = select(frame.offsetA.xyz, frame.offsetB.xyz, (u32(frame.offsetB.w) & layer) != 0u);
    return clamp(color + vec3f(offset) * (31.0 / 255.0), vec3f(0.0), vec3f(31.0));
}

/// Window 0, a line window: a [start, end) span per scanline in VDP2 VRAM.
fn insideWindow(pixel: vec2u) -> bool {
    let entry = frame.water.x + pixel.y * 4u;
    return pixel.x >= vdp2Word(entry) && pixel.x < vdp2Word(entry + 2u);
}

/// NBG0 at screen position (x, y): a bitmap whose bottom row rests on the horizon, scrolled
/// horizontally with the camera yaw. Alpha is 0 where the bitmap is transparent or absent.
fn nbg0Pixel(x: f32, y: f32) -> vec4f {
    let width = frame.sizes.y & 0xFFFFu;
    let height = frame.sizes.y >> 16u;
    let row = i32(floor(y - frame.sky.y)) + i32(height);
    if row < 0 || row >= i32(height) {
        return vec4f(0.0);
    }
    let column = u32(i32(floor(x + frame.sky.x)) & i32(width - 1u));
    let index = vdp2Byte(frame.vdp2.y + u32(row) * width + column);
    if index == 0u {
        return vec4f(0.0);
    }
    return vec4f(rgb5(cramWord((frame.sizes.z >> 16u) + index)), 1.0);
}

// --- VDP2 ----------------------------------------------------------------------

@vertex
fn fullscreenVertex(@builtin(vertex_index) index: u32) -> @builtin(position) vec4f {
    let uv = vec2f(f32((index << 1u) & 2u), f32(index & 2u));
    return vec4f(uv * 2.0 - 1.0, 0.0, 1.0);
}

@fragment
fn vdp2Fragment(@builtin(position) position: vec4f) -> @location(0) vec4f {
    let pixel = vec2u(position.xy);
    let layers = frame.vdp2.w;

    // Back screen: one colour per scanline.
    var color = rgb5(vdp2Word(frame.vdp2.z + pixel.y * 2u));
    var layer = kLayerBack;

    // NBG0: a bitmap whose bottom row rests on the horizon, scrolled by camera yaw.
    if (layers & 2u) != 0u {
        let nbg0 = nbg0Pixel(position.x, position.y);
        if nbg0.a > 0.0 {
            color = nbg0.rgb;
            layer = kLayerNbg0;
        }
    }

    // RBG0: rotation plane drawn as an infinite ground. The hardware evaluates
    // per-line rotation coefficients; intersecting a ray per pixel is the same math.
    let origin = frame.screen.zw;
    let ray = frame.camForward.xyz
        + frame.camRight.xyz * ((position.x - origin.x) / frame.focal.x)
        - frame.camUp.xyz * ((position.y - origin.y) / frame.focal.y);
    if (layers & 1u) != 0u && ray.y < -1e-5 {
        let distance = (frame.ground.x - frame.camPos.y) / ray.y;
        if distance > 0.0 {
            let hit = frame.camPos.xyz + ray * distance;
            let width = frame.sizes.x & 0xFFFFu;
            let height = frame.sizes.x >> 16u;
            let texel = vec2i(floor(hit.xz * frame.ground.y));
            let x = u32(texel.x & i32(width - 1u));
            let y = u32(texel.y & i32(height - 1u));
            let index = vdp2Byte(frame.vdp2.x + y * width + x);
            let ground = rgb5(cramWord((frame.sizes.z & 0xFFFFu) + index));
            // Colour calculation towards the haze colour in 32 ratio steps.
            let fade = clamp((distance - frame.ground.z) / (frame.ground.w - frame.ground.z), 0.0, 1.0);
            let ratio = floor(fade * 31.0) / 31.0;
            color = floor(mix(ground, rgb5(frame.sizes.w), ratio));
            layer = kLayerRbg0;
        }
    }

    // NBG1, the water: inside window 0 it replaces the ground with a reflection of the sky.
    // The line scroll table picks, per scanline, which (mirrored) line to show and how far
    // to shift it sideways; the result is blended towards the water colour.
    if (frame.water.w & 0x100u) != 0u && insideWindow(pixel) {
        let scroll = frame.water.y + pixel.y * 4u;
        let row = signed16(vdp2Word(scroll));
        let shift = signed16(vdp2Word(scroll + 2u));
        let sourceRow = u32(clamp(row, 0.0, frame.screen.y - 1.0));
        var reflection = rgb5(vdp2Word(frame.vdp2.z + sourceRow * 2u));
        if (layers & 2u) != 0u {
            let nbg0 = nbg0Pixel(position.x + shift, row + 0.5);
            if nbg0.a > 0.0 {
                reflection = nbg0.rgb;
            }
        }
        let ratio = f32(frame.water.w & 31u) / 31.0;
        color = floor(mix(reflection, rgb5(frame.water.z), ratio));
        layer = kLayerNbg1;
    }

    return vec4f(colorOffset(color, layer) / 31.0, 1.0);
}

// --- VDP1 ----------------------------------------------------------------------

struct Vdp1Out {
    @builtin(position) position: vec4f,
    @location(0) @interpolate(flat) command: u32,
    @location(1) @interpolate(flat) ab: vec4f, // corners A and B in pixels
    @location(2) @interpolate(flat) cd: vec4f, // corners C and D in pixels
}

// Each command is drawn as two triangles; the fragment shader maps the whole
// quad bilinearly like VDP1, so the diagonal split never shows.
@vertex
fn vdp1Vertex(@builtin(vertex_index) vertexIndex: u32) -> Vdp1Out {
    var corners = array<u32, 6>(0u, 1u, 2u, 0u, 2u, 3u);
    let command = vertexIndex / 6u;
    let base = command * 32u;
    let ctrl = vdp1Word(base);
    let size = vdp1Word(base + 10u);
    let width = f32(((size >> 8u) & 0x3Fu) * 8u);
    let height = f32(size & 0xFFu);

    let a = vec2f(signed16(vdp1Word(base + 12u)), signed16(vdp1Word(base + 14u)));
    let b = vec2f(signed16(vdp1Word(base + 16u)), signed16(vdp1Word(base + 18u)));
    let c = vec2f(signed16(vdp1Word(base + 20u)), signed16(vdp1Word(base + 22u)));
    let d = vec2f(signed16(vdp1Word(base + 24u)), signed16(vdp1Word(base + 26u)));

    var p = array<vec2f, 4>(a, b, c, d);
    switch ctrl & 0x800Fu {
        case 0u: { // normal sprite: A plus the texture size
            p = array<vec2f, 4>(a, a + vec2f(width, 0.0), a + vec2f(width, height), a + vec2f(0.0, height));
        }
        case 1u: { // scaled sprite: A and the inclusive opposite corner C
            let opposite = c + 1.0;
            p = array<vec2f, 4>(a, vec2f(opposite.x, a.y), opposite, vec2f(a.x, opposite.y));
        }
        case 2u, 4u: { // distorted sprite, polygon
        }
        default: { // anything else is not drawn
            p = array<vec2f, 4>(vec2f(0.0), vec2f(0.0), vec2f(0.0), vec2f(0.0));
        }
    }
    for (var i = 0u; i < 4u; i++) {
        p[i] += frame.screen.zw;
    }

    let pixel = p[corners[vertexIndex % 6u]];
    var out: Vdp1Out;
    out.position = vec4f(pixel.x / frame.screen.x * 2.0 - 1.0, 1.0 - pixel.y / frame.screen.y * 2.0, 0.0, 1.0);
    out.command = command;
    out.ab = vec4f(p[0], p[1]);
    out.cd = vec4f(p[2], p[3]);
    return out;
}

fn cross2(a: vec2f, b: vec2f) -> f32 {
    return a.x * b.y - a.y * b.x;
}

fn solveU(h: vec2f, e: vec2f, f: vec2f, g: vec2f, v: f32) -> f32 {
    let denominator = e + g * v;
    let numerator = h - f * v;
    if abs(denominator.x) > abs(denominator.y) {
        return numerator.x / denominator.x;
    }
    if abs(denominator.y) > 1e-6 {
        return numerator.y / denominator.y;
    }
    return 0.0;
}

fn inside(uv: vec2f) -> bool {
    return all(uv >= vec2f(-0.02)) && all(uv <= vec2f(1.02));
}

/// Inverse bilinear mapping: finds (u, v) with p = mix(mix(a, b, u), mix(d, c, u), v).
fn invBilinear(p: vec2f, a: vec2f, b: vec2f, c: vec2f, d: vec2f) -> vec2f {
    let e = b - a;
    let f = d - a;
    let g = a - b + c - d;
    let h = p - a;
    let k2 = cross2(g, f);
    let k1 = cross2(e, f) + cross2(h, g);
    let k0 = cross2(h, e);

    // Numerically stable quadratic k2 v^2 + k1 v + k0 = 0 (also covers k2 == 0).
    let root = sqrt(max(k1 * k1 - 4.0 * k0 * k2, 0.0));
    let q = -0.5 * (k1 + select(-root, root, k1 >= 0.0));
    var best = vec2f(0.0);
    if abs(q) > 1e-9 {
        let v = k0 / q;
        best = vec2f(solveU(h, e, f, g, v), v);
        if inside(best) {
            return clamp(best, vec2f(0.0), vec2f(1.0));
        }
    }
    if abs(k2) > 1e-9 {
        let v = q / k2;
        let candidate = vec2f(solveU(h, e, f, g, v), v);
        if inside(candidate) {
            return clamp(candidate, vec2f(0.0), vec2f(1.0));
        }
    }
    return clamp(best, vec2f(0.0), vec2f(1.0));
}

@fragment
fn vdp1Fragment(in: Vdp1Out) -> @location(0) vec4f {
    let base = in.command * 32u;
    let ctrl = vdp1Word(base);
    let pmod = vdp1Word(base + 4u);
    let colr = vdp1Word(base + 6u);
    let pixel = vec2u(in.position.xy);

    // User clipping: only draw inside the clip region (here the VDP2 line window).
    if (pmod & 0x400u) != 0u && !insideWindow(pixel) {
        discard;
    }

    // Mesh: VDP1's checkerboard "screen door" transparency.
    if (pmod & 0x100u) != 0u && ((pixel.x ^ pixel.y) & 1u) != 0u {
        discard;
    }

    var uv = invBilinear(in.position.xy, in.ab.xy, in.ab.zw, in.cd.xy, in.cd.zw);
    var color: vec3f;
    var isRgb: bool; // gouraud shading only works on direct RGB colours, not colour bank codes

    if (ctrl & 0xFu) == 4u {
        color = resolveColor(colr);
        isRgb = (colr & 0x8000u) != 0u;
    } else {
        if (ctrl & 0x10u) != 0u {
            uv.x = 1.0 - uv.x;
        }
        if (ctrl & 0x20u) != 0u {
            uv.y = 1.0 - uv.y;
        }
        let size = vdp1Word(base + 10u);
        let width = ((size >> 8u) & 0x3Fu) * 8u;
        let height = size & 0xFFu;
        let texel = min(vec2u(uv * vec2f(f32(width), f32(height))), vec2u(width - 1u, height - 1u));
        let index = texel.y * width + texel.x;
        let address = vdp1Word(base + 8u) * 8u;
        let mode = (pmod >> 3u) & 7u;
        let drawZero = (pmod & 0x40u) != 0u; // SPD: draw colour 0 instead of treating it as transparent

        var code = 0u;
        if mode <= 1u {
            let packed = vdp1Byte(address + index / 2u);
            let nibble = select(packed >> 4u, packed & 0xFu, (index & 1u) == 1u);
            if nibble == 0u && !drawZero {
                discard;
            }
            if mode == 0u {
                code = (colr & 0xFFF0u) | nibble;
            } else {
                code = vdp1Word(colr * 8u + nibble * 2u);
            }
        } else if mode <= 4u {
            let value = vdp1Byte(address + index);
            if value == 0u && !drawZero {
                discard;
            }
            let mask = select(select(0xFFu, 0x7Fu, mode == 3u), 0x3Fu, mode == 2u);
            code = (colr & ~mask) | (value & mask);
        } else {
            code = vdp1Word(address + index * 2u);
            if code == 0u && !drawZero {
                discard;
            }
        }
        color = resolveColor(code);
        isRgb = (code & 0x8000u) != 0u;
    }

    // Gouraud shading: per-corner RGB offsets interpolated across the quad, in 5-bit steps.
    if (pmod & 0x4u) != 0u && isRgb {
        let table = vdp1Word(base + 28u) * 8u;
        let ga = rgb5(vdp1Word(table));
        let gb = rgb5(vdp1Word(table + 2u));
        let gc = rgb5(vdp1Word(table + 4u));
        let gd = rgb5(vdp1Word(table + 6u));
        let gouraud = mix(mix(ga, gb, uv.x), mix(gd, gc, uv.x), uv.y);
        color = clamp(color + floor(gouraud) - 16.0, vec3f(0.0), vec3f(31.0));
    }

    switch pmod & 3u {
        case 1u: { // shadow: halve what is already in the framebuffer
            return vec4f(0.0, 0.0, 0.0, 0.5);
        }
        case 2u: { // half luminance
            return vec4f(colorOffset(floor(color * 0.5), kLayerSprite) / 31.0, 1.0);
        }
        case 3u: { // half transparency
            return vec4f(colorOffset(color, kLayerSprite) / 31.0, 0.5);
        }
        default: {
            return vec4f(colorOffset(color, kLayerSprite) / 31.0, 1.0);
        }
    }
}
