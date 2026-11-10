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
| `GaussiansEditor` | Editor | the factory, the importers (PLY, USD, `.athc`, `.splat`, `.spz`, `.sog`, glTF), the exporter, the import and export dialogues |
| `AtheneaCodecs` | Editor | what the importers need to undo a file's compression: gzip (UE's zlib), zstd's decompressor and libwebp's decoder, compiled from their sources (no precompiled header, no unity build) |

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
   composition pass writes into UE's GBuffer. With
   `r.Gaussians.Athenea.FrontToBack` the unlit path draws front to back too
   (`GSFront.usf`): the sort ascending, the quads at their splat's depth
   (no `SV_Depth`, so the depth test comes before the shader), in batches
   into `Gaussians.Front` with a copy of the scene's depth; between batches
   `GSFrontSaturatePS` brings the depth of the opaque pixels to the near
   plane; `GSFrontCompositePS` puts the buffer over SceneColor and
   `GSFrontVelocityPS` makes the scene's velocity from the nearest depth.

The levels of detail's cut and its morph are `GSLod.ush`, included by the
culling pass (which gaussians) and the prepass (where they are drawn).
5. **Shadows**: the vertex factory (`GSVertexFactory.ush`) draws the splats
   into virtual shadow maps, opaque above 0.9.

## 4. What this fork adds

| Piece | Where | What it does |
|---|---|---|
| Harmonics | `GSColor.ush`, `GSPrepass` | degree 1 to 3 evaluated along the view in the file's frame; clamp; sRGB curve when `bLinearizeColor` |
| File axes | `FGaussianSplatData::FileAxisX/Y/Z`, `GSResources` | where the file's axes land in local space, so the view direction is taken back into the frame the harmonics were fitted in |
| PLY comment | `GSplatImporter.cpp`, `ParsePly` | athenea's `flatten` comment sets swizzle, scale and colour space |
| USD reader | `GSplatUsdImporter.cpp` | a stage's first `ParticleField3DGaussianSplat` through USDCore's OpenUSD |
| `.athc` reader | `GSplatAthcImporter.cpp` | a `.athc`'s finest level, copied as athenea packs it; a version 3's chunks or pages, their `CORE`, `SHRS`, `TREE`, `MATL`, `SHAD`, `TXDI`, `TXIN` and `TXFD` sections as stored, with section descriptors and a block table (`PackedFormat` 5): `GSAthc3UndeltaCS` sums the delta planes, `GSAthc3CpcaCS` rebuilds CPCA, `GSDecodeAthc3CS` the splats and `GSDecodeAthc3TxCS` the transfer's buffers |
| `.splat`, `.spz` readers | `GSplatFileImporter.cpp`, `GSDecodeSplatCS` and `GSDecodeSpzCS` in `GSDecode.usf` | a file's own bytes (decompressed for `.spz`), decoded on the GPU (`PackedFormat` 1 and 2) |
| `.sog` reader | `GSplatSogImporter.cpp`, `GSDecodeSogCS` in `GSDecode.usf` | the images' texels, ranges and codebooks (`PackedFormat` 3), decoded on the GPU; a small zip reader of its own |
| `.athz` reader | `GSplatAthzImporter.cpp`, `GSAthzTablesCS` and `GSAthzDecodeCS` in `GSDecode.usf` | an `.athz`'s pages, page list, planes, frequencies and a descriptor a level (`PackedFormat` 6); decoded into the scratch at load, the splats then read as a `.athc` v3 block of encoding 4 (words already in the scratch) |
| Web scene reader | `GSplatSceneImporter.cpp` | a `.athz` zip: `scene.json`, each `.athc` through the `.athc` reader (a file of its bytes under `Intermediate/athenea/athz`), the sky through UE's own import; the zip reader is the SOG importer's (`GSplatImporter::ReadZip`) |
| glTF reader | `GSplatGltfImporter.cpp`, `GSDecodeGltfCS` in `GSDecode.usf` | the file's buffers and a table of 21 accessor descriptors (`PackedFormat` 4, the table at `PackedStreams[0]`), every accessor read on the GPU |
| GPU decoder | `GSDecode.usf`, `FGSDecodeAtheneaCS` in `GaussianSplatResource.cpp` | athenea's packing into the float buffers the passes read, once a resource |
| Fixes | see `decisions.md` | 5.8 build, out-of-bounds append, pre-exposure, shadow bound, bool culling, vertex factory flags, plugin content, bounds initialisation |
| GPU stat | `GSViewExtension.cpp` | `GPU/GaussiansUnlit`, beside upstream's `GPU/GaussiansLit` |
| Transfer | `GSColor.ush` (`GSTransferredBody`), `FGaussianSplatData::Transfer` | a TX cloud's body: albedo (1 − metallic) ⟨T_direct + T_indirect, L⟩, athenea's `transferredBody` |
| Sky projection | `GSSkySH.usf`, `FGSSkySHCS` in `GSRasterizePass.cpp` | the sky light's cubemap on 16 harmonics in a primitive's file frame, one group a TX primitive a view, before its prepass |
| Glass | `GSLensExit`, `GSLensExitThrough`, `GSLensFarReflection`, `GSTransmittedRoughness`, the solid-glass branch in `GSTransferredRadiance` (`GSColor.ush`); slot 19 of the surface (transmission, plus 2 for a thin wall) | athenea's `splat_relight` lens, the body as its `body2`: the opaque share, then what passes, through the lens, and the far face's reflection |
| Sharp lobes | the turn in `GSPrepass` (`GSCompute.usf`), the split in `GSTransferredRadiance` (`GSColor.ush`), the read in `GSSplatting_PS` (`GSSharpSky`); `GSSunWorldCS` in `GSSkyResidual.usf` | athenea's sharp lobe drawn per pixel: two `uint4` a splat (`OutSharpA`, `OutSharpB`) from the prepass to the raster, the sun in the world in a two-element buffer |
| Residual sky | `GSSkyResidual.usf` (`GSSkyCutCS`, `GSSkyDownCS`, `GSSkyResidualCS`), in `GSRasterizePass.cpp` | the sky light's cube with its sun cut and box-filtered (the cut), then prefiltered by GGX into the cube the reflections read; built once a sky light's cube and colour (`GAtheneaResidual`) |
| Polish | `GSPolish.ush`, `GSTransferredRadiance` in `GSColor.ush` | athenea's opaque `relitByDome` sum: layers, environment BRDFs, open-direction cells, the reflected field and its coupling to the sky |
| Debug | `r.Gaussians.Athenea.Debug`, `.LogSky` | views of the projected sky, and its readback to the log |

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
  linearise, bit 2 the colour is an albedo lit by a transfer.

