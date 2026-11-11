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
14. [Levels of detail](#14-levels-of-detail-9-nov)
15. [Media of gaussians](#15-media-of-gaussians-9-nov)
16. [Sequences](#16-sequences-9-nov)
17. [The rest of athenea's gaussians](#17-the-rest-of-atheneas-gaussians-11-nov)

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
  athenea's frame, a fold's widening of the coat, a v2 file's curvature, a
  light's cone over its size (§17), and a real-time sky capture.
- **`.athc` streaming.** Chunk streaming (the levels are cut every frame,
  §14, but every chunk is in the asset); the normals and extras. In
  version 3 the occlusion codebook, the levels of a skinned cloud and the
  compact tier (the merged levels of kind 0, the zonal transfer and the skin
  are read, §17).
- **Other formats.** `.athv` (HEVC video), `.athl` (the web's light layers),
  an `.athz`'s layer K;
  an SPZ's coordinate-system extension; a glTF node's transform, more than one
  gaussian primitive, sparse accessors and `EXT_meshopt_compression`.
- **The ParticleField's transform** (a rotation would have to turn the
  harmonics).
- **Remaining upstream defects** from `DESIGN.md`: transient buffers sized
  to `MaxSplats`, the sort's 64 workgroups and 32 bits. (The ViewRect offset,
  the relit GBuffer's normalisation and the importer's metallic/roughness
  crash are fixed, §17.)
- **f16 storage** for the PLY and USD paths, and their conversion on the GPU.
- **Automated tests** of the rendering: the importers' are in (development
  §7); the frames are still compared by hand against athenea.

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

## 14. Levels of detail (9 Nov)

**The cut.** A version 1 or 2 `.athc` keeps merged levels: each level's groups
follow the splats in the asset with two words a gaussian (a group's octree
cell and level, a splat's finest-level group), and the culling pass decides
per gaussian as athenea's `lod_cut.slang` does: a group is drawn where its
cell spans at most `LodPixels` pixels at its nearest point to the eye and its
parent's does not, a splat where its finest group's cell spans more. A
version 3 file's paged tree is cut as athenea's `lod_tree_cut.slang` cuts it:
the parent of each node is named at the import from the pages' `TREE` words,
and a node is drawn where it is a leaf or narrow enough on screen and its
parent is not. The cut is per primitive and per view: the index buffer it
writes is the proxy's, but each view culls and draws before the next, so two
scene captures of pawn.athc in the same frames, at one and at sixteen times
the framing distance (LodPixels 8), each match the same view drawn alone (RMS
3e-5 and 1.5e-8).

**The kernel.** A merged group carries an opacity of 5: Spark's harder kernel,
which athenea draws as `1 − (1 − g)^A`. Drawn as five times a gaussian, its
alpha passed one in the premultiplied blend, which brightened its rim and
darkened the splats behind it: a far cut came out a lattice. The pixel shader
now draws athenea's falloff and the quad is widened by 0.7 (D − 1)
deviations; a flat surfel pays its dilation back by area (athenea's
`surfelPaid`).

**Measured** (`r.Gaussians.Athenea.LogSplats`, a cloud's splats sent to the
rasteriser; image RMS against the cut off, 768 px): pawn.athc (325,833
gaussians, 8 levels) at 1 px draws 121,241 at four times the framing distance
(RMS 0.006) and 8,725 at sixteen (0.005), 37 times fewer; the first body cloud
of the pawn web scene (paged tree, 891,383 leaves) 279,890 and 10,394
(0.013, 0.011), 86 times fewer.

**An `.athz`.** Its merged levels were decoded only as the splats' parents.
They are now drawn: each level is a block after the splats, read from where
the decode left it in the scratch, and a level's CPCA transfer is expanded by
a head of its own, so a merge is relit as a splat is. The cut's words cannot
come from the CPU here (the tails are rANS-coded), so a pass writes them on
the device from the decoded tails. pawntop.athz at 1 px: 17,243 of 292,056
splats at sixteen times the distance, RMS 0.0028.

**A web scene split in files.** The pawn web scene's body is two `.athc`
files whose leaves complete each other; each is an asset, and an import from a
script saves only the first unless the rest are saved too, which made the body
look torn.

**The splat budget.** The frame's buffers hold `MaxSplats` splats, and the
prepass appends cloud after cloud: past the end, the clouds drawn last lost
their splats. A fly-in over the 106 M-gaussian town (four parts) lost a whole
part for three seconds while crossing the cliff, and got it back as the view
came down. Each view now keeps four words on the device: the prepass adds what
it wanted before the end cut it, and at the next frame's cut a one-thread
kernel scales the threshold by the square root of the excess over 85 % of the
capacity (a cut's splats go as the inverse square of its threshold), easing
back 3 % a frame below 60 %. The CPU only names the view's buffer. Measured on
the same flight, `LodPixels` 2 and 12 M, in `-game` (`LogSplats`): no frame
reached the cap (most 10.8 M); the factor peaked at 2.82 over the cliff and
stayed 1 high up and over the roofs. The first frame of a view that
overflows still drops splats; a cloud without levels of detail cannot be
coarsened and is still cut short.

**The flight's frame rate** (1920 × 1080, RTX 3070 Laptop, frames per
wall-clock second counted over whole bursts, medians of each stretch): 80
from high over the sea, 18.7 crossing the cliff with the whole town in view
(GPU about 60 ms: raster 39, sort 5.7, prepass 5.5, culling 4.3), 31.9 over
the roofs. With the faint splats' quads sized by their opacity and the cut
staggered, the same flight: 77.7, 17.1 and 36.4. With `MaxSplats` 6 M the
cliff's GPU frame falls from 61 to 36 ms, the far town drawn as coarse round
merges. With no window, `FrameTime` is not the
frame's duration (operations §7.2).

**Front to back** (10 Nov). Back to front, every gaussian of every layer is
blended, and the pixel shader wrote each splat's depth, so no fragment was
dropped before it was shaded. `r.Gaussians.Athenea.FrontToBack` N draws the
unlit splats nearest first into a buffer of their own, each under what is
there, with the quad at its splat's depth; the sorted splats go in N batches,
each twice the one before, and after each a pass over the screen closes the
pixels that reached 0.996 of opacity. The image is the back-to-front one:
mean difference 0.04/255 on the flight's frames, 0.01 % of pixels past 2/255.
The same flight, GPU frame, back to front against 8 batches: 12.1 and 9.2 ms
high over the sea, 45 and 30 crossing the cliff, 37 and 21 to 24 over the
roofs (22 to 33 and 26 to 41-46 frames a second). 12 and 16 batches measured
as 8. A second target for the velocity costs about 5 ms of the cliff's frame
whatever its format (the nearest depth in R32F, or the velocity in RGBA16F),
so it is written only under a temporal anti-aliasing, the one thing that
reads it. 8 batches is the default; the plugin's automation tests pass with
it. Not done: the relit path already draws front to back, without the
batches.

**The cut by the frustum** (`LodFrustum`, 10 Nov). The culling pass cut the
levels and nothing else, so the prepass walked every gaussian of the cut,
on screen or not. The cut now also leaves out the cells the view's four side
planes do not reach, as athenea's budgeted cut does. The flight with it,
front to back: the same 9.2 ms high up, 28 against 30 to 32 over the cliff,
20 against 25 coming down, 11.4 to 12.4 against 21 to 22 over the roofs (78
to 92 frames a second). The frames are the uncut ones at street level (no
pixel past 2/255) and to 0.05 % over the cliff; with a cell grown by one
edge instead of an edge and a half, near splats at the frame's lower edge
were lost (0.35 to 0.5 %). The vertex factory's shadow passes draw the same
index list, so by default only a cloud that casts no shadow is cut this way.

**The groups judged once** (`LodGroupState`, 10 Nov). The culling pass
cost 4.3 ms in every view of the town: each of 106 M splats read its
finest-level group's cell and judged it, the same answer for every splat of
a group. A pass over the groups (`GSLodStateCS`, as athenea's `lodCutFinest`
keeps its state) writes a word a group, and a splat reads its group's. The
flight, with the frustum cut: 7.9 against 9.2 ms high up, 26.8 against 28
over the cliff, 9.5 against 11.5 over the roofs (107 frames a second); the
same frames.

**The cut by its groups** (`LodGroupRuns`, 10 Nov). A part of the town is
26.6 M splats in 483 thousand finest-level groups, and a `.athc`'s splats
lie in Morton order: a group's are one run, the runs in the groups' order.
Where each begins is read once from the cut's words when the resource is
made (anything else found there leaves the runs unused), and where no
clipping volume culls the cloud the pass walks the groups alone
(`GSplatCullingGroupsCS`): a drawn group takes room for its run with one
atomic add and writes it. The culling pass: 4.3 ms a view, 3 with the groups
judged once, under 1 so. The flight with the frustum cut: 6.1 ms high up
(165 frames a second), 24.5 over the cliff, 8.5 over the roofs; the same
frames (no pixel past 2/255). Not done: an `.athz`, whose words are written
on the device, and a paged tree.

**The splats thinned** (`LodThin`, 10 Nov). With the eye moving, the place
where a part's splats give way to its finest merged level showed as a line
across the ground, and each group's turn as a pop: the cut's last step is its
largest, one merged gaussian for some 55 splats in this capture, and a
staggered threshold only spreads where it happens. Drawing the splats toward
their group (`LodMorphSplats`) removed the line and softened the near ground
(28 % of a frame's edge energy at 2, 47 % at 4). Thinning them does not:
while a group's cell spans under `LodThin` thresholds a fraction of its
splats is drawn, halving with each equal step of log r down to a
thirty-second, chosen by a hash of the splat. Those drawn carry the rest's
mass. All of it by width smeared a far meadow's long splats over the
horizon; by width and an opacity past 1 they stood as shards (athenea, at a
thirty-second). So: a little by width, the rest by opacity up to 1, and over
the band's last half the survivors are drawn toward the group's merged
gaussian, which they are at the switch. At a band of 4, the descent of the
flight: a frame's edge energy 24.6 against 19.9, 21.3 against 17.6 over the
cliff, houses where merges stood across the town, 3 % brighter. It also
draws fewer splats where the budget was the limit: the flight's descent 9.5
against 27.6 ms, the cliff 12.8 against 26.7, 5 high up; over the roofs 13.7
against 8.9 (a splat's thinning is one more thing the prepass asks). A splat
going out one frame to the next made the band flicker: it fades over the last
30 % of the fraction before its turn. And the budget's factor, which rose by
whole steps and eased back 3 % a frame, moved every group's fraction at once:
it now moves 1 % a frame, up over nine tenths of the capacity and down under
three quarters, and only a frame that wanted more than there is room for is
answered at once. On by default (4), with `LodMorph` 2 for the merged levels.

What was left of the band's flicker was measured with the view held still
(`hold_sequence.py`) and the threshold swept (`LodSweep` 1, no budget,
`LodPixels` 3), so that a frame differs from the next only by what the cut
switches; half the second difference in time below the sky, pixels past
4/255. Thinned, 0.78 %; with no thinning, 0.10 %. A group left with no splat
or with three at the switch was not it: the survivors given the merged
gaussian's opacity split among them measured the same, 0.80 %, and a group
ending with exactly one splat (a fraction down to 0.3 / n) thinned the band
so fast that houses stood as blobs, 1.21 %. It was the opacity: over 1 a
gaussian is the harder kernel and is drawn without what a small one pays
back, and a thinned splat on its way to a merged gaussian of opacity over 1
crossed it as the fraction moved, each crossing a jump. A thinned splat now
stays at 1 at most (or at its own, if that was over): 0.13 %, and the dark
hard blots it left across the band are gone. With it a splat's turn is no
longer a hash but its place in its group's run by the golden ratio
(`LodThinTurn`): a group keeps its fraction of splats to one more or less,
spread along the run, and its first splat is never dropped, so no group
stands empty before the switch. Where a cloud's splats are not in their
groups' runs the hash stays. athenea takes the same turns: its `starts` are
these runs. The flight's GPU frame with it, before and after (3070, 1080p):
6.6 and 6.8 ms high up, 10.0 and 11.0 on the descent, 13.2 and 13.4 over the
cliff, 15.2 and 15.6 at its worst, 13.5 and 13.1 at street level -- a splat
asked for its turn reads where its group's run begins.

**The wide view's shimmer** (10 Nov). Seen whole from afar with no
anti-aliasing, the town flickers from frame to frame as the eye moves. Half
the second difference in time, |f(t) − (f(t−1) + f(t+1))/2|, over 48 frames
of the flight's first seconds, pixels past 4/255: 1.69 % at `LodPixels` 2;
2.24 % with the threshold swept as well (`LodSweep` 1), so three quarters of
it is not the levels switching but sub-pixel gaussians aliasing. What calms
it, with the edge energy of a frame beside (22.9 as it is): `LodPixels` 3,
1.21 % and 23.0; `LodPixels` 4, 0.92 % and 20.8; `Dilation` 0.6, 1.31 % and
20.8; `Dilation` 1.0, 1.03 % and 18.9; the engine's TSR, 0.61 % and 18.2.
`LodPixels` 3 is the one that costs nothing, and it draws fewer splats: 9.3
against 17.6 ms coming down, 18.5 against 22.5 over the roofs, the cliff the
same (the budget's 8 M either way).

**The cut morphed** (`LodMorph`, 10 Nov). athenea's `--lod-morph`, the same
arithmetic in the prepass: a gaussian a fraction of the way to its parent
group as the parent nears its threshold. On the town it did not lower the
flicker above (2.26 to 2.42 % swept, at 1.15 and 1.5) and a band of 1.5 took
15 % of a frame's edge energy over the cliff. With the splats thinned it is
the merged levels' alone (2 by default). A group's parent
is found by its cell in the level above, each level's run of groups read once
from the cut's words when the resource is made.

## 15. Media of gaussians (9 Nov)

A ParticleField marked `primvars:athenea:splat:volume` (`athenea vdb2splat`)
is a medium: per gaussian a sigma and a temperature, its albedo in the colour's
constant term, and the medium's constants read from the stage's primvars
(`densityScale`, `albedo`, `volume:emission`, `volume:emissionScale`,
`volume:temperatureScale`, `volume:temperatureOffset`). Each gaussian is drawn
with the alpha 1 − exp(−τ g): τ = densityScale σ √(2π) / √(rᵀ Σ⁻¹ r), its
optical depth along the ray through its centre, from the projection's own
covariance, and g the image gaussian; it is carried to the pixel shader as a
negative opacity. Its colour is athenea's: albedo times the light it scatters
and (1 − albedo) times the glow, the glow athenea's black body (Planck against
the CIE 1931 fit, at luminance σ_SB T⁴ / π · 10⁻⁶), computed per gaussian in
the prepass.

**Its shadow on itself.** athenea reads its cloud shadow map at each
gaussian's centre. Here each frame the medium's extinction goes into a 64³
grid over the primitive's bounds (fixed point, atomics, a gaussian out to
three deviations sampled with at most nine steps an axis), and each centre
marches to UE's directional light and to five directions of the sky above; the
scattered light takes athenea's four octaves (Wrenninge). Measured with
`ue_time.sh` on the EmberGen mid-air explosion playing at 24 fps (20,000
gaussians, 1280 × 720, medians of 300 frames): the shadow 0.94 ms, the raster
4.3 ms, the GPU frame 11.2 ms with it and 11.9 ms without (the same within the
runs' spread).

**Against athenea.** The fireball's frame 40 has the same shape as athenea's
raster of it; the brightness differs with the lighting (UE's studio sky
against athenea's dim dome and key), and there is no anisotropic phase or
shadow between the medium and other clouds.

## 15.1 GPU memory a gaussian (9 Nov)

Every format is decoded on the device into the buffers the rasteriser reads:
position, scale, rotation, colour and opacity (56 bytes a gaussian) and the
45 rest coefficients. Those were 45 floats (180 bytes) for every gaussian,
allocated even for a cloud with none. They are now halves, two a word and 46
a gaussian (a pad, so that no word holds two gaussians' and a thread writes
its own words alone), 92 bytes, allocated only where the cloud draws any; a
PLY's or a USD stage's floats are packed on the device (GSPackSHCS). So the
bicycle (6.1 million gaussians, no rest coefficients) goes from 1.44 GB to
0.34 GB and the garden (5.8 million, degree 3) from 1.37 GB to 0.86 GB. The
pictures are the same: RMS 3e-4 (garden PLY), 1.3e-6 (bonsai), 1.2e-4 (the
garden's .athc) against the floats. Still per frame: the prepass's transient
buffers at r.Gaussians.MaxSplats (about 68 bytes a visible gaussian).

## 16. Sequences (9 Nov)

A medium from `vdb2splat` is a file a frame. A component holds a list of
assets and switches the one it draws as time passes (in the editor's
viewports too); each frame is its own asset and its own GPU resource, so the
switch re-creates only the proxy. Seventy-two frames of 20,000 gaussians are
about 90 MB of USD. Playing the 96-frame mid-air explosion at 24 fps in
`-game` under a 30 fps cap, the frame time over 300 frames is 33.3 ms median,
34.6 ms p99 and 35.1 ms at most: the switches leave no stall. Streaming a sequence
from disk, rather than holding it whole, is not done.

## 17. The rest of athenea's gaussians (11 Nov)

**A cloud a skeleton carries.** Where athenea's own stage keeps the joints'
transforms at each time code (`skinningXforms`, written by `mesh2splat
--skinned`), the asset keeps them too, after the bind transform (C = X G, three
rows a joint a pose): the sparrow's 61 poses are a few hundred kilobytes, and
nothing ties the cloud to a UE Skeleton, which a ParticleField cannot be bound
to. The bind pose stays in the file's space and the pass writes the component's
(GSSplatData.ush's mapping for a packed cloud), so the kernel is athenea's
line for line and its output is what every pass already reads. It runs once a
view family, before the shadows; a pose already drawn is not posed again.
Between two samples the rows are blended linearly: for the 30 samples a second
the conversion writes, a joint turns a few degrees between them. The asset's
pose is shared by the components that draw it.

**The zonal transfer** is rebuilt for each gaussian from the rotation it has
when its colour is read -- posed, for a skinned cloud -- into the nine
harmonics the other readers take; it has no indirect half (athenea writes
none for a skinned cloud), so a zonal cloud has no field and no cells either.

**Parts' visibility.** The read is athenea's: a part does not shadow itself,
and each other part's field is read where the gaussian would stand had that
part not moved, the inverse of its joint's pose, made on the processor once a
pose (twelve 3 x 4 inverses). Its two factors are kept a gaussian beside the
posed cloud, written by the skin pass toward the scene's first directional
light, so they cost nothing where the pose and the sun hold still.

**Lamps.** A layer is packed at import (RGB9E5 times 2^14, athenea's
`lightLayerPack`) so the asset is a word a group a gaussian; the groups'
weights are sixteen float4 a draw, set by a render command, so dimming a
lamp re-creates nothing.

**The edit and emission** are athenea's arithmetic in the prepass: the edit's
filters and box on the splat as stored, its grade after the lamps.

**Local lights** on a TX cloud take UE's attenuation rather than athenea's
lights, so a gaussian next to a mesh is lit as the mesh is; a rect is taken
from its centre, and a light's shadow is the gaussian's own open cells along
it (one direction, bilinear), not athenea's cone over the light's size.

**Shadow catchers** read the sky's harmonics over every open cell, not the
prefiltered map athenea reads for each: the horizon under a body is a little
softer. The faintest shadow (opacity below 1/255 at the gaussian's centre) is
cut as any splat is, where athenea draws a catcher whole.

**Upstream defects.** A splat's `Mean` is in the render target's pixels, as
`SV_Position` is, so a view whose rect does not start at the corner draws it
where it belongs; the relit route's GBuffer is read divided by its
accumulated alpha (a mean, not a sum); the PLY importer writes metallic and
roughness only where the array was sized for them.

**Slopes and the bake's spread.** Where a TX cloud keeps its curvature, a
lobe too rough for the sharp route (the polish over 0.5, the coat over 0.2)
changes across the splat by the sky read along the mirrors a pixel right and
a pixel down, two rgb differences carried in the sharp lobes' payload under a
mark and added by the pixel shader (athenea's `SplatSlope`). A sharp lobe
reads the sky at the roughness before the bake widened it by the surface's
turn across the gaussian (`bakeSpread`, the largest principal curvature times
the widest size; `unwidened`), since its mirror already turns per pixel.

**Cryptomatte** is athenea's 32-bit id a gaussian: an ID view (its three low
bytes as a colour) and a matte a component; there is no multi-layer
Cryptomatte EXR (Movie Render Queue's ID pass is what is rendered).

**Translucent shadows** are stochastic: the vertex factory hands the shadow
material opacity times its clip value over a per-pixel uniform number, so a
pixel passes the mask as often as the gaussian is opaque there, with no
change to the material. Behind a console variable, since every gaussian then
enters the shadow depth passes (a capture's millions where only the opaque
few did).

**Surfels** are drawn from each pixel's ray, as athenea's `splat_blend`
draws them: the projection pass writes the disc's two wide axes as
homogeneous pixel directions over the centre's depth (six halves) and its p,
so the pixel shader needs only the centre it already has. On a fixture of six
discs turned 0, 50 and 75 degrees, hard and not, the frame matches athenea's:
mean error 0.008, 33.6 dB (8-bit sRGB, the worst pixel 0.145 on the 75
degree disc's edge). A surfel's sample under 1/255 is cut, as athenea's
blend cuts it: its quad, widened for the disc's perspective, reaches past
where a gaussian's ends, and without the cut its tails showed. The check's
capture writes an sRGB target with no tone curve and 0 EV; an 8-bit linear
target had made every UE frame look darker than athenea's, with the same
light (the disc's centre 0.618 0.263 0.121 against 0.618 0.266 0.125). A hard surfel's p is read from USD;
a `.athc`'s, carried in its flat axis's logarithm, is not decoded yet.

A **version 3 file's merged levels** are read as a version 2 file's: after
the chunks, cut from two words a gaussian. Those words are written on the
device from CORE's tails (`GSAthc3LodCS`), since CORE may be byte planes or
deltas the CPU does not undo; each block's row carries its level in the word
after its sections. Checked by `Athenea.Import.Athc3Levels` (a 9-level file:
the counts and the finest level's place) and on a TX body at `LodPixels` 0, 1
and 40, whose coarse merges stand where its parts are. A skinned file keeps
its splats alone, as before.

**Frame tests.** `Athenea.Frame.Surfels` and `Athenea.Frame.SparrowSky`
draw a fixture and compare it on the device with athenea's frame
(`GSCompare.usf`: a histogram of 8-bit sRGB steps, a reference's alpha
masking what athenea drew over a background UE does not have). The surfels
match (mean 3.5 steps, p99 28). The TX sparrow under the studio sky -- the
sky light turned 180 degrees, which is athenea's dome turned up -- matches
to mean 10.4 steps, p99 45; over the bird UE's linear means are 0.95, 0.95
and 0.94 of athenea's, and the albedo the two decode is the same to 2 %. A
first reference drawn with its dome seen behind the bird read a fifth
brighter: the dome showed through the feathers' partial cover, where UE's
background is black; it is drawn with the dome lighting and not seen
(`athenea:domeVisible` off, through `--render-settings` and a camera prim).
A zonal gaussian's normal is now its posed third axis, as athenea's
transferFrame takes it, not the file's rest normal. athenea draws 81,785 of
the file's 124,819 splats, UE all of them.

**An `.athz`'s layer K** (athenea's `readAthzSkinned`): its pages' fourth
layer is decoded with the splats -- four (joint, weight) float pairs and
three gradient words a splat -- and `GSAthzSkinCS` lifts them into the
skin's buffers (joint | weight unorm16 << 16, the gradients as they are);
the frames' block -- the geometry's bind transform, each frame's time code,
each joint's matrix in the skinner's column layout, three rows in halves --
becomes the rows the USD reader makes, C = X G. The file keeps time codes
and no rate: they are taken at 30 a second. The splats are the first
elements and the merged groups have no influences, so a skinned cloud's cut
draws its splats alone (threshold 0, no thinning or morph), as athenea's does:
it writes zeros for a merged group's influences ("nothing skins them"), and
its answer for a cloud that moves is a discrete level of detail, the cloud
converted again at a coarser cell with its own rig (`mesh2splat
--lod-levels`), which this does not read yet. Checked on the
sparrow's `.athz` (1,504,096 splats, 609 joints, 61 poses): the flight's
pose at 0, 0.5, 1 and 1.5 s is the USD sparrow's.

**Timings** (RTX 3070 Laptop, 1080p, `-game`, medians of 300 frames,
`Tools/athenea/ue_time.sh` on `timing_level.py`'s levels in a test
project; alternated runs). Surfels drawn from the ray cost nothing
measurable: the 421,911-splat v3 body with its levels, raster 0.460 / 0.460
ms on against 0.470 / 0.474 off, the prepass 0.486 / 0.458 against 0.471 /
0.478. The sparrow's `.athz` playing and held still both take 52 ms of GPU
a frame (prepass 18.5, sort 8.7, raster 10.0): its skin pass is under the
noise, and the cost is its 1,504,096 splats drawn whole -- a skinned cloud
has no levels -- and their zonal transfer rebuilt each frame. The town's
flight (TestProj, 1920 × 1080, frames 60 to 1400 of 1500, medians, each run
started under 72 C, alternated): 1.2 took 12.71 ms of GPU a frame; 1.3 takes
14.00 (13.38 to 14.47 over five runs), 12.35 with
`r.Gaussians.Athenea.Surfels 0`. The difference is the town's merged levels
drawn as surfels: a packed cloud with levels counts as holding surfels,
since athenea writes a flat merge as one, and the disc's record costs the
prepass 0.6 ms to write and the raster 0.5 ms to carry (a `uint4`
interpolant). The draw shaders are compiled with that interpolant only
where a cloud drawn holds surfels (`GS_SURFELS`), so a cloud without any
pays nothing; surfels 0 trades the merged discs' correct edges for that
time. The frames are unchanged by the permutation (surfels 3.49 steps,
sparrow 10.44).

The compact tier (`CMPC`, tier 4 of a version 3 file) is the web player's
phone path -- 56 bytes a node, joint CPCA bases and shadow dictionaries, so a
phone draws a relit page without tiers 1 to 3 -- and is not read: on a
desktop GPU the plugin has the tiers it replaces.

Neither `.athz` nor `.athc` records its up axis, and athenea's sparrow was
written from a z-up stage: the import option `bFileZUp` maps such a file to
(X, −Y, Z) instead of the y-up (X, Z, Y). It is the importer's swizzle alone
-- the skin, the zonal frames and the sky are evaluated in the file's frame
and turned by its axes -- so nothing else changes.

A `.athc` carries no hardness: athenea's writer (`lod/Athc.cpp`) keeps none,
so a hard surfel reaches the plugin only through USD
(`primvars:athenea:splat:hardness`, read).

Not done here: chunk streaming and `.athv`.

