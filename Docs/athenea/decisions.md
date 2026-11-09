# athenea for Unreal: decisions

Copyright (c) 2026 jesus luque.

Every change this fork makes to StarLightGS (upstream 8720289), why it was
made, what was measured, and what is not done. Newest last. How to use the
plugin is in `operations.md`; how the code is laid out, in `development.md`.

## Contents

1. [The fork and where it runs](#1-the-fork-and-where-it-runs)
2. [Upstream defects fixed before building](#2-upstream-defects-fixed-before-building)
3. [Building against UE 5.8.4](#3-building-against-ue-584)
4. [The first frame](#4-the-first-frame)
5. [Harmonics along the view](#5-harmonics-along-the-view)
6. [An athenea PLY says its axes and colour space](#6-an-athenea-ply-says-its-axes-and-colour-space)
7. [USD ParticleFields through UE's OpenUSD](#7-usd-particlefields-through-ues-openusd)
8. [.athc packed, decoded on the GPU](#8-athc-packed-decoded-on-the-gpu)
9. [The sRGB curve, not a 2.2 power](#9-the-srgb-curve-not-a-22-power)
10. [Timing](#10-timing)
11. [Transfer, the body and the polish](#11-transfer-the-body-and-the-polish)
12. [.splat, .spz, .sog and glTF](#12-splat-spz-sog-and-gltf)
13. [Not done](#13-not-done)

## 1. The fork and where it runs

StarLightGS (CoSTAR National Lab, MIT) was chosen as the base because it
already does in UE what athenea needs first: an asset of gaussians, a compute
prepass with frustum and HZB culling and EWA projection, one global radix sort
a view, and an indirect draw of instanced quads blended over the scene. What
it lacks is what makes athenea's clouds athenea's: view-dependent colour,
athenea's formats, levels of detail, and transfer. `DESIGN.md` gives the order.

It is built and tested on a Windows laptop (RTX 3070 Laptop GPU, 8 GB) with
UE 5.8.4 from the Epic launcher.

## 2. Upstream defects fixed before building

Found by reading the code, fixed before any build (commit 51a9446):

- **An append past the buffer.** The prepass appends visible splats into one
  buffer of `r.Gaussians.MaxSplats`. Upstream checked only that a group's
  start was inside it, so the group straddling the end wrote past it. DX12
  drops those writes; Metal and Vulkan do not. The group now keeps what fits
  and the counter is set to the capacity.
- **No pre-exposure.** Unlit colour is scene light, and everything else in
  SceneColor is multiplied by the view's pre-exposure; the splats were not.
  They are now, in the pixel shader, on the unlit path only.
- **The shadow vertex bound.** The vertex factory fetched one splat past the
  count (`Index > Count` where `>=` is meant).

## 3. Building against UE 5.8.4

Upstream says it supports 5.8; it did not build with the launcher's 5.8.4
(commit bb201b1):

- **Includes.** UE 5.8's headers no longer bring in what upstream relied on:
  `ShaderParameterMacros.h` and `UniformBuffer.h` for the uniform buffers,
  `VertexFactory.h`, `SceneManagement.h`, `SceneView.h`, `MeshBatch.h`,
  `Materials/MaterialInterface.h` and `Materials/Material.h`,
  `Engine/World.h`, `UObject/ConstructorHelpers.h`, `Misc/Paths.h`,
  `PrimitiveUniformShaderParameters.h`, `RenderingThread.h`,
  `RHICommandList.h`, `RHIResourceUtils.h`. About 680 errors before; none
  after.
- **Categories.** UHT in 5.8 requires a `Category` on every property and
  function exposed to the editor or Blueprints; upstream had ten without one.

The machine also needed two things outside the code: Windows Smart App Control turned off (it blocked UnrealBuildTool's
own rules DLL) and the .NET Framework 4.8 SDK added to the Build Tools.

## 4. The first frame

Five more defects stood between a successful build and a picture:

- **The plugin packaged without its content** (commit d9a9174):
  `"CanContainContent": "true"` is a string, which 5.8 reads as false, so
  `BuildPlugin` dropped `Content/` and the component's constructor asserted on
  a missing material. It is a boolean now.
- **`&=` on a bool.** The culling code combined visibility with `bVisible &=
  ...`, which 5.8's shader compiler refuses (X3020). Six places now write
  `bVisible = bVisible && (...)` (commit 0a89bfb).
- **Too many UAVs for SM5.** The prepass binds 10 UAVs; SM5 allows 8. The
  plugin needs an SM6-only project (operations §3).
- **Lightmap permutations.** The vertex factory declared
  `SupportsStaticLighting` but fetches no lightmap coordinates, so every
  lightmapped base-pass permutation of the default material failed to compile.
  The factory only draws shadow depth; the flag is gone (commit 07c6c4a).
- **Bounds not initialised.** `FGaussianSplatData::Bounds` had no initialiser,
  which UE's member-initialisation test reports at load; it starts zeroed
  (commit 4968e2b).

The first image (pawn, 325 833 gaussians, unlit): drawn, upright after a
rotation of the actor, and tiny, because the camera was framed on the actor's
bounds, which include the editor billboard (128 cm); the component's bounds
(about 4 × 4 × 7 cm) frame it.

## 5. Harmonics along the view

Upstream reserves 45 floats a splat for degree 1 to 3, never imports them
(the importer zero-fills the array) and never reads them: every cloud was
drawn with its base colour only. Commit 7ed3cf4:

- The PLY importer reads `f_rest_0..44` when all 45 are present and keeps
  them in the file's layout, splat by splat.
- The prepass evaluates them (`GSColor.ush`, 3DGS's constants and signs) along
  the direction from the eye to the splat. That direction is taken into the
  asset's local space, then into the file's frame through `FileAxisX/Y/Z`,
  because the harmonics were fitted in the file's axes, not UE's.
- The relit path does not use them: its colour is an albedo.

**Checked against athenea.** athenea's raster rendered the same PLY from the
same three cameras (front, side, high), with UE's auto-exposure, bloom,
motion blur and temporal anti-aliasing off. Shape, framing and the glass
head's reflections fall in the same places in all three views, including the
dark spot on the head seen from above. The one difference is the head's
colour, which UE's filmic tonemapper takes from teal toward white; that is
UE's display transform, not the splats.

## 6. An athenea PLY says its axes and colour space

`athenea flatten` writes `comment athenea flatten: degree 3, sRGB
display-referred, y up, metres`. Upstream's default swizzle (Z, X, −Y) is
COLMAP's y-down convention, so athenea's y-up pawn came in upside down, and
its colour was taken as linear. The importer now reads the comment (commit
7ed3cf4, operations §4.2): `y up` gives (X, Z, Y), `metres` scale 100, `sRGB
display-referred` linearisation. The per-asset choice is made from the file,
not by hand.

**Gamma moved to the GPU.** Upstream raised the base colour to 2.2 on the CPU,
before anything else; with harmonics that is wrong, because a display-referred
colour is the sum of all bands and is linearised once, after the sum. The CPU
now neither clamps nor applies a gamma; the prepass clamps at zero and
linearises after summing.

## 7. USD ParticleFields through UE's OpenUSD

The choice: read athenea's own `.usd` export rather than invent an
interchange file. UE's USD importer knows neither `ParticleField3DGaussianSplat`
nor athenea's `primvars:athenea:splat:*`, so the plugin reads the stage
itself, through the OpenUSD UE bundles in its USDCore plugin (0.26.3, which
has the ParticleField schemas) (commit 7591c05).

- It reads what athenea's own reader reads (operations §4.3).
- Its factory claims a USD file only when the stage holds a gaussian
  ParticleField, at a higher priority than UE's importer, so every other
  stage still goes to UE's.
- **File versions.** athenea writes with OpenUSD 26.08 and UE reads with
  26.03; the 40 recent `.usdc` files checked all use crate 0.8.0, which both
  read.

**Measured.** The pawn's USD export (76 MB `.usdc`) imports in about 2 s:
325 833 gaussians, degree 3, sRGB.

**Licence.** OpenUSD is under the Tomorrow Open Source Technology License 1.0
(Pixar's modified Apache 2.0). UE ships it under that licence; the plugin
links against UE's copy and redistributes none of it.

## 8. .athc packed, decoded on the GPU

A `.athc` holds athenea's device packing: 32 bytes a splat before harmonics,
and 23 words of f16 harmonics at degree 3, against 236 bytes as floats.
Converting it on the CPU would break athenea's rule that the CPU only copies
bytes, and would quadruple what the asset keeps. So (commit 2e98148):

- The importer copies the finest level's chunks as they are into the asset
  (`bPacked`: `PackedPositions`, `PackedShape`, `PackedSH`).
- When the resource is made, `GSDecode.usf` writes the float buffers the
  passes read, one thread a splat: the smallest-three rotation, the f16 log
  scales, the f16 base colour and the rest coefficients, through the same
  axis mapping, scale convention and quaternion rule the PLY importer uses.
- Bounds come from the file's header.

**Measured.** The pawn's `.athc` (60.6 MB, 5 chunks, 8 levels) imports with
the levels not read. From the three cameras it and the USD export render
alike: mean absolute difference 3.8 × 10⁻⁶, RMS 3 × 10⁻⁴ on a 0 to 1 scale;
the few pixels that differ more (at most 0.21) sit on splat edges, where the
`.athc`'s 10-bit rotations and f16 log scales differ from USD's floats.

**Version 3, the web's.** sparkwebGPU writes `.athc` v3 for the browser
(its `docs/docs/athc-v3.md`): v2's values in sections and tiers, each block's
sections gzipped and stored as v2's words, byte planes or byte planes of
16-bit differences, so a published cloud is one of these. Rather than undo
the planes on the CPU, the importer keeps each block's `CORE` and `SHRS` as
stored after the gunzip, with a table of blocks, and one kernel reads a word
in any encoding: in place for words, four bytes from four planes for byte
planes, and from a scratch for differences, which a pass sums first, one
thread a word of a block's rows (a word's two lanes have one owner, so no
two threads write the same word). The CPU decompresses and copies, nothing
else.

- **Checked.** sparkwebGPU's fixture `every_stream` exists as v2 and as v3 in
  each encoding, the same 300 splats (their decoded `CORE` and `SHRS` bytes
  are identical). Shot from the same camera, each v3 differs from the v2 by
  0.0001 to 0.005 on average, and the v2 differs from itself by 0.0007 from
  one run to the next: a few splats larger than the frame, close to the
  camera, are drawn or not from run to run (from another side, one of them
  covered half the frame in some runs and none in others). The decode is
  inside that noise; the fixture is no sharper a test than this from a
  camera.
- **Error-band pages.** A published piece (the pawn's top, `car_M_Pawn_Top_W`,
  698 791 nodes in 233 pages) is a paged tree: every block a page of leaves
  and merges together, no chunks. Its `TREE` section counts each node's
  children; the decoder reads it and draws a merge at no opacity, so the
  leaves alone are seen, until the levels are read. Its two arrays of one
  word are cut from the run of whole rows the writer lays down (a node's
  page and its first child's place with count, side by side), so node e's
  word w is that run's word 2e + w. Read as two columns, half the merges
  counted no children, and in the whole pawn the coarse ones -- 1 to 2 cm
  across a 4 cm piece, at 0.99 opacity -- stood behind it as a tilted quad
  and a white ellipse. Its units are metres,
  and the piece is small: a chess pawn's head, 2.2 cm across, so a camera
  framing it came within UE's 10 cm near plane and drew nothing; the level
  scales the actor by 100 (this note first called the units wrong). At
  degree 0 a v3 header says one harmonic word, as v2's dummy word; the first
  check refused it.
- **A TX cloud from the web's file.** The sections are described once (their
  words and arrays) and each block names every section's place, encoding and
  scratch, so one fetch reads any word of any section and one pass sums any
  delta-coded word. A CPCA section (Sloan et al. 2003, the web's encoding 3)
  is rebuilt by a thread an element into the scratch, in the web decoder's
  order of operations: the mean, then each coefficient times its step times
  its direction, in f32, then the nearest half. A last kernel writes the
  transfer, metallic, the surface (normal, roughness, lobes, the pbr word's
  bits), the cells and the field into the buffers the USD importer's arrays
  fill, so the shading does not know where they came from.
- **Checked against the USD.** athenea's TX test cloud (`paint_tx.usdc`,
  272 907 gaussians, transfer of 112 values, 16 × 16 cells, the material)
  written to `.athc` v3 by sparkwebGPU's `usd-athc` (v2's words, gzip), then
  by its `athc-convert --planes` (each section in its smallest encoding:
  `CORE` and `TXIN` byte planes, `MATL` delta planes, the rest words) and
  `--cpca 16,48` (the transfer as CPCA: 51.8 MB to 6.7 MB). Each drawn under
  the same sky light from the test camera, linear captures against the USD's:

  | File | Mean | RMS |
  |---|---|---|
  | words, gzip | 0.00108 | 0.0045 |
  | smallest encodings | 0.00109 | 0.0046 |
  | CPCA | 0.00142 | 0.0052 |

  On values up to 3. The first two are the file's f16 transfer, byte
  material and 16-bit normals against USD's floats; CPCA adds 0.0003, its own
  loss. A first CPCA import was refused: a CPCA section's raw size in the
  block index is its decoded size, not its payload's, and the gunzip was
  checked against it. The `.athc` holds metres, so its level takes no ×100
  (the USD stage says centimetres over metres' numbers, and its actor does);
  a first capture with ×100 put the camera inside the ground.
- **athenea's own `.athc` v2 with a transfer** goes the same way: a v2 chunk
  lays out the same arrays (positions, shape, harmonics, tail, the material,
  the transfer, the cells), so the importer keeps each chunk's bytes and
  describes the arrays where they are, every section in v2's words, the whole
  transfer as one section. No kernel was added. The paint cloud written as v2
  (`athc-convert --v2`, 115.8 MB) against the USD: mean 0.00113 (RMS 0.0049);
  against the v3 in words, 0.0006. A v2 file of degree 0 written by the web's
  tools keeps one harmonic word, which the v2 header check refused; it is
  taken now.

## 9. The sRGB curve, not a 2.2 power

athenea linearises a display-referred cloud with the sRGB curve
(`cloudLight` → `srgbToLinear`). The plugin had used upstream's 2.2 power,
which departs from it in the shadows. It uses the curve now (commit 2e98148).

## 10. Timing

The pawn's front view (325 833 gaussians, degree 3) at 1280 × 720, from the
PLY level, in `-game` mode with the CSV profiler, a 30 fps cap, on the Windows
laptop's RTX 3070 Laptop GPU checked free (61 °C at the start, 65 °C at the end).
Medians over the last 300 of 450 frames:

| Stage | Median | Fastest |
|---|---|---|
| Prepass (`GPU/GaussiansPrePass`): culling, projection, harmonics | 2.40 ms | -- |
| Sort (`GPU/GaussiansSort`) | 1.29 ms | -- |
| Raster (`GPU/GaussiansRaster`) | 5.34 ms | -- |
| The splats, the three together | 9.0 ms | -- |
| The whole frame on the GPU (`GPUTime`) | 11.90 ms | 11.44 ms |

athenea's raster draws the same frame in **14.1 ms** (best of its run) on an
Apple M5 Pro. The two are different machines, so this says the plugin is in
the same range as athenea's own raster, not which is faster; a comparison on
one machine needs athenea built on the same Windows machine.

Two earlier runs are not valid and are kept as what not to repeat: one gave
440 ms a frame while another job (in WSL, which `nvidia-smi` does
not list) held the GPU at 100 % and 780 MHz; the other, uncapped, heated the
GPU to 87 °C and throttled it to 780 MHz during the run, giving 84.6 ms for
the splats. Frames are now timed at a 30 fps cap on a GPU checked free, and
the profiler's file is copied only once it stops growing.

## 11. Transfer, the body and the polish

A TX cloud carries no radiance: its colour is an albedo, and a frame lights it
from the transfer athenea baked into each gaussian. The first stage of the
port is the body: `albedo (1 − metallic) ⟨T_direct + T_indirect, L⟩`, with
`L` the sky's radiance on the same 16 harmonics, athenea's `transferredBody`.

**The sky.** UE's sky light, projected each frame by a compute pass onto those
harmonics in the cloud's own frame (development §5.2.1). Checked by reading
the 16 coefficients back from the GPU and projecting the same HDRI in Python
with athenea's dome convention (UsdLux): at `SourceCubemapAngle` 180 they
agree within 2.8 %, UE 2.8 % brighter. A search over the 48 signed axis
mappings and turns about the vertical found no mirror: the angle is the only
thing to set.

**The test.** athenea's paint-ball scene converted with `mesh2splat
--transfer` (272 946 gaussians, 16 direct and 48 indirect values each),
under the same studio HDRI in both, from the scene's camera. The ball is a
metal (metallic 1): it has no body, and draws black until the polish is
ported. The ground (albedo 1, metallic 0):

| | athenea raster | UE, the body |
|---|---|---|
| ground mean, linear | 1.046 | 0.92 |
| the darkest 5 % (the ball's shadow) | 0.372 | 0.38 |

UE's values are the capture's (operations §7.1) divided by its exposure,
1.72, measured from the projected sky's mean radiance. The shadow agrees;
the ground's mean is 12 % lower, which is what athenea adds through the sun it
separates from this sky (its log: a sun 14.8° across) and lights through the
open-direction bits, not yet ported.

**The polish.** The rest of athenea's sum for an opaque surface came next,
function by function (`GSPolish.ush`): the coat and sheen layers with
OpenPBR's coat darkening, the environment BRDFs (dielectric with its exact
Fresnel, conductor through MaterialX's artistic index, Schlick's metal),
the open-direction cells read along the mirror lobe, and the reflected field
coupled to the sky by the indirect transfer. The sky light's cubemap is
raised to 1024 texels a face for it (128 by default). Measured on the same
frame, against athenea, with the ball's disc fitted from UE's image:

| | athenea | UE | UE / athenea | mean abs. rel. error |
|---|---|---|---|---|
| ball | 0.058 | 0.071 | 1.22 | 0.47 |
| ball, upper half (the sky) | 0.081 | 0.102 | 1.26 | 0.60 |
| ball, lower half (the ground, through the field) | 0.035 | 0.041 | 1.16 | 0.16 |
| ground | 1.032 | 0.934 | 0.91 | 0.11 |

Before the field the ball's lower half was black: its closed directions
reflected nothing.

**The sun.** athenea takes a sky's sun out of its harmonics and gives it back
as a directional light, shadowed by the cells along it and bounced through
the indirect transfer (`env_sun`, `transferViewless`). The sky pass now does
the same (development §5.2.1): over 16384 directions, a histogram of log
luminance gives the median, the sun is what stands past max(16 median,
0.02 peak) within ten degrees of the peak where the peak is 32 times the
median, and it is kept only where it is as compact as a disc. For this HDRI
UE finds it at (−0.37, 0.58, 0.72), 17.4° across, irradiance 1.49; athenea
at (−0.51, 0.63, 0.59), 14.8°, 1.35 (its own walk of the lat-long). The
reflections still read UE's prefiltered mips, which keep the sun in them.

**A vignette, not the transfer.** The tables above were measured through
UE's post-processing, whose vignette (0.4 by default) darkens the frame
towards its edges: a debug view of one flat colour read 1.27 at the centre
and 0.95 at the bottom. With the vignette off on the capture, the flat colour
is the same everywhere and the capture's exposure factor is 2.02. Measured
again, sun included:

| | athenea | UE | UE / athenea | mean abs. rel. error |
|---|---|---|---|---|
| ground | 1.032 | 1.048 | 1.016 | 0.024 |
| ball, lower half (the field) | 0.036 | 0.037 | 1.046 | 0.091 |
| ball, upper half (the sky's reflection) | 0.082 | 0.094 | 1.156 | 0.554 |
| ball | 0.059 | 0.066 | 1.123 | 0.413 |
| the ball's shadow (athenea's darkest 5 %) | 0.270 | 0.304 | 1.127 | 0.172 |

The body and the field agree; what is left is in the sky's reflection on the
ball: the sun still in UE's prefiltered mips and unshadowed there, and the
slope of a sharp reflection across a splat, which athenea draws and this does
not (its windows blur).

**The sky without its sun in the reflections.** athenea prefilters its sky
with the sun cut out (`env_residual`) and reflects the sun analytically
through each lobe (`sunReflectPdf`): the split sum is a quarter short for a
compact sun. The plugin does the same: a cut cube, the sky light's cube read
at 256 texels a face with the sun's texels (env_sun's cut: within ten degrees
of the sun and as bright as the threshold it was found at) set to nothing,
box-filtered down its mips; then the residual, each mip GGX-sampled from the
cut at the roughness UE's captures put there (128 samples, each read at the
cut's level matching its solid angle). Measured on the same frame against
athenea (`tx_regions.py`):

| | UE's prefiltered cube | residual sampled from UE's mips | residual from the cut |
|---|---|---|---|
| ball | 1.117 (0.317) | 1.121 (0.394) | 1.081 (0.239) |
| ball, upper half | 1.181 (0.569) | 1.169 (0.704) | 1.124 (0.437) |
| ball, lower half | 0.994 (0.067) | 1.027 (0.086) | 0.996 (0.043) |
| ground | 1.017 (0.026) | 1.023 (0.031) | 1.012 (0.022) |

UE / athenea, with the mean absolute relative error. The middle column was
the first version: it cut the sun from UE's own mips, which are prefiltered
from the whole sky, so their sun is a halo under any threshold; it stayed in
the map and the analytic sun came on top. The cut's own box mips hold no sun
at all. What is left on the ball's upper half is the sharpness: athenea draws
a smooth coat per pixel (its reflection slope) and shows the room in it,
which one colour a splat cannot.

**Timing** (`ue_time.sh /Game/TestTx`, RTX 3070, 1280 × 720, medians of 300
frames): building the residual every frame cost 1.3 ms of GPU
(`GPU/GaussiansSky` 1.69 ms against 0.40 ms for the projection alone); it is
the world's, so it is built once for a sky light's cube and colour and kept,
shared by every TX cloud. Kept, `GPU/GaussiansSky` is 0.397 ms against
0.389 ms with the residual off, and `GPUTime` 11.90 ms against 11.69 ms.

**The sharp lobes, per pixel.** What the residual left on the ball's upper
half was the sharpness: athenea reads a smooth coat (under 0.2) and a sharp
base polish (under 0.5) per pixel, along the mirror each pixel turns to by the
surface's curvature, and a splat's single colour cannot show a window
mirrored inside it. Ported (athenea's `SplatSlope` sharp lobe,
`splat_project`, `splat_blend`): the cloud's shape operator is read
(`primvars:athenea:splat:curvature`, `CURV`), the prepass turns the normal a
pixel right and down, takes the centre's reading out of the colour and hands
two words of payload to the raster, which reads the residual and the sun
again at each pixel. Measured on the same frame:

| | the residual | and the sharp lobes |
|---|---|---|
| ball | 1.081 (0.239) | 1.030 (0.124) |
| ball, upper half | 1.124 (0.437) | 1.048 (0.213) |
| ball, lower half | 0.996 (0.043) | 0.997 (0.035) |
| ground | 1.012 (0.022) | 1.012 (0.022) |

UE / athenea (mean absolute relative error). The window and the two lights
the coat mirrors now stand where athenea draws them. The paint cloud as
`.athc` v3 (the smallest encodings, and CPCA) gives the same (ball 1.033,
upper half 1.051); its v2 keeps no curvature and stays at the residual's
(1.085). `GPUTime` 11.84 ms against 12.07 ms with the residual off: inside
the run-to-run noise.

**Glass.** A TX cloud's glass came out opaque in UE: the plugin read no
transmission. athenea's raster lens is ported as it is (`lensExit`,
`lensExitThrough`, `lensFarReflection`, `transmittedRoughness`, the solid
glass's branch and the body as `body2`), with the transmission and the
thin-wall bit in the surface's spare slot and the index a value of the cloud.
The dragon's crystal -- surfels with no mesh, baked by athenea's new
`splat-transfer` -- draws in UE as the light lilac glass athenea's raster
draws from the same camera. A thin wall is drawn at its file's opacity with
its base polish scaled by athenea's `thinWallGain`; no thin-wall TX cloud
was at hand to measure it against athenea's frame.

Two mistakes first: the curvature primvar has three values a gaussian and
was read with the one-a-gaussian reader, which dropped it (debug view 6 shows
which condition holds); and a version 3 block grew by the `CURV` section to
29 words and was padded to 34, not the 32 the shader steps by, which read
every later block's sections from the wrong place (the CPCA ball came out at
3 % of athenea's).

**Measurements that were wrong first, kept as what not to repeat.** UE's HDR
screenshot saturates near 1, so it read the ground 26 % dark and every
projected coefficient as about 1; and the test stage says centimetres (its
root layer sets none) while its numbers are metres, so the cloud first came
in 2 cm across.

## 12. .splat, .spz, .sog and glTF

The user asked for every format. These four are the most common after PLY,
and athenea reads the first three (`io/SplatReaders.cpp`, `io/Sog.cpp`) and
writes the fourth; the plugin reads them the way it reads a `.athc`: the
file's bytes kept, a kernel a format to decode them (`GSDecodeSplatCS`,
`GSDecodeSpzCS`, `GSDecodeSogCS`, `GSDecodeGltfCS`).

- **Compression.** `.spz` versions 2 and 3 are gzip, which UE's zlib undoes;
  version 4 is zstd and `.sog` is WebP, neither of which UE ships. With the
  user's OK their decoders' sources were taken from the projects' own
  repositories (zstd v1.5.7, libwebp v1.5.0, both BSD) into a module of their
  own, `AtheneaCodecs`, editor only.
- **Axes.** athenea turns an SPZ, right-up-back by the format, by negating
  y and z into a 3DGS PLY's right-down-front, the frame its PLYs are read in.
  The plugin composes that turn with its PLY mapping, `(z, x, −y)`, into
  `(−z, x, y)`, so an SPZ stands as a PLY does. A first version took
  athenea's PLY frame as y-up, `(x, −z, −y)`, which stood a painted cat
  upright -- as athenea's default view does, since that view reads a PLY's
  frame as y-up (COLMAP clouds want `--rotate-x 180` there) -- and drew a
  version 4 SPZ written by splat-transform upside down, against the `.glb` the
  same tool wrote of the same cloud. The cat holds y-down coordinates without
  the coordinate-system extension saying so: it is the file that is off, and
  it now stands upside down, as in athenea under a y-up view. A `.splat` keeps
  COLMAP's axes, as a 3DGS PLY.
- **Checked.** A version 2 SPZ (the cat, 206 232 gaussians, first-three
  rotations), a version 3 (404 092, smallest three) and a `.splat` (the
  dwarf, 524 288) import and draw. A version 4 (PlayCanvas's `biker.spz`,
  152 746, degree 0) was refused at first: degree 0 has five streams, not six;
  it now reads.
- **`.sog`** follows athenea's `io/Sog.cpp` and `scene/sog_decode.slang`:
  the zip and the WebP images undone on the CPU (libwebp from
  `AtheneaCodecs`), the rest a kernel. Checked on athenea's own fixture,
  `sh3.sog` (64 gaussians, version 2, a degree-3 palette) against the PLY it
  was written from, both through the same axes and colour space: mean
  difference 0.0017 (RMS 0.015) between the two frames, which is SOG's
  quantisation -- athenea's two frames of the same pair differ by 0.0014. A
  first run read the palette's width and its coefficients a colour from each
  other's slots, and the harmonics drew a negative colour clamped to black.
- **glTF** (`KHR_gaussian_splatting`) is read as glTF reads: accessors of any
  component type, normalised or not, at any stride. Rather than a kernel for
  each layout, the importer writes a table of accessor descriptors after the
  buffers and one kernel reads any of them, so a quantised file
  (`KHR_mesh_quantization`, PlayCanvas's normalised `COLOR_0`) costs nothing
  more. The bounds are `POSITION`'s `min` and `max`, which glTF requires, so
  no arithmetic on the data is left on the CPU.
- **glTF's axes** are not an SPZ's: +Y up as well, but +Z the asset's front
  where an SPZ's +Z is its back. The two files splat-transform wrote of one
  cloud (`biker.glb`, `biker.spz`) first stood back to front; glTF is now
  taken as `(z, −x, y)`, and the two frames differ by 0.00044 on average
  (RMS 0.0020), the SPZ's 8-bit colours and fixed point.
- **UE's own glTF importer** takes `.glb` too. The factory answers for a file
  only when its JSON names the extension, so a mesh `.glb` stays with
  Interchange; dropped into the Content Browser, which of the two UE asks
  first has not been checked (the scripts name the factory).

**The web scene's `.athz`.** What the web publishes is a zip of a scene and
its `.athc` clouds, so with `.athc` v3 read, the scene is a loop over its
files: each cloud through the same reader, the sky through UE's own HDR
import. A UE import makes one asset, so the first cloud is the asset asked
for and the rest are created beside it. The published pawn (`athenea-pawn.athz`,
455 MB: two body pieces of 1.78 M nodes each, the top of 0.70 M, CPCA
transfers, error-band pages) imports in one go, and relights under its sky.
What it shows past the pawn (a large tilted quad and a white ellipse behind
it) is not understood yet: probably merged nodes of the body pieces that are
drawn when they should be hidden.

**athenea's `.athz`, decoded in UE.** The decoder is athenea's
(`athz_decode.slangh`, `athz_tables.slang`), ported to HLSL as it is: a group
of 256 threads is eight runs of 32 lanes, each plane's rANS table in group
memory, the lanes renormalising from one stream of words through a mask, the
prefix transforms summed along a run, the parent transforms read from the
level above. Its output is a `.athc` chunk's arrays in the scratch, so the
splats then go through the version 3 decoders with one more encoding (4: the
words are in the scratch already), the transfer included. The pawn's top
(`top.athz`, version 3, 292 056 gaussians, 7 merged levels, 129 pages,
transfer of 112 values) against its USD source (`M_Pawn_Top_W.usdc`) under
the same sky: mean difference 0.0019 in a frame whose highlights reach 7; the
`.athz` carries the reflected field the USD here does not. The merged levels
are decoded only as the splats' parents.

## 13. Not done

- **Transfer (TX), past the opaque polish:** a thin wall measured against
  athenea's frame, a slope across a splat for the lobes that are not sharp (athenea's `slopeX`, `slopeY`), a fold's widening
  (its `bakeSpread`), a v2 file's curvature, analytic lights, the zonal transfer of skinned
  clouds, and a real-time sky capture.
- **`.athc` levels and streaming.** The merged levels, the per-frame cut and
  chunk streaming; the normals, emission and extras. In version 3 the
  curvature, the occlusion codebook, a zonal transfer, the skin and the
  compact tier; a paged tree's merges are decoded and hidden, not left out.
- **Other formats.** `.athv` (HEVC video), `.athl` (the web's light layers),
  an `.athz`'s layer K;
  an SPZ's coordinate-system extension; a glTF node's transform, more than one
  gaussian primitive, sparse accessors and `EXT_meshopt_compression`.
- **The ParticleField's transform** (a rotation would have to turn the
  harmonics).
- **Remaining upstream defects** from `DESIGN.md`: the ViewRect offset,
  the relit GBuffer not normalised by alpha, the importer's crash on an empty
  metallic/roughness array, transient buffers sized to `MaxSplats`, the sort's
  64 workgroups and 32 bits.
- **f16 storage** for the PLY and USD paths, and their conversion on the GPU.
- **Automated tests** in the plugin.

## The package BuildPlugin makes (8 Nov)

`RunUAT BuildPlugin` compiles the plugin without a unity build, each file
alone, where a project's build had merged them. `GSplatImporter.h` named
`class UGSplatAsset&` inside `namespace GSplatImporter`, which declares a new,
empty `GSplatImporter::UGSplatAsset`; merged, a file that had seen the real
class came first, and alone `GSplatSceneImporter.cpp` failed (C2664, C2027).
The header now declares `UGSplatAsset` at global scope before the namespace.

Past that, the build stopped on files that used types they never included,
which the project's shared precompiled header had brought in: the RHI's
(`RHIFwd.h`, `RHIResources.h`, `RHIFeatureLevel.h`), the render graph's
(`RenderGraphFwd.h`, `RenderGraphResources.h`), `FGlobalShader`
(`GlobalShader.h`, `ShaderParameterStruct.h`), `TAutoConsoleVariable`
(`HAL/IConsoleManager.h`), `FPaths` and the shader directory mappings
(`Misc/Paths.h`, `ShaderCore.h`), `TQueue` (`Containers/Queue.h`),
`FPackageName` and `UPackage` (`Misc/PackageName.h`, `UObject/Package.h`), and
`FSceneView`, declared where only a reference is named. In
`GaussianSplatInterface.h` the RHI types were the cause of the proxy's
`override` errors: undeclared, the interface's three index count methods were
dropped. Each file now includes what it uses, and the package builds.
