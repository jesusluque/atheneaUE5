# athenea for Unreal: design

Copyright (c) 2026 jesus luque.

A fork of StarLightGS (CoSTAR National Lab, MIT, upstream 8720289) that plays
athenea's gaussian clouds in Unreal Engine 5.7+. The upstream licence stays in
`LICENSE`; files written here carry the header above.

## What upstream gives

- An asset of float32 arrays per splat (position, quaternion, linear scale,
  DC colour + opacity, optional normal and metallic/roughness), one immutable
  structured buffer each, 56 B a splat unlit and 76 B relit.
- A frame per view:
  1. a clipping cull, run only when something changes;
  2. a prepass compute (frustum, EWA projection, HZB) appending into one buffer
     for every visible component;
  3. one global 32-bit radix sort on view z;
  4. one indirect draw of instanced quads.
- **Unlit splats** blend "over" after UE's translucency, before TSR and the
  tonemap.
- **Relit splats** accumulate front to back into four RGBA16F targets. A
  composition pass then writes UE's GBuffer where coverage passes 0.3, so they
  are alpha-tested, opaque surfaces.
- **Shadows** are the splats drawn again through a vertex factory into virtual
  shadow maps, opaque above opacity 0.9, at the full count for every light.

## What athenea needs from it

athenea's product is raster playback of transfer (TX) clouds, translucent and
sorted. A TX cloud does not carry its radiance: it carries what the frame
needs to light it under any sky and any light (docs/decisions.md, "A transfer
right under any sky and any light"). The frame recombines direct and indirect
transfer (degree 3) with the sky's harmonics, narrows each reflection by the
open-direction bits (`shadowBits`), shows the reflected field where they are
closed, and evaluates the material's lobes against the prefiltered sky. In
order:

1. **Baked colour, view dependent.** Done (7ed3cf4).
   - Harmonics of degree 1 to 3 are imported and evaluated in the prepass
     along the view, in the file's frame.
   - athenea's PLY comment gives the axes and the colour space; colour is
     linearised after the sum and multiplied by `View.PreExposure`.
   - Matches athenea's raster of the same file from the same cameras, short
     of UE's tonemapper.
   - Still float, 180 B a splat; f16 comes with the USD reader.
2. **TX playback.** `splat_relight` ported to the prepass as a permutation
   beside `ENABLE_RELIGHTING`, with UE's sky light as the sky: its captured
   cubemap projected to degree-3 harmonics and prefiltered by roughness on the
   GPU, once a capture.
3. **Import of athenea's clouds**, as a factory beside the PLY one.
   - The interchange is athenea's own export: a USD ParticleField with
     `primvars:athenea:splat:*`. UE's USD importer knows neither the
     ParticleField schemas nor these primvars, so the plugin's factory reads
     the stage itself through the OpenUSD that UE bundles, by attribute name,
     without depending on that version carrying the ParticleField schemas.
   - It carries scale in the stage's units; no hard-coded x100.
   - Decoding to the buffers happens on the GPU where it is arithmetic on
     data. The CPU only reads the file.
4. **Levels of detail and streaming:** athenea's merge tree (`lodTree`) and
   the per-frame cut. This comes later; upstream has a hard 5M a view and 8.39M
   a component.
5. **Relit mode from exact materials:** mesh2splat's base colour, normal,
   roughness and metal, through upstream's GBuffer path. Shadows from athenea's
   translucent cloud shadow maps would replace the opaque VSM blobs.
6. **Composition with UE meshes:** depth, and athenea's ground shadow catcher
   as gaussians.

## Upstream defects to fix first

- **Out-of-bounds appends:** the append past `MaxSplats` writes out of bounds
  (only `start >= capacity` is checked). DX12 drops these writes; Metal and
  Vulkan do not.
- **Shadow vertex bound check:** `Index > count` should be `>=`.
- **No exposure:** unlit colour ignores `View.PreExposure`.
- **ViewRect offset:** views with a non-zero ViewRect offset (split screen,
  stereo) rasterise at the wrong place.
- **Relit GBuffer not normalised:** values are not divided by accumulated
  alpha.
- **Importer crash:** writes into an empty metallic/roughness array when it
  exists but is unchecked.
- **Transient buffers:** sized to `MaxSplats` (about 340 MB at 5M), not to the
  real count.
- **Sort:** limited to 64 workgroups, and always sorts all 32 bits.

## Where it runs

A Windows laptop with an RTX 3070 Laptop GPU (8 GB), UE 5.8.4 from the Epic
launcher, VS Build Tools 2022 17.14. The plugin is built with `RunUAT BuildPlugin`, and frames are
rendered headless with `-RenderOffscreen`. They are judged against athenea's
path tracer on the pawn and the Corvette, whole frames.

## Open questions

- A flat binary written by athenea, only if reading the stage proves slow.
- Whether TX splats should sort together with UE's own translucency, rather
  than after it.
