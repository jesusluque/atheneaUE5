# athenea for Unreal: operations

Copyright (c) 2026 jesus luque.

How to build, install and use the plugin: what it needs, which files it
imports and what it reads from each, the settings it adds, what it prints
when something is wrong, and how to render and time it without a window.
The design is in `DESIGN.md`, the code in `development.md`, and every change
against upstream with what was measured in `decisions.md`.

## 1. Requirements

| What | Value | Why |
|---|---|---|
| Unreal Engine | 5.8.4 (launcher build) | the version it is built and tested against |
| Platform | Win64 | the only target built so far |
| RHI | DirectX 12, shader model 6 | the prepass binds 10 UAVs; SM5 allows 8 |
| Compiler | Visual Studio Build Tools 2022 (17.14) | what UE 5.8's UnrealBuildTool uses |
| .NET Framework SDK | 4.6 or later (4.8 installed) | UnrealBuildTool refuses to make its rules without it ("Could not find NetFxSDK install dir") |
| Windows Smart App Control | off | it blocks the unsigned DLL UnrealBuildTool compiles from the rules ("An Application Control policy has blocked this file") |
| Engine plugins it enables | Niagara, ProceduralMeshComponent, USDCore | the first two are upstream's; USDCore gives the OpenUSD the USD importer reads with |

USDCore in UE 5.8.4 bundles OpenUSD 0.26.3, which carries the
`UsdVolParticleField3DGaussianSplat` schema.

## 2. Building

The plugin is built as UE builds any plugin, into a package folder:

```bat
"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\RunUAT.bat" BuildPlugin ^
  -Plugin="<checkout>\StarLightGS.uplugin" -Package="<out>" -TargetPlatforms=Win64 -Rocket
```

- It takes about 20 seconds on a 10-core laptop once the engine's own
  intermediates exist.
- The package holds `Binaries`, `Content`, `Resources`, `Shaders`, `Source`
  and the `.uplugin`.
- Shaders are not compiled by the build. UE compiles them the first time a
  project opens with the plugin, and keeps them in the project's derived data
  cache.

## 3. Installing in a project

1. Copy the package folder to `<Project>/Plugins/StarLightGS`.
2. Enable the plugin in the project (`"Plugins": [{ "Name": "StarLightGS", "Enabled": true }]`).
3. Restrict the project's shader formats to SM6 in `Config/DefaultEngine.ini`:

```ini
[/Script/WindowsTargetPlatform.WindowsTargetSettings]
DefaultGraphicsRHI=DefaultGraphicsRHI_DX12
-D3D12TargetedShaderFormats=PCD3D_SM5
+D3D12TargetedShaderFormats=PCD3D_SM6
-D3D11TargetedShaderFormats=PCD3D_SM5
+D3D11TargetedShaderFormats=PCD3D_SM6
```

Without the last step, opening the project fails while compiling the global
shaders for `PCD3D_SM5`: "Shader is using too many UAVs: 10 (only 8
supported)".

## 4. Importing

Files are imported as any asset is: dropped on the content browser, through
**Import**, or from Python with an `AssetImportTask`. Each makes a
`GSplatAsset`, which a `GaussianSplatActor` (or a `GaussianSplatComponent`)
draws.

### 4.1 What each format gives

| Format | Extensions | Dialogue | Axes and units | Colour space | Harmonics | Kept as |
|---|---|---|---|---|---|---|
| 3DGS PLY | `.ply` | yes, unless unattended | the dialogue's swizzle and scale; an athenea PLY says them in its comment (4.2) | the dialogue's gamma option; an athenea PLY says it | `f_rest_0..44`, degree 3 | float arrays |
| USD ParticleField | `.usd` `.usda` `.usdc` | no | the stage's `upAxis` and `metersPerUnit` | sRGB unless `primvars:athenea:splat:linear` is true | `radiance:sphericalHarmonicsCoefficients`, degree 0 to 3 | float arrays |
| athenea chunked cloud | `.athc` | no | athenea's: y up, metres | sRGB unless flag bit 1 (linear) | the file's rest coefficients (0 to 15 a colour) | athenea's packing, decoded on the GPU |

