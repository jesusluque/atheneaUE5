# athenea para Unreal: operación

Copyright (c) 2026 jesus luque.

Cómo compilar, instalar y usar el plugin: qué necesita, qué ficheros importa y
qué lee de cada uno, los ajustes que añade, qué imprime cuando algo va mal, y
cómo renderizar y medir tiempos sin ventana. El diseño está en `DESIGN.es.md`,
el código en `development.es.md`, y cada cambio respecto a upstream, con lo
que se midió, en `decisions.es.md`.

## 1. Requisitos

| Qué | Valor | Por qué |
|---|---|---|
| Unreal Engine | 5.8.4 (build del launcher) | la versión contra la que se compila y se prueba |
| Plataforma | Win64 | el único target compilado hasta ahora |
| RHI | DirectX 12, shader model 6 | el prepass enlaza 10 UAVs; SM5 permite 8 |
| Compilador | Visual Studio Build Tools 2022 (17.14) | lo que usa el UnrealBuildTool de UE 5.8 |
| .NET Framework SDK | 4.6 o posterior (4.8 instalado) | sin él, UnrealBuildTool se niega a generar sus reglas ("Could not find NetFxSDK install dir") |
| Windows Smart App Control | desactivado | bloquea la DLL sin firmar que UnrealBuildTool compila a partir de las reglas ("An Application Control policy has blocked this file") |
| Plugins del motor que activa | Niagara, ProceduralMeshComponent, USDCore | los dos primeros son de upstream; USDCore da el OpenUSD con el que lee el importador USD |

USDCore en UE 5.8.4 incluye OpenUSD 0.26.3, que trae el esquema
`UsdVolParticleField3DGaussianSplat`.

## 2. Build

El plugin se compila como UE compila cualquier plugin, en una carpeta de
paquete:

```bat
"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\RunUAT.bat" BuildPlugin ^
  -Plugin="<checkout>\StarLightGS.uplugin" -Package="<out>" -TargetPlatforms=Win64 -Rocket
```

- Tarda unos 20 segundos en un portátil de 10 núcleos una vez que existen los
  intermedios propios del motor.
- El paquete contiene `Binaries`, `Content`, `Resources`, `Shaders`, `Source`
  y el `.uplugin`.
- El build no compila los shaders. UE los compila la primera vez que se abre
  un proyecto con el plugin, y los guarda en la derived data cache del
  proyecto.

## 3. Instalación en un proyecto

1. Copiar la carpeta del paquete a `<Project>/Plugins/StarLightGS`.
2. Activar el plugin en el proyecto (`"Plugins": [{ "Name": "StarLightGS", "Enabled": true }]`).
3. Restringir los formatos de shader del proyecto a SM6 en `Config/DefaultEngine.ini`:

```ini
[/Script/WindowsTargetPlatform.WindowsTargetSettings]
DefaultGraphicsRHI=DefaultGraphicsRHI_DX12
-D3D12TargetedShaderFormats=PCD3D_SM5
+D3D12TargetedShaderFormats=PCD3D_SM6
-D3D11TargetedShaderFormats=PCD3D_SM5
+D3D11TargetedShaderFormats=PCD3D_SM6
```

Sin el último paso, abrir el proyecto falla al compilar los shaders globales
para `PCD3D_SM5`: "Shader is using too many UAVs: 10 (only 8
supported)".

## 4. Importación

Los ficheros se importan como cualquier asset: arrastrados al content browser,
mediante **Import**, o desde Python con un `AssetImportTask`. Cada uno crea un
`GSplatAsset`, que dibuja un `GaussianSplatActor` (o un
`GaussianSplatComponent`).

### 4.1 Lo que da cada formato

| Formato | Extensiones | Diálogo | Ejes y unidades | Espacio de color | Armónicos | Se guarda como |
|---|---|---|---|---|---|---|
| PLY 3DGS | `.ply` | sí, salvo en modo desatendido | el swizzle y la escala del diálogo; un PLY de athenea los indica en su comentario (4.2) | la opción de gamma del diálogo; un PLY de athenea lo indica | `f_rest_0..44`, grado 3 | arrays float |
| ParticleField USD | `.usd` `.usda` `.usdc` | no | el `upAxis` y el `metersPerUnit` del stage | sRGB salvo que `primvars:athenea:splat:linear` sea true | `radiance:sphericalHarmonicsCoefficients`, grado 0 a 3 | arrays float |
| nube por chunks de athenea | `.athc` | no | los de athenea: y arriba, metros | sRGB salvo con el bit de flag 1 (lineal) | los coeficientes rest del fichero (0 a 15 por color) | el empaquetado de athenea, decodificado en la GPU |

