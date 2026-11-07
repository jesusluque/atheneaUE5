# athenea for Unreal: development

Copyright (c) 2026 jesus luque.

How the plugin's code is laid out, what upstream does and what this fork
adds, the conventions every importer and shader keeps, how to add a format,
and how a change is checked. Using the plugin is in `operations.md`; why each
change was made, and what it measured, is in `decisions.md`.

## 1. Repository

- A fork of StarLightGS (github.com/CoSTAR-National-Lab/starlightgs), from
  upstream's 8720289; upstream's history is kept below this fork's work.
- Lines changed in upstream's files are marked with an `athenea:` comment, so
  a merge from upstream shows what is ours.
- Files written here start with `// Copyright (c) 2026 jesus luque.` (or the
  same in the file's comment syntax).
- Documentation lives in `Docs/athenea/`: `DESIGN.md`, `operations.md`,
  `development.md` and `decisions.md`, each with a `.es.md` translation
  changed in the same commit. A translation keeps the same headings and
  tables.

## 2. Modules

| Module | Type | Holds |
|---|---|---|
| `GaussiansRenderer` | Runtime, loaded at PostConfigInit (global shaders need it) | the splat data (`FGaussianSplatData`), its GPU resource (`FGaussianSplatResource`), the view extension that draws every frame, the passes (culling, prepass, radix sort, raster, composition) |
| `Gaussians` | Runtime | the asset (`UGSplatAsset`), its import data, the component and actor, the scene proxy and vertex factory used for shadows, clipping volumes |
| `GaussiansEditor` | Editor | the factory, the importers (PLY, USD, `.athc`), the exporter, the import and export dialogues |

Shaders are in `Shaders/`, mapped to `/Plugin/StarLightGS/`.

## 3. A frame

1. **Culling** (`GSplatCulling.usf`, only when something changed): clipping
   volumes against the splats, into an index list a primitive.
2. **Prepass** (`GSCompute.usf`, `GSPrepass`): a thread a splat. Frustum and
   HZB culling, the EWA projection, the colour (`GSColor.ush`), and an append
   into one buffer shared by every visible primitive (`r.Gaussians.MaxSplats`,
   5 M by default).
3. **Sort** (`RadixSort.usf`): one global radix sort a view on the view-space
   depth.
4. **Raster** (`GSSplatting_VS/PS`): one indirect instanced draw of quads.
   The unlit path blends "over" into SceneColor before post-processing; the
   relit path accumulates front to back into four RGBA16F targets that a
   composition pass writes into UE's GBuffer.
5. **Shadows**: the vertex factory (`GSVertexFactory.ush`) draws the splats
   into virtual shadow maps, opaque above 0.9.

## 4. What this fork adds

| Piece | Where | What it does |
|---|---|---|
| Harmonics | `GSColor.ush`, `GSPrepass` | degree 1 to 3 evaluated along the view in the file's frame; clamp; sRGB curve when `bLinearizeColor` |
| File axes | `FGaussianSplatData::FileAxisX/Y/Z`, `GSResources` | where the file's axes land in local space, so the view direction is taken back into the frame the harmonics were fitted in |
| PLY comment | `GSplatImporter.cpp`, `ParsePly` | athenea's `flatten` comment sets swizzle, scale and colour space |
| USD reader | `GSplatUsdImporter.cpp` | a stage's first `ParticleField3DGaussianSplat` through USDCore's OpenUSD |
| `.athc` reader | `GSplatAthcImporter.cpp` | a `.athc`'s finest level, copied as athenea packs it |
| GPU decoder | `GSDecode.usf`, `FGSDecodeAtheneaCS` in `GaussianSplatResource.cpp` | athenea's packing into the float buffers the passes read, once a resource |
| Fixes | see `decisions.md` | 5.8 build, out-of-bounds append, pre-exposure, shadow bound, bool culling, vertex factory flags, plugin content, bounds initialisation |
| GPU stat | `GSViewExtension.cpp` | `GPU/GaussiansUnlit`, beside upstream's `GPU/GaussiansLit` |

## 5. Conventions

### 5.1 Axes, units and rotations

- UE is left-handed and z up, in centimetres. Every importer maps a file's
  axes with a swizzle (`EGSplatSwizzleAxis`, three entries), the same
  `SwizzleVector`, `SwizzleVectorAbs` and `SwizzleQuaternion` the PLY
  importer uses (declared in `GSplatImporter.h`).
- A right-handed y-up file (athenea's, USD's default) maps to (X, Z, Y); a
  right-handed z-up stage to (X, −Y, Z). Each is a mirror, which a change of
  handedness needs.
- Positions are scaled to centimetres. **Scales stay in metres**: the prepass
  and the vertex factory multiply them by 100 themselves (upstream's
  convention), so an importer scales them by its scale factor over 100.
- Rotations follow `SwizzleQuaternion`: the vector part goes through the
  swizzle, and `w` is negated when the swizzle keeps handedness. That is the
  convention the rasteriser was written against; `GSDecode.usf` repeats it.
- `FileAxisX/Y/Z` are the swizzle applied to the unit axes, recorded in the
  asset so the shaders can take a local direction back into the file's frame.

### 5.2 Colour and harmonics

- `ZeroOrderHarmonicsCoefficientsAndOpacities.rgb` holds `0.5 + C0 · dc`
  (3DGS's convention, and athenea's packed base colour), unclamped.
- `HighOrderHarmonicsCoefficients` holds 45 floats a splat in the PLY's
  `f_rest` order: red's coefficients 1 to 15, then green's, then blue's.
  Missing bands are zero.
- The colour is summed in the space the file trained it in, clamped at zero,
  and only then linearised (the sRGB curve, as athenea's `cloudLight`). No
  importer clamps or applies a gamma on the CPU.
- `GSResources.ColorFlags`: bit 0 the harmonics are present and used, bit 1
  linearise.

### 5.3 athenea's packing

A packed asset (`bPacked`) holds athenea's device layout
(`shaders/athenea/common/packing.slang` in athenea), copied from the file:

| Array | Per splat | Content |
|---|---|---|
| `PackedPositions` | float4 | xyz in the file's units, linear opacity |
| `PackedShape` | 4 uint | [0] rotation, smallest three (10 + 10 + 10 bits, largest index in bits 0–1); [1] f16 log scale x, y; [2] f16 log scale z, base colour r; [3] base colour g, b |
| `PackedSH` | `PackedSHWords` uint | f16 rest coefficients, rgb per basis, two halves a word; `PackedRestPerColour` of them a colour |

`FGSDecodeAtheneaCS` writes `Positions`, `Scales`, `Orientations`,
`SHAndOpacities` and `HighOrderSH` from them in `InitRHI`, in dispatches of
at most 65535 groups of 64. The float arrays of a packed asset stay empty on
the CPU.

### 5.4 The CPU's part

As in athenea, the CPU reads files, parses headers and copies bytes; decoding
is a kernel. The PLY and USD importers still convert on the CPU, as upstream
did (log scales, the sigmoid, swizzles); the `.athc` path is the model the
others are to follow.

## 6. Adding a format

1. Write `GSplat<Format>Importer.cpp` in `GaussiansEditor/Private` with
   `Is<Format>File` and `Import<Format>`, declared in `GSplatImporter.h`.
2. Fill `UGSplatImportData` (swizzle, scale, gamma, what the file has) and
   `FGaussianSplatData`, either as float arrays or packed. A format athenea
   already decodes on its GPU is best copied packed and given a decoder in
   `GSDecode.usf`.
3. Record `FileAxisX/Y/Z`, `bLinearizeColor` and the bounds (`RecalculateBounds`
   for float arrays, `SetBounds` from the file for packed ones).
4. Add the extension to the factory's `Formats`, to `FactoryCanImport`, and to
   the branch in `ImportInternal`.
5. Log one line on success and a reason on every refusal (operations §4.5).
6. Check it as in §7, and document it in all three manuals, both languages.

## 7. How a change is checked

There are no automated tests in the plugin yet. A change is checked on a
Windows machine against athenea:

1. **Build**: `RunUAT BuildPlugin` (operations §2), with no new warning.
2. **Import**: a commandlet (`UnrealEditor-Cmd ... -run=pythonscript
   -script=setup_*.py -nullrhi`) imports the test file and saves a level;
   its log line gives the count and what was read.
3. **Shots**: `shot_level.py` renders the level from the saved cameras
   (`shots/cameras.json`: front, side and high views of the pawn).
4. **Reference**: athenea's raster renders the same file from the same
   cameras (`athenea render --technique raster`, eye and target taken from
   UE's centimetres back to the file's metres with the axes swapped, focal
   45.861 mm for UE's 30°).
5. **Comparison**: `oiiotool --diff` between formats, and side by side against
   athenea.
6. **Timing**: `-game` with the CSV profiler (operations §7.2), on a free GPU,
   reported against athenea's raster on the same frame.

The scripts are in `Tools/athenea/` (§8). Test files: a chess pawn
(`pawn_3dgs.ply`, 325 833 gaussians, degree 3, athenea-flattened), its USD
export (`pawn.usdc`) and its `.athc` (`pawn.athc`); they are not distributed
with the plugin.

## 8. Scripts

`Tools/athenea/` holds what §7 runs, as they were used. They expect a project
at `~/athenea-unreal/TestProj` with the plugin in its `Plugins` folder, test
files in `~/athenea-unreal/`, and write shots to `~/athenea-unreal/shots/`.

| File | Run with | Does |
|---|---|---|
| `setup.py` | `UnrealEditor-Cmd <project> -run=pythonscript -script=setup.py -nullrhi` | imports `pawn_3dgs.ply` and saves `/Game/Test` with an actor of it |
| `setup_usd.py`, `setup_athc.py` | the same | import `pawn.usdc` or `pawn.athc` into `/Game/TestUsd` or `/Game/TestAthc` |
| `shot.py` | `UnrealEditor <project> -RenderOffscreen -ExecCmds="py shot.py"` | frames the PLY level's splats from three views, writes `cameras.json` and the shots |
| `shot_level.py` | `-ExecCmds="py shot_level.py /Game/Level name"` | shots of a level from the saved `cameras.json` |
| `gamecam.py` | as `setup.py` | a camera the player takes on start at the front view, for `-game` timing |
| `ue_time.sh` | `ue_time.sh /Game/Level out.csv`, from a machine with `ssh` to the Windows one | times a level in `-game` with the CSV profiler (30 fps cap) and prints the splat stages' medians |
| `TestProj.uproject`, `DefaultEngine.ini`, `DefaultEditorPerProjectUserSettings.ini` | -- | the test project: SM6 only, auto-exposure, bloom, motion blur and temporal AA off; the MCP server's settings (operations §8) |