A USD file is imported by this plugin only when its stage holds a
`ParticleField3DGaussianSplat` prim; every other stage is left to UE's USD
importer. The plugin's factory has a higher import priority than the default,
so a stage with splats comes here first.

### 4.2 PLY files athenea wrote

`athenea flatten` writes a comment in the PLY header, for example:

```
comment athenea flatten: degree 3, sRGB display-referred, y up, metres
```

The importer reads it, when the comment names athenea, before the dialogue
opens (and in an unattended import, which has no dialogue):

| Comment says | Import option set |
|---|---|
| `sRGB display-referred` | colour linearised after the harmonics are summed (`bApplyGammaCorrection`) |
| `y up` | swizzle (X, Z, Y): a right-handed, y-up file into UE's left-handed, z-up space |
| `metres` | scale 100 (centimetres) |

A PLY without that comment takes upstream's defaults: swizzle (Z, X, −Y),
which suits COLMAP's y-down captures, scale 100 and no gamma.

### 4.3 USD stages

What is read, from the stage's first `ParticleField3DGaussianSplat`:

| Attribute | Read as |
|---|---|
| `positions` | float3, the stage's units |
| `orientations` | quatf, or quath taken to float |
| `scales` | linear, the stage's units |
| `opacities` | linear, 0 to 1 |
| `radiance:sphericalHarmonicsDegree` | 0 to 3 (more is clamped to 3) |
| `radiance:sphericalHarmonicsCoefficients` | (degree + 1)² float3 a gaussian, the constant term first, in 3DGS's convention (colour = 0.5 + C0 · dc) |
| `primvars:athenea:splat:normal` | float3 a gaussian, kept when its length matches |
| `primvars:athenea:splat:linear` | true: the colours are linear light |
| stage `upAxis` | `Y` → (X, Z, Y); `Z` → (X, −Y, Z) |
| stage `metersPerUnit` | positions times it times 100 |

Values are read at the earliest time sample, so a cloud whose arrays are
animated imports its first frame. The prim's own transform is not applied;
the importer warns when it is not the identity, and the actor is to be placed
instead.

### 4.4 `.athc` files

What is read: the header, the chunk table, and from each chunk the positions
(float4: xyz in metres, linear opacity), the four shape words and the
harmonic words, copied unchanged into the asset. Versions 1 and 2 are read.

