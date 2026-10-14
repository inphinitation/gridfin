// Scales the 320x224 Saturn framebuffer to the window with nearest-neighbour
// sampling and outputs exactly 32768 colours (RGB555).

struct Present {
    viewport: vec4f, // x, y, width, height of the picture in window pixels
    params: vec4f,   // scanline strength, 1 if the surface is sRGB, unused, unused
}

@group(0) @binding(0) var screenTexture: texture_2d<f32>;
@group(0) @binding(1) var<uniform> present: Present;

@vertex
fn presentVertex(@builtin(vertex_index) index: u32) -> @builtin(position) vec4f {
    let uv = vec2f(f32((index << 1u) & 2u), f32(index & 2u));
    return vec4f(uv * 2.0 - 1.0, 0.0, 1.0);
}

@fragment
fn presentFragment(@builtin(position) position: vec4f) -> @location(0) vec4f {
    let local = (position.xy - present.viewport.xy) / present.viewport.zw;
    if any(local < vec2f(0.0)) || any(local >= vec2f(1.0)) {
        return vec4f(0.0, 0.0, 0.0, 1.0);
    }

    let size = vec2f(textureDimensions(screenTexture));
    let texel = vec2i(local * size);
    var color = textureLoad(screenTexture, texel, 0).rgb;
    color = floor(color * 31.0 + 0.5) / 31.0;

    // Optional CRT-ish scanlines, darkening the lower part of every source line.
    let line = fract(local.y * size.y);
    color *= 1.0 - present.params.x * smoothstep(0.45, 0.95, line);

    if present.params.y > 0.5 {
        // The surface will encode to sRGB; pre-decode so the output matches.
        color = pow(color, vec3f(2.2));
    }
    return vec4f(color, 1.0);
}