Este plugin importa un fichero USD solo cuando su stage contiene un prim
`ParticleField3DGaussianSplat`; cualquier otro stage queda para el importador
USD de UE. El factory del plugin tiene una prioridad de importación mayor que
la predeterminada, así que un stage con splats llega aquí primero.

### 4.2 Ficheros PLY escritos por athenea

`athenea flatten` escribe un comentario en la cabecera del PLY, por ejemplo:

```
comment athenea flatten: degree 3, sRGB display-referred, y up, metres
```

El importador lo lee, cuando el comentario nombra a athenea, antes de que se
abra el diálogo (y en una importación desatendida, que no tiene diálogo):

| El comentario dice | Opción de importación que fija |
|---|---|
| `sRGB display-referred` | color linealizado después de sumar los armónicos (`bApplyGammaCorrection`) |
| `y up` | swizzle (X, Z, Y): un fichero dextrógiro con y arriba al espacio levógiro con z arriba de UE |
| `metres` | escala 100 (centímetros) |

Un PLY sin ese comentario toma los valores predeterminados de upstream:
swizzle (Z, X, −Y), adecuado para las capturas de COLMAP con y abajo, escala
100 y sin gamma.

### 4.3 Stages USD

Lo que se lee, del primer `ParticleField3DGaussianSplat` del stage:

| Atributo | Se lee como |
|---|---|
| `positions` | float3, las unidades del stage |
| `orientations` | quatf, o quath llevado a float |
| `scales` | lineal, las unidades del stage |
| `opacities` | lineal, 0 a 1 |
| `radiance:sphericalHarmonicsDegree` | 0 a 3 (más se limita a 3) |
| `radiance:sphericalHarmonicsCoefficients` | (grado + 1)² float3 por gaussiana, el término constante primero, en la convención de 3DGS (color = 0.5 + C0 · dc) |
| `primvars:athenea:splat:normal` | float3 por gaussiana, se conserva cuando su longitud coincide |
| `primvars:athenea:splat:linear` | true: los colores son luz lineal |
| `upAxis` del stage | `Y` → (X, Z, Y); `Z` → (X, −Y, Z) |
| `metersPerUnit` del stage | posiciones multiplicadas por él y por 100 |

Los valores se leen en la primera muestra de tiempo, así que una nube cuyos
arrays están animados importa su primer frame. La transformación propia del
prim no se aplica; el importador avisa cuando no es la identidad, y en su
lugar se coloca el actor.

### 4.4 Ficheros `.athc`

Lo que se lee: la cabecera, la tabla de chunks, y de cada chunk las posiciones
(float4: xyz en metros, opacidad lineal), las cuatro palabras de forma y las
palabras de armónicos, copiadas sin cambios en el asset. Se leen las versiones
1 y 2.

Todavía no se lee: los niveles fusionados (los niveles de detalle del
fichero), las normales de shading (bit de flag 0), la radiancia emitida (bit
2), ni los extras de material, transferencia y curvatura (bits 4 a 6). La nube
dibuja su nivel más fino, completo.

### 4.5 Mensajes

Los errores y avisos van a la categoría `LogGSplatEditor`.

| Mensaje | Significado |
|---|---|
| `Failed to open USD stage: <file>` | OpenUSD no pudo abrir el fichero |
| `<file> has no gaussian ParticleField.` | el stage no contiene ninguno |
| `a ParticleField needs one position, orientation, scale and opacity a gaussian.` | faltan los cuatro arrays o tienen longitudes distintas |
| `<n> harmonic coefficients for <m> gaussians at degree <d>.` | el array de coeficientes no tiene (d + 1)² por gaussiana |
| `the ParticleField's transform is not applied; place the actor instead.` | aviso: el prim tiene una transformación |
| `This build has no USD SDK; <file> cannot be read.` | el plugin se compiló donde USDCore no tiene SDK |
| `Cannot open <file>.` | no se puede abrir el `.athc` |
| `no ATHC magic.` | no es un `.athc` |
| `version <v>, this reads 1 and 2.` | una versión de `.athc` que este plugin no conoce |
| `a header that does not add up (...)` | los recuentos de la cabecera del `.athc` se contradicen |
| `the chunk table is cut short.` / `chunk <c> is cut short.` | el fichero termina antes de tiempo |
| `chunk <c> holds <n> splats, where <m> were due.` | el recuento de un chunk difiere del que implica la cabecera |

Si tiene éxito, cada importador escribe en el log una línea que dice cuántas
gaussianas, el grado o los coeficientes rest, y el espacio de color que tomó.

## 5. Ajustes del asset

Se muestran en el asset bajo **Gaussians**:

