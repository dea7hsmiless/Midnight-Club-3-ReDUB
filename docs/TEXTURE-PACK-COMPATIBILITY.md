# Texture-pack compatibility

## Finding

The local folder named `ULUS10021` is a **PSP** *Midnight Club 3: DUB Edition* pack. Its `textures.ini` and 24-character quick hashes are PPSSPP format. It is not a PS2/PCSX2 pack, and it targets the non-Remix game. ReDUB runs the original Xbox release of *DUB Edition Remix*. The pack cannot be installed directly, and its identifiers cannot be translated by renaming files.

ReDUB's current OpenGL texture replacement loader looks for PNGs named by a 16-digit FNV-1a hash of the Xbox texture's bytes, format, dimensions, and palette under `hd/textures/`. The source graphics may overlap between editions, but the underlying assets, crop, palette, and hashes are not guaranteed to match.

## What reuse would require

1. Dump the Xbox Remix textures from the player's own game files with the existing `RECOMP_GL_TEX_DUMP=1` option.
2. Visually compare the PSP replacement images with those Xbox dumps and identify only clear matches.
3. Preserve selected replacements as PNG and create a mapping to the corresponding Xbox hash filenames.
4. Test transparency, mipmaps, and in-game use; keep the pack outside the source repository unless redistribution rights are clear.

This is a manual asset-matching task, not an automatic hash conversion. A partial pack could be useful for shared art such as logos or some vehicles, but compatibility and visual correctness must be confirmed per texture. The PSP pack itself is not redistributed. A separate, small ReDUB logo replacement set is included under `hd/textures`; it contains the maintainer-supplied custom logo art only, not PSP, Xbox, or other original game textures.