### 5.2.1 The transfer and its sky

- `Transfer` holds `4 · TransferDirectCount` floats a splat: the direct
  values (9 or 16), then the indirect ones, rgb per coefficient, in athenea's
  order. `TransferMetallic` holds one a splat.
- The sky is projected on athenea's environment basis
  (`envBasisValue`, light/environment.slang): 3DGS's real harmonics.
- `GSSkySH.usf` takes 4096 directions on a Fibonacci sphere **in the file's
  frame**, takes each to the world through `FileAxisX/Y/Z` and the primitive's
  `LocalToRelativeWorld`, reads the sky light's `ProcessedTexture` there at
  the mip about 32 texels a face, times `View.SkyLightColor`, and sums the
  basis in the file's frame. The transfer and the sky then share a frame
  whatever the actor's rotation, mirror included.
- The polish reads the residual cube (or, with `r.Gaussians.Athenea.Residual
  0`, the sky light's) at the mip UE's own reflections read at a lobe's
  roughness (`ComputeReflectionCaptureMipFromRoughness`: `mips − 1 − (1 −
  1.2 log2 r)`), its direction taken to the world the same way; the residual
  prefilters each mip at that roughness. The sun comes back through each lobe
  by `GSSunReflectPdf` (athenea's `sunReflectPdf`), times the lobe's weight,
  the sun's irradiance and the share the cells let through.
- A sharp lobe's turn (athenea's `splat_project`): the gaussian's first two
  axes are the file's mapped through its rotation (`R' · FileAxisX`,
  `R' · FileAxisY`, R' its local rotation, which is M R Mᵀ for the file's
  mapping M); a pixel right and down on its plane comes from the projection's
  Jacobian (`JW`, pixels a metre, the view's y negated for a pixel down), in
  the file's units through the actor's scale and `FileUnitsToCm`; the shape
  operator times that step, along those axes, is the normal's turn in the
  file's frame, at most 0.5. A lobe's roughness widens by the pixel's own
  turn (α² += (|dx|² + |dy|²) / 6). The payload: A = the mirror at the centre
  in the world (octahedral), its steps, the coat's weight (a luminance) and
  roughness; B = the polish's weight (rgb) and roughness, the sun's through
  the polish (rgb) and the coat, each times the share the cells let through.
- `TransferSurface` holds 20 floats a splat (`GS_SURFACE_STRIDE`): normal (3),
  roughness, specular weight, colour (3) and index, coat weight, roughness,
  index and darkening, sheen colour (3) and roughness, Schlick's metal, the
  material's roughness, and one unused. `TransferCells` holds `CellWords`
  words a splat; `TransferField` 48 floats.
- Which openness a lobe takes follows athenea: the base lobe uses the cells
  and the field only when the material's roughness is 0.3 or more
  (`kPolishFieldRoughness`), and Lagarde's fit of the transfer's constant
  term below that; the coat uses the cells wherever the cloud keeps them.
- The sky pass also finds the sky's sun, as athenea's `env_sun` does: over
  16384 equal-area directions at a mip of about 64 texels a face, a
  histogram of log2 luminance (80 half-stop bins) gives the median; the sun
  is the directions past max(16 median, 0.02 peak) within 0.175 rad of the
  peak, where the peak is 32 times the median, kept only when its
  light-weighted spread is no more than a disc's of its solid angle (else its
  directions go back into the harmonics). `SkySH[16]` is its direction and
  half angle (0: none), `SkySH[17]` its irradiance and the threshold. The
  body adds `irradiance · cos · share / π`, the share read from the cells
  along the sun, and the bounce `irradiance · T_indirect(sun)`; the field's
  coupling takes the sun into both of its sums.