| Ajuste | Editable | Significado |
|---|---|---|
| `SplatCount` | no | gaussianas del asset |
| `bHasHighOrderSH` / `bUseHighOrderSH` | has: no; use: sí | si el asset lleva armónicos de grado 1 a 3, y si se dibujan |
| `bLinearizeColor` | no | el color es sRGB display-referred; la GPU aplica la curva sRGB después de sumar los armónicos |
| `bPacked` | no | el asset conserva el empaquetado de athenea (un `.athc`); los arrays float están vacíos y la GPU lo decodifica |
| `bHasNormals` / `bUseNormals`, `bHasMetallicRoughness` / `bUseMetallicRoughness` | como arriba | las entradas del modo iluminado de upstream |

## 6. Lo que se dibuja

- **Sin iluminar** (la ruta que toman las nubes de athenea): color a partir
  del base colour y los armónicos a lo largo de la vista, en el marco del
  fichero, limitado a cero, linealizado cuando `bLinearizeColor`,
  multiplicado por la pre-exposure de la vista, y mezclado "over" la escena
  antes del post-proceso.
- **Iluminado** (el de upstream): el base colour como albedo, escrito en el
  GBuffer de UE donde la cobertura supera 0,3. No usa los armónicos.

## 7. Render y tiempos sin ventana

### 7.1 Capturas

El editor renderiza sin ventana con `-RenderOffscreen`. Un script de Python
lanzado mediante `-ExecCmds` mantiene vivo el editor; `-ExecutePythonScript`
sale en cuanto el script retorna, antes de que se dibuje ningún frame.

```bat
UnrealEditor.exe <Project>.uproject -RenderOffscreen -unattended -nosplash ^
  -ExecCmds="py C:/path/shot_level.py /Game/Level name" -stdout
```

Los scripts usados para las comparaciones (`shot_level.py`, `setup_*.py`)
esperan 60 s a los shaders, después llaman a
`unreal.AutomationLibrary.take_high_res_screenshot(1280, 720, path, camera)`
desde un callback post-tick de Slate, y salen. El primer arranque de un
proyecto compila los shaders del motor y tarda varios minutos.

Para una comparación con athenea, fijar esto en `DefaultEngine.ini` bajo
`[/Script/Engine.RendererSettings]`: `r.DefaultFeature.AutoExposure=False`,
`r.DefaultFeature.Bloom=False`, `r.DefaultFeature.MotionBlur=False`,
`r.AntiAliasingMethod=0`. El tonemapper de UE se sigue aplicando.

### 7.2 Tiempos

El viewport del editor no renderiza con `-RenderOffscreen`, así que un
`ProfileGPU` ahí mide solo la interfaz. Los frames se miden en modo `-game`,
desde un camera actor cuyo `AutoActivateForPlayer` es el jugador 0, con el
profiler CSV de UE:

```bat
UnrealEditor.exe <Project>.uproject /Game/Level -game -RenderOffscreen -ResX=1280 -ResY=720 ^
  -windowed -unattended -nosplash -nosound -csvGpuStats -csvCaptureFrames=1200 ^
  -ExecCmds="r.GPUStatsEnabled 1,t.MaxFPS 0,r.VSync 0"
```

- El CSV queda en `%LOCALAPPDATA%\UnrealEngine\5.8\Saved\Profiling\CSV`.
- Los splats son la columna `GPU/GaussiansUnlit` (y `GPU/GaussiansLit` para
  la ruta iluminada); `FrameTime` y `GPUTime` son los del frame.
- `-csvExitOnCompletion` no terminó el proceso en 5.8.4; la ejecución se
  termina desde fuera una vez escrito el CSV.
- La GPU debe estar libre: en una máquina compartida, comprobar `nvidia-smi`
  (y WSL, cuyos procesos no lista) antes de medir.

## 8. Unreal MCP (opcional)

UE 5.8 incluye el plugin experimental "Unreal MCP" de Epic
(`ModelContextProtocol`), un servidor HTTP dentro del editor (puerto 8000,
ruta `/mcp`, solo conexiones locales). Activarlo en el proyecto con
`[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]
bAutoStartServer=True` en `Config/DefaultEditorPerProjectUserSettings.ini`
permite que un cliente MCP controle el editor. Su propio log indica que lo
que envía a un modelo cuenta como Licensed Technology según la EULA de UE.
Nada de este plugin lo necesita.

## 9. Licencias

- El plugin es un fork de StarLightGS (MIT); `LICENSE` es el de upstream y se
  conserva. Los ficheros añadidos aquí llevan la línea de copyright de este
  proyecto.
- No se copia código fuente del motor en el repositorio; el plugin incluye
  las cabeceras de UE cuando se compila.
- A OpenUSD se llega a través del plugin USDCore de UE, bajo la propia
  licencia de OpenUSD (Tomorrow Open Source Technology License 1.0); el plugin
  no redistribuye nada de él.
