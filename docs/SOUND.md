# Sound data

Music and sound are written as text: instruments in **tone files**, songs in **MML**. Both are loaded into the SCSP
model (see the sound section of [SATURN.md](SATURN.md)), so every sample costs
sound RAM and every note costs one of the 32 slots (two for FM). Edit them in a source folder and they reload while
the game runs.

## Tone files (`.tone`)

Each file under `music.tones` (`sound/tones/`) defines one instrument, chosen in MML with `@<program>`.

```ini
; FM electric piano
program = 1
wave = 1                ; carrier: a sine
decay1 = 9
release = 18
level = 30
fm-depth = 10
reverb = 5

[modulator]             ; a second slot that frequency-modulates the first
wave = 1
decay1 = 15
decay-level = 12
decay2 = 9
```

### Instrument keys

| Key | Range | Meaning |
| --- | --- | --- |
| `program` | 0..255 | The number MML selects with `@` (required, unique) |
| `fixed-pitch` | true/false | Ignore the note and always play the sample at its root (drums) |
| `pan` | -15..15 | Negative is left, positive right, in 3 dB steps (an MML `p` overrides it) |
| `reverb` | 0..7 | Send level to the DSP reverb (EFSDL); 0 is none |
| `lfo-rate` | 0..31 | LFO frequency, about 0.17 Hz to 172 Hz |
| `vibrato` | 0..7 | Pitch LFO depth |
| `tremolo` | 0..7 | Amplitude LFO depth |
| `fm-depth` | 0..15 | Modulation level (MDL) when there is a `[modulator]`; 5 and up modulate, default 9 |

### Slot keys (top level for the carrier, under `[modulator]` for the modulator)

The sound, exactly one of:

| Key | Meaning |
| --- | --- |
| `wave = 1 0.5 0.33 ...` | A single-cycle waveform built from harmonic amplitudes (1 to 64 of them), looping |
| `wave-length` | Samples in the cycle, 16..1024 (default 64; more harmonics need more) |
| `sample = path.wav` | An imported WAVE file, relative to the tone file |

For imported samples:

| Key | Meaning |
| --- | --- |
| `format` | `pcm8` or `pcm16` in sound RAM (default: 8-bit files stay 8-bit, others become 16-bit) |
| `downsample` | 1..8: keep every n-th sample (averaged), to save sound RAM |
| `loop` | true/false; defaults to true when the file has a loop in its `smpl` chunk |
| `loop-start`, `loop-end` | Loop points in samples of the original file (default: the `smpl` loop, or the whole sample) |
| `root` | The note the sample sounds at when played at its own rate: `c4`, `a#3`, or a MIDI number (default: the file's `smpl` root note, else `c4`) |

WAVE files may be 8, 16, 24 or 32-bit integer PCM or 32-bit float, at any sample rate, with any number of channels
(mixed down to mono). They play at their own rate: the SCSP's pitch registers do the conversion. A sample may be at
most 65535 samples long after downsampling, the limit of the SCSP's 16-bit loop registers. Several tones using the
same sample (or the same `wave`) share one copy in sound RAM.

Envelope and level:

| Key | Range | Meaning |
| --- | --- | --- |
| `attack` | 0..31 | Attack rate; 31 is instant (default) |
| `decay1` | 0..31 | Rate from full level down to `decay-level` (0 = hold) |
| `decay-level` | 0..31 | Where decay 1 ends, in steps of 3 dB |
| `decay2` | 0..31 | Rate after `decay-level`, while the key is held (0 = sustain) |
| `release` | 0..31 | Rate after key off (default 20) |
| `level` | 0..255 | Total level: attenuation in 0.375 dB steps |
| `frequency-ratio` | 1/64..64 | Multiplies the note's frequency (for FM modulators: 2 is an octave up) |

## Songs (`.mml`)

A song file holds several tracks, each starting with a `#track <name>` line. Before the first track only comments
(`;`) and `#title <text>` may appear. A file without `#track` lines is a single track.

```
#title Gridfin Fields
#track lead
t104 @1 v13 q7 o4 l4 r1 r1 r1 r1 L
e4. a8 b4 >c4< | a4 g8 f8 e4 c4
#track bass
@0 v14 q6 o2 l8 [a>a<]4 [f>f<]4
```

| Command | Meaning |
| --- | --- |
| `c d e f g a b` | A note, with `+` or `#` (sharp) or `-` (flat), then an optional length and dots: `c4.`, `f+8` |
| `r` | A rest, with an optional length |
| Lengths | 1, 2, 4, 8, 16, 32, 64, and 3, 6, 12, 24, 48 for triplets (48 ticks per quarter note) |
| `o<n>` | Octave 0..8; `o4 c` is middle C |
| `<` `>` | Octave down / up |
| `l<n>` | Default length |
| `v<n>` | Volume 0..15, 3 dB per step |
| `@<n>` | Tone (program number) |
| `q<n>` | Gate: the note sounds for n/8 of its length (8 = full) |
| `t<n>` | Tempo in quarter notes per minute, 20..300 |
| `p<n>` | Pan -15..15 (0 uses the tone's pan) |
| `[ ... ]<n>` | Repeat n times (default 2); repeats nest |
| `L` | Loop point: the track restarts here when it ends |
| `;` | Comment to the end of the line; spaces and `|` are ignored |

Each track plays one note at a time. Errors are reported as `file:line:column`.