- The cubemap is read from the scene's sky light proxy
  (`FScene::SkyLight->ProcessedTexture`): UE's reflection uniform buffer
  (`CreateReflectionUniformBuffer`) is not exported to plugins.

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
did (log scales, the sigmoid, swizzles); the `.athc`, `.splat` and `.spz`
paths are the model the others are to follow. Those two keep the file's own
bytes (`PackedBytes`, `PackedFormat`, and for `.spz` `PackedStreams` and
`PackedSpz`), uploaded as a byte-address buffer; the CPU's one piece of
arithmetic there is the bounds, which neither format stores.

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
5. Log one line on success and a reason on every refusal (operations §4.6).
6. Check it as in §7, and document it in all three manuals, both languages.

## 7. How a change is checked

The importers have automation tests (`Source/GaussiansEditor/Private/Tests/`,
`Athenea.Import.*`): each test file is read by its importer and what the
import made of it is checked -- every format reads, holds gaussians and has
bounds; `pawn.athc`'s 8 levels and their words; a web scene's paged tree, one
root and its 349,076 merges; an `.athz`'s 7 levels, its groups after its
splats; a `vdb2splat` frame's medium and constants (a
cold frame with no temperature reads 0); a TX cloud's transfer. They need no
GPU (the decode runs when a resource is made) and take the test files from
`ATHENEA_UE_TEST_FILES` (default `~/athenea-unreal`), skipping one that is not
there:

```
UnrealEditor-Cmd.exe <project> -nullrhi -unattended -ExecCmds="Automation RunTests Athenea; Quit"
```

Past them, a change is checked on a Windows machine against athenea:

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
| `setup_tx.py` | as `setup.py`, or `-ExecCmds="py setup_tx.py paint.athc /Game/TestTxW paint 1 --quit"` | imports `paint_tx.usdc` and `studio.hdr` and saves `/Game/TestTx`: the cloud (scaled 100, its stage saying centimetres), a sky light with the HDRI, the test scene's camera; or another cloud into another level at another scale (1 for a file in metres) |
| `shot_tx.py` | `-ExecCmds="py shot_tx.py 0 90 180"` | PNG (and UE's saturating HDR) shots of `/Game/TestTx` with the sky light turned by each angle |
| `capture_tx.py` | `-ExecCmds="py capture_tx.py 180 name [/Game/Level] [cvar=value;...]"` | the linear capture (operations §7.1) of `/Game/TestTx` (or the level given) at an angle, console variables set first, and the projected sky logged |
| `shot_dbg.py` | `-ExecCmds="py shot_dbg.py 100 101 ..."` | shots with `r.Gaussians.Athenea.Debug` at each value |
| `setup_files.py` | `-ExecCmds="py setup_files.py a.spz b.glb@100 --quit"` | imports each file (from the home folder's `athenea-unreal`) with the plugin's factory into `/Game/Files` and saves a level `/Game/F_<name>_<ext>` with an actor of it; `@k` scales the actor, for a file in units other than metres |
| `shot_framed.py` | `-ExecCmds="py shot_framed.py +y x3 /Game/F_a /Game/F_b"` | a shot of each level, framed on its splats' bounds from a side (`±x`, `±y`), `x<k>` k times further, into `shots/framed_<level><side>.png` |
| `ue_time.sh` | `ue_time.sh /Game/Level out.csv ["cvar value,..."]`, from a machine with `ssh` to the Windows one | times a level in `-game` with the CSV profiler (30 fps cap), console variables set first, and prints the splat stages' medians |
| `setup_scene.py` | `-ExecCmds="py setup_scene.py /Game/Level sky.hdr 100 0:0.05:0.18 0:0.05:0 39.6 a.athc b.athc --quit"` | several clouds and an HDRI's sky light in a level, a camera from the files' frame (`x:y:z`, as `-ExecCmds` splits on commas), every actor and the camera scaled (a scene far smaller than UE's near plane) |
| `tx_regions.py` | `python3 tx_regions.py ue.exr athenea.exr [exposure]` | the TX test frame's ball, its halves and the ground, UE (over the capture's exposure factor, 2.02) against athenea's frame: means, ratio and mean absolute relative error |
| `TestProj.uproject`, `DefaultEngine.ini`, `DefaultEditorPerProjectUserSettings.ini` | -- | the test project: SM6 only, auto-exposure, bloom, motion blur and temporal AA off; the MCP server's settings (operations §8) |
