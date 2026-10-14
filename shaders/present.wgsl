// Scales the Saturn picture to the window.
//
// The input is either the 320x224 framebuffer (RGB555, one texel per dot) or the
// NTSC composite pass output (4 samples per dot). Two looks:
//
//  - pixels: sharp nearest-neighbour dots, optionally darkened between lines;
//  - CRT: an electron beam model. Each scanline is a Gaussian beam whose width
//    grows with brightness, dots are reconstructed with a Gaussian spot so they
//    melt into their neighbours, a faint halation glow surrounds bright areas and
//    an aperture grille splits the light into phosphor stripes. This softening is
//    what makes low resolution art look smooth and detailed on a real CRT.

struct Present {
    viewport: vec4f, // x, y, width, height of the picture in window pixels
    params: vec4f,   // scanline strength, 1 if the surface is sRGB, mode (0 pixels, 1 CRT), samples per dot
    crt: vec4f,      // mask strength, glow strength, unused, unused
}

@group(0) @binding(0) var image: texture_2d<f32>;
@group(0) @binding(1) var<uniform> present: Present;

const kSourceSize = vec2f(320.0, 224.0);

@vertex
fn presentVertex(@builtin(vertex_index) index: u32) -> @builtin(position) vec4f {
    let uv = vec2f(f32((index << 1u) & 2u), f32(index & 2u));
    return vec4f(uv * 2.0 - 1.0, 0.0, 1.0);
}

/// Colour at horizontal position `x` (in dots) on a scanline.
fn fetch(x: f32, line: i32) -> vec3f {
    let size = vec2i(textureDimensions(image));
    let row = clamp(line, 0, size.y - 1);
    let samplesPerDot = present.params.w;
    if samplesPerDot <= 1.0 {
        // The framebuffer: one RGB555 colour per dot.
        let px = clamp(i32(floor(x)), 0, size.x - 1);
        return floor(textureLoad(image, vec2i(px, row), 0).rgb * 31.0 + 0.5) / 31.0;
    }
    // The composite signal is continuous: interpolate between its samples.
    let s = x * samplesPerDot - 0.5;
    let s0 = i32(floor(s));
    let a = textureLoad(image, vec2i(clamp(s0, 0, size.x - 1), row), 0).rgb;
    let b = textureLoad(image, vec2i(clamp(s0 + 1, 0, size.x - 1), row), 0).rgb;
    return mix(a, b, s - floor(s));
}

fn pixels(source: vec2f) -> vec3f {
    var color = fetch(source.x, i32(floor(source.y)));
    // Darken the lower part of every source line.
    color *= 1.0 - present.params.x * smoothstep(0.45, 0.95, fract(source.y));
    return color;
}

fn toLinear(c: vec3f) -> vec3f {
    return pow(c, vec3f(2.4));
}

fn crt(source: vec2f, screenX: f32) -> vec3f {
    let line0 = i32(floor(source.y));
    let x0 = floor(source.x);
    var beam = vec3f(0.0);
    var glow = vec3f(0.0);
    var glowWeight = 0.0;

    // Lines further away than this contribute nothing visible to the beam or the glow.
    let below = select(0, 1, fract(source.y) > 0.5);
    for (var dy = below - 2; dy <= below + 1; dy++) {
        let line = line0 + dy;
        if line < 0 || line >= i32(kSourceSize.y) {
            continue; // nothing is scanned above or below the picture
        }
        // The beam spot is about a dot wide: neighbouring dots blend into each other.
        var color = vec3f(0.0);
        var weight = 0.0;
        for (var dx = -1; dx <= 1; dx++) {
            let center = x0 + f32(dx) + 0.5;
            let d = source.x - center;
            let w = exp(-d * d / (2.0 * 0.5 * 0.5));
            color += toLinear(fetch(center, line)) * w;
            weight += w;
        }
        color /= weight;

        // Vertical beam profile: brighter lines are wider and bloom into the gap.
        let d = source.y - (f32(line) + 0.5);
        let luminance = dot(color, vec3f(0.2126, 0.7152, 0.0722));
        let sigma = mix(0.27, 0.46, sqrt(luminance));
        beam += color * exp(-d * d / (2.0 * sigma * sigma)) / (sigma * 2.5066283);

        // Halation: light scattering in the glass around bright areas.
        let g = exp(-d * d / (2.0 * 1.5 * 1.5));
        glow += color * g;
        glowWeight += g;
    }

    var color = beam + present.crt.y * glow / max(glowWeight, 1e-4);

    // Aperture grille: vertical red, green and blue phosphor stripes.
    let strength = present.crt.x;
    var mask = vec3f(1.0 - strength);
    mask[u32(screenX) % 3u] = 1.0;
    color *= mask * (3.0 / (3.0 - 2.0 * strength));

    return pow(clamp(color, vec3f(0.0), vec3f(1.0)), vec3f(1.0 / 2.2));
}

@fragment
fn presentFragment(@builtin(position) position: vec4f) -> @location(0) vec4f {
    let local = (position.xy - present.viewport.xy) / present.viewport.zw;
    if any(local < vec2f(0.0)) || any(local >= vec2f(1.0)) {
        return vec4f(0.0, 0.0, 0.0, 1.0);
    }

    let source = local * kSourceSize;
    var color: vec3f;
    if present.params.z > 0.5 {
        color = crt(source, position.x);
    } else {
        color = pixels(source);
    }

    if present.params.y > 0.5 {
        // The surface will encode to sRGB; pre-decode so the output matches.
        color = pow(color, vec3f(2.2));
    }
    return vec4f(color, 1.0);
}
