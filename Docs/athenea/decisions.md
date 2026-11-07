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
11. [Not done](#11-not-done)

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

## 11. Not done

- **Transfer (TX).** athenea's relighting (`splat_relight`) ported to the
  prepass, with UE's sky light as the sky. The largest piece left.
- **`.athc` levels and streaming.** The merged levels, the per-frame cut and
  chunk streaming; the normals, emission and extras.
- **Other formats.** `.athv` (HEVC video), `.athz` and `.athl` (the web's),
  `.spz`, `.splat`, `.sog`, glTF `.glb` with `KHR_gaussian_splatting`.
- **The ParticleField's transform** (a rotation would have to turn the
  harmonics).
- **Remaining upstream defects** from `DESIGN.md`: the ViewRect offset,
  the relit GBuffer not normalised by alpha, the importer's crash on an empty
  metallic/roughness array, transient buffers sized to `MaxSplats`, the sort's
  64 workgroups and 32 bits.
- **f16 storage** for the PLY and USD paths, and their conversion on the GPU.
- **Automated tests** in the plugin.
