# Game data

Gridfin's executable is an engine that plays a game from its data. The data is a tree of small files, mostly text,
read through a virtual file system (`src/data/Vfs.*`). It can come from two kinds of source:

- a **source folder**, the form you edit (and keep in git). The base game is `data/base/`;
- a **packed archive** (`.gfa`), the form you ship: one file holding the same tree.

## Mounting

Sources are mounted in order, and a file in a later source replaces the same path in earlier ones, so a mod can
ship only the files it changes (like a Doom PWAD over the IWAD):

```sh
gridfin --data base.gfa --data mymod/          # mymod/sound/music/fields.mml replaces the base song
```

Without `--data`, the game looks for, in order:

1. the source folder `data/base/` in the source tree (dev builds only, so edits show up live);
2. `base.gfa` next to the executable (the build packs it from `data/base/`);
3. `data/base/` next to the executable, then in the working directory (the web build preloads this folder).

## Live reload

Source folders are checked for changes twice a second. Saving a tone, a song or `gridfin.cfg` reloads the sound
program; errors are logged and the rest keeps playing.

## Paths

Paths are relative, use `/`, and are case-sensitive. `.` and `..` parts, absolute paths and drive letters are
rejected, so data can't reach files outside its source. Inside a file, other files are referenced relative to it
(`../samples/kick.wav`), or relative to the root with a leading `/`. Files and folders whose names start with `.` are
ignored.

## Layout of the base game

```
gridfin.cfg               manifest: title, which song plays, reverb settings
sound/tones/*.tone        instruments (see SOUND.md)
sound/samples/*.wav       imported samples
sound/music/*.mml         songs in MML
```

## gridfin.cfg

The manifest uses the same `key = value` format as every definition file: `;` or `#` start a comment, `[section]`
prefixes the keys after it, values may be quoted. Unknown keys and out-of-range values are reported with their line.

| Key | Meaning |
| --- | --- |
| `title` | The game's name |
| `music.song` | The song that plays (`sound/music/fields.mml`) |
| `music.tones` | The folder whose `.tone` files are loaded (`sound/tones/`) |
| `reverb.bytes` | Sound RAM for the DSP's delay lines; 0 turns the reverb off (default 32768) |
| `reverb.room`, `reverb.damping` | Reverb feedback (0..0.98) and high-frequency damping (0..1) |
| `reverb.level` | Reverb return level, 0..7 |

## Archive format (`.gfa`)

`gridfin --pack <folder> <file.gfa>` packs a source folder. All integers are little-endian:

```
header     "GFA1", u32 version (1), u32 entry count, u32 directory offset
data       the files, each aligned to 4 bytes
directory  per entry, sorted by path: u16 path length, path (UTF-8), u32 offset, u32 size, u32 CRC-32
```

Archives are checked when they are opened: structure, paths and every file's CRC-32.
