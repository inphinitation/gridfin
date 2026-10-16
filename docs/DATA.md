# Game data

Gridfin's executable is an engine that plays a game from its data. The data is a tree of small files, mostly text,
read through a virtual file system (`src/data/Vfs.*`). It can come from two kinds of source:

- a **source folder**, the form you edit (and keep in git). The base game is `data/base/`;
- a **`.pak` archive**, the form you ship: one file holding the same tree.

## Mounting

Sources are mounted in order, and a file in a later source replaces the same path in earlier ones, so a mod can
ship only the files it changes (like a Doom PWAD over the IWAD):

```sh
gridfin --data base.pak --data mymod/          # mymod/sound/music/fields.mml replaces the base song
```

Without `--data`, the game looks for, in order:

1. the source folder `data/base/` in the source tree (dev builds only, so edits show up live);
2. `base.pak` next to the executable (the build packs it from `data/base/`);
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

## The `.pak` format

`gridfin --pack <folder> <file.pak>` packs a source folder. A `.pak` stores the folder tree itself: every folder's
entries are contiguous and sorted by name, so finding `sound/tones/bass.tone` means a binary search among the root's
entries for `sound`, then among `sound`'s for `tones`, and so on. Lookups run straight on the file's bytes; nothing is
indexed at load time. All integers are little-endian:

```
header    32 bytes
          "GPAK", u32 version (1), u32 node count, u32 node table offset,
          u32 name table offset, u32 name table size, u32 data offset, u32 reserved (0)
nodes     16 bytes each, node 0 is the root folder
          u32 name offset, u16 name length, u16 flags (bit 0: folder),
          folder: u32 first child node, u32 child count
          file:   u32 data offset, u32 size
CRC-32s   u32 per node, right after the node table (0 for folders)
names     UTF-8, not terminated (the root's is empty)
data      the files, each aligned to 4 bytes
```

Nodes are numbered breadth first, so a folder's children always come after it. A `.pak` is checked when it is
opened: every name must be a valid path part, every folder's entries sorted and unique, every node but the root in
exactly one folder, and every file's CRC-32 must match.