Not read yet: the merged levels (the file's levels of detail), the shading
normals (flag bit 0), the emitted radiance (bit 2), and the material,
transfer and curvature extras (bits 4 to 6). The cloud draws its finest level,
whole.

### 4.5 Messages

Errors and warnings go to the `LogGSplatEditor` category.

| Message | Meaning |
|---|---|
| `Failed to open USD stage: <file>` | OpenUSD could not open the file |
| `<file> has no gaussian ParticleField.` | the stage holds none |
| `a ParticleField needs one position, orientation, scale and opacity a gaussian.` | the four arrays are missing or of different lengths |
| `<n> harmonic coefficients for <m> gaussians at degree <d>.` | the coefficient array is not (d + 1)² a gaussian |
| `the ParticleField's transform is not applied; place the actor instead.` | warning: the prim has a transform |
| `This build has no USD SDK; <file> cannot be read.` | the plugin was built where USDCore has no SDK |
| `Cannot open <file>.` | the `.athc` cannot be opened |
| `no ATHC magic.` | not a `.athc` |
| `version <v>, this reads 1 and 2.` | a `.athc` version this plugin does not know |
| `a header that does not add up (...)` | the counts in the `.athc` header contradict each other |
| `the chunk table is cut short.` / `chunk <c> is cut short.` | the file ends early |
| `chunk <c> holds <n> splats, where <m> were due.` | a chunk's count differs from what the header implies |

On success each importer logs one line saying how many gaussians, the degree
or rest coefficients, and the colour space it took.

## 5. Asset settings

Shown on the asset under **Gaussians**:

| Setting | Editable | Meaning |
|---|---|---|
| `SplatCount` | no | gaussians in the asset |
| `bHasHighOrderSH` / `bUseHighOrderSH` | has: no; use: yes | whether the asset carries harmonics of degree 1 to 3, and whether they are drawn |
| `bLinearizeColor` | no | the colour is display-referred sRGB; the GPU applies the sRGB curve after summing the harmonics |
| `bPacked` | no | the asset keeps athenea's packing (a `.athc`); the float arrays are empty and the GPU decodes it |
| `bHasNormals` / `bUseNormals`, `bHasMetallicRoughness` / `bUseMetallicRoughness` | as above | upstream's relit inputs |

## 6. What is drawn

- **Unlit** (the path athenea's clouds take): colour from the base colour and
  the harmonics along the view, in the file's frame, clamped at zero,
  linearised when `bLinearizeColor`, multiplied by the view's pre-exposure,
  and blended "over" the scene before post-processing.
- **Relit** (upstream's): the base colour as albedo, written into UE's GBuffer
  where coverage passes 0.3. It does not use the harmonics.

## 7. Rendering and timing without a window

### 7.1 Shots

The editor renders with no window under `-RenderOffscreen`. A Python script
started through `-ExecCmds` keeps the editor alive; `-ExecutePythonScript`
quits as soon as the script returns, before any frame is drawn.

```bat
UnrealEditor.exe <Project>.uproject -RenderOffscreen -unattended -nosplash ^
  -ExecCmds="py C:/path/shot_level.py /Game/Level name" -stdout
```

The scripts used for the comparisons (`shot_level.py`, `setup_*.py`) wait
60 s for shaders, then call
`unreal.AutomationLibrary.take_high_res_screenshot(1280, 720, path, camera)`
from a Slate post-tick callback, and quit. The first launch of a project
compiles the engine's shaders and takes several minutes.

For a comparison with athenea, set these in `DefaultEngine.ini` under
`[/Script/Engine.RendererSettings]`: `r.DefaultFeature.AutoExposure=False`,
`r.DefaultFeature.Bloom=False`, `r.DefaultFeature.MotionBlur=False`,
`r.AntiAliasingMethod=0`. UE's tonemapper still applies.

### 7.2 Timing

The editor's viewport does not render under `-RenderOffscreen`, so a
`ProfileGPU` there measures only the UI. Frames are timed in `-game` mode,
from a camera actor whose `AutoActivateForPlayer` is player 0, with UE's CSV
profiler:

```bat
UnrealEditor.exe <Project>.uproject /Game/Level -game -RenderOffscreen -ResX=1280 -ResY=720 ^
  -windowed -unattended -nosplash -nosound -csvGpuStats -csvCaptureFrames=1200 ^
  -ExecCmds="r.GPUStatsEnabled 1,t.MaxFPS 0,r.VSync 0"
```

- The CSV lands in `%LOCALAPPDATA%\UnrealEngine\5.8\Saved\Profiling\CSV`.
- The splats are the `GPU/GaussiansUnlit` column (and `GPU/GaussiansLit` for
  the relit path); `FrameTime` and `GPUTime` are the frame's.
- `-csvExitOnCompletion` did not end the process in 5.8.4; the run is ended
  from outside once the CSV is written.
- The GPU must be free: on a shared machine, check `nvidia-smi` (and WSL,
  whose processes it does not list) before timing.

## 8. Unreal MCP (optional)

UE 5.8 ships Epic's experimental "Unreal MCP" plugin (`ModelContextProtocol`),
an HTTP server inside the editor (port 8000, path `/mcp`, local connections
only). Enabling it in the project with
`[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]
bAutoStartServer=True` in `Config/DefaultEditorPerProjectUserSettings.ini`
lets an MCP client drive the editor. Its own log states that what it sends to
a model counts as Licensed Technology under UE's EULA. Nothing in this plugin
needs it.

## 9. Licences

- The plugin is a fork of StarLightGS (MIT), and this fork's own work is
  under the same MIT licence: `LICENSE` keeps upstream's copyright line and
  adds this project's. Files added here carry that line in their header.
- No engine source is copied into the repository; the plugin includes UE's
  headers when it is built.
- OpenUSD is reached through UE's USDCore plugin, under OpenUSD's own licence
  (Tomorrow Open Source Technology License 1.0); the plugin redistributes none
  of it.
