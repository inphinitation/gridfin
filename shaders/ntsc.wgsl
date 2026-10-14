// NTSC composite video: encodes each scanline of the 320x224 framebuffer into a
// composite signal and decodes it again like a TV would.
//
// Luma and chroma share one signal: chroma rides on a 3.58 MHz subcarrier that
// runs at ~0.53 cycles per Saturn dot. Decoding separates them with filters, so
// colour is blurred over several dots, sharp luma edges leak into chroma
// (rainbow fringes) and leftover subcarrier shows up in luma (dot crawl). It is
// also why mesh transparency's checkerboard blends into real transparency on a
// composite TV.
//
// The output has 4 samples per source dot so the present pass can use the extra
// horizontal detail.

struct Ntsc {
    params: vec4f, // x = subcarrier phase offset of this frame (cycles), yzw unused
}

@group(0) @binding(0) var screenTexture: texture_2d<f32>;
@group(0) @binding(1) var<uniform> ntsc: Ntsc;

const kSamplesPerDot = 4.0;
// 3.579545 MHz colour subcarrier / 6.7116 MHz dot clock of the 320 pixel mode.
const kCyclesPerDot = 0.5333;
// Filter window in samples on each side (3.5 dots, where the chroma filter has faded out).
const kTaps = 14;
// Gaussian widths in dots: luma keeps ~4 MHz of detail, chroma only ~1.3 MHz.
const kLumaSigma = 0.75;
const kChromaSigma = 1.6;
const kTau = 6.2831853;

@vertex
fn ntscVertex(@builtin(vertex_index) index: u32) -> @builtin(position) vec4f {
    let uv = vec2f(f32((index << 1u) & 2u), f32(index & 2u));
    return vec4f(uv * 2.0 - 1.0, 0.0, 1.0);
}

/// The video DAC holds each dot's colour for the whole dot.
fn dacOutput(x: f32, line: i32) -> vec3f {
    let width = i32(textureDimensions(screenTexture).x);
    let px = i32(floor(x));
    if px < 0 || px >= width {
        return vec3f(0.0); // blanking
    }
    let color = textureLoad(screenTexture, vec2i(px, line), 0).rgb;
    return floor(color * 31.0 + 0.5) / 31.0; // the framebuffer holds RGB555
}

fn rgbToYiq(c: vec3f) -> vec3f {
    return vec3f(
        dot(c, vec3f(0.299, 0.587, 0.114)),
        dot(c, vec3f(0.596, -0.274, -0.322)),
        dot(c, vec3f(0.211, -0.523, 0.312)));
}

fn yiqToRgb(c: vec3f) -> vec3f {
    return vec3f(
        c.x + 0.956 * c.y + 0.621 * c.z,
        c.x - 0.272 * c.y - 0.647 * c.z,
        c.x - 1.106 * c.y + 1.703 * c.z);
}

@fragment
fn ntscFragment(@builtin(position) position: vec4f) -> @location(0) vec4f {
    let line = i32(position.y);
    let t = position.x / kSamplesPerDot;
    // 227.5 subcarrier cycles per line: the phase flips every line (and every frame).
    let linePhase = f32(line) * 0.5 + ntsc.params.x;

    var luma = 0.0;
    var lumaWeight = 0.0;
    var chroma = vec2f(0.0);
    var chromaWeight = 0.0;
    // The subcarrier advances by a fixed angle per sample: rotate instead of calling sin/cos per tap.
    let step = kTau * kCyclesPerDot / kSamplesPerDot;
    let rotation = vec2f(cos(step), sin(step));
    let startPhase = kTau * ((t - f32(kTaps) / kSamplesPerDot) * kCyclesPerDot + linePhase);
    var carrier = vec2f(cos(startPhase), sin(startPhase));
    for (var k = -kTaps; k <= kTaps; k++) {
        let offset = f32(k) / kSamplesPerDot;
        let yiq = rgbToYiq(dacOutput(t + offset, line));
        let signal = yiq.x + yiq.y * carrier.x + yiq.z * carrier.y;

        // Decoder: low-pass for luma, demodulate and low-pass for chroma.
        let wl = exp(-offset * offset / (2.0 * kLumaSigma * kLumaSigma));
        let wc = exp(-offset * offset / (2.0 * kChromaSigma * kChromaSigma));
        luma += signal * wl;
        lumaWeight += wl;
        chroma += signal * 2.0 * carrier * wc;
        chromaWeight += wc;

        carrier = vec2f(carrier.x * rotation.x - carrier.y * rotation.y,
                        carrier.x * rotation.y + carrier.y * rotation.x);
    }

    let rgb = yiqToRgb(vec3f(luma / lumaWeight, chroma / chromaWeight));
    return vec4f(clamp(rgb, vec3f(0.0), vec3f(1.0)), 1.0);
}
