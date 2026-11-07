# athenea para Unreal: desarrollo

Copyright (c) 2026 jesus luque.

Cómo está organizado el código del plugin, qué hace upstream y qué añade este
fork, las convenciones que respetan todos los importadores y shaders, cómo
añadir un formato, y cómo se comprueba un cambio. El uso del plugin está en
`operations.es.md`; por qué se hizo cada cambio, y lo que midió, en
`decisions.es.md`.

## 1. Repositorio

- Un fork de StarLightGS (github.com/CoSTAR-National-Lab/starlightgs), desde
  el 8720289 de upstream; la historia de upstream se conserva debajo del
  trabajo de este fork.
- Las líneas cambiadas en los ficheros de upstream se marcan con un
  comentario `athenea:`, para que un merge desde upstream muestre qué es
  nuestro.
- Los ficheros escritos aquí empiezan por `// Copyright (c) 2026 jesus luque.`
  (o lo mismo en la sintaxis de comentarios del fichero).
- La documentación está en `Docs/athenea/`: `DESIGN.md`, `operations.md`,
  `development.md` y `decisions.md`, cada uno con una traducción `.es.md`
  cambiada en el mismo commit. Una traducción conserva los mismos títulos y
  tablas.

## 2. Módulos

| Módulo | Tipo | Contiene |
|---|---|---|
| `GaussiansRenderer` | Runtime, cargado en PostConfigInit (los shaders globales lo necesitan) | los datos de los splats (`FGaussianSplatData`), su recurso de GPU (`FGaussianSplatResource`), la view extension que dibuja cada frame, los pases (culling, prepass, radix sort, raster, composición) |
| `Gaussians` | Runtime | el asset (`UGSplatAsset`), sus datos de importación, el componente y el actor, el scene proxy y el vertex factory usados para las sombras, los volúmenes de recorte |
| `GaussiansEditor` | Editor | el factory, los importadores (PLY, USD, `.athc`), el exportador, los diálogos de importación y exportación |

Los shaders están en `Shaders/`, mapeados a `/Plugin/StarLightGS/`.

## 3. Un frame

1. **Culling** (`GSplatCulling.usf`, solo cuando algo ha cambiado): volúmenes
   de recorte contra los splats, en una lista de índices por primitiva.
2. **Prepass** (`GSCompute.usf`, `GSPrepass`): un thread por splat. Culling de
   frustum y HZB, la proyección EWA, el color (`GSColor.ush`), y un append a
   un único buffer compartido por todas las primitivas visibles
   (`r.Gaussians.MaxSplats`, 5 M por defecto).
3. **Sort** (`RadixSort.usf`): un único radix sort global por vista sobre la
   profundidad en espacio de vista.
4. **Raster** (`GSSplatting_VS/PS`): un único draw indirecto instanciado de
   quads. La ruta sin iluminar mezcla "over" en SceneColor antes del
   post-proceso; la ruta iluminada acumula de delante a atrás en cuatro
   targets RGBA16F que un pase de composición escribe en el GBuffer de UE.
5. **Sombras**: el vertex factory (`GSVertexFactory.ush`) dibuja los splats en
   virtual shadow maps, opacos por encima de 0,9.

## 4. Lo que añade este fork

| Pieza | Dónde | Qué hace |
|---|---|---|
| Armónicos | `GSColor.ush`, `GSPrepass` | grados 1 a 3 evaluados a lo largo de la vista en el marco del fichero; límite a cero; curva sRGB cuando `bLinearizeColor` |
| Ejes del fichero | `FGaussianSplatData::FileAxisX/Y/Z`, `GSResources` | dónde caen los ejes del fichero en el espacio local, para llevar la dirección de vista de vuelta al marco en el que se ajustaron los armónicos |
| Comentario PLY | `GSplatImporter.cpp`, `ParsePly` | el comentario `flatten` de athenea fija el swizzle, la escala y el espacio de color |
| Lector USD | `GSplatUsdImporter.cpp` | el primer `ParticleField3DGaussianSplat` de un stage mediante el OpenUSD de USDCore |
| Lector `.athc` | `GSplatAthcImporter.cpp` | el nivel más fino de un `.athc`, copiado tal como lo empaqueta athenea |
| Decodificador en GPU | `GSDecode.usf`, `FGSDecodeAtheneaCS` en `GaussianSplatResource.cpp` | el empaquetado de athenea a los buffers float que leen los pases, una vez por recurso |
| Correcciones | ver `decisions.es.md` | build en 5.8, append fuera de límites, pre-exposure, límite de sombra, culling con bool, flags del vertex factory, contenido del plugin, inicialización de los bounds |
| Estadística de GPU | `GSViewExtension.cpp` | `GPU/GaussiansUnlit`, junto a `GPU/GaussiansLit` de upstream |

## 5. Convenciones

### 5.1 Ejes, unidades y rotaciones

- UE es levógiro y con z arriba, en centímetros. Todo importador mapea los
  ejes de un fichero con un swizzle (`EGSplatSwizzleAxis`, tres entradas),
  las mismas `SwizzleVector`, `SwizzleVectorAbs` y `SwizzleQuaternion` que usa
  el importador PLY (declaradas en `GSplatImporter.h`).
- Un fichero dextrógiro con y arriba (el de athenea, el predeterminado de
  USD) se mapea a (X, Z, Y); un stage dextrógiro con z arriba, a (X, −Y, Z).
  Cada uno es un espejo, que es lo que necesita un cambio de quiralidad.
- Las posiciones se escalan a centímetros. **Las escalas se quedan en
  metros**: el prepass y el vertex factory las multiplican por 100 ellos
  mismos (la convención de upstream), así que un importador las escala por su
  factor de escala dividido entre 100.
- Las rotaciones siguen `SwizzleQuaternion`: la parte vectorial pasa por el
  swizzle, y `w` se niega cuando el swizzle conserva la quiralidad. Es la
  convención contra la que se escribió el rasterizador; `GSDecode.usf` la
  repite.
- `FileAxisX/Y/Z` son el swizzle aplicado a los ejes unitarios, guardado en el
  asset para que los shaders puedan llevar una dirección local de vuelta al
  marco del fichero.

### 5.2 Color y armónicos

- `ZeroOrderHarmonicsCoefficientsAndOpacities.rgb` contiene `0.5 + C0 · dc`
  (la convención de 3DGS, y el base colour empaquetado de athenea), sin
  limitar.
- `HighOrderHarmonicsCoefficients` contiene 45 floats por splat en el orden
  `f_rest` del PLY: los coeficientes 1 a 15 del rojo, luego los del verde,
  luego los del azul. Las bandas que faltan son cero.
- El color se suma en el espacio en el que el fichero lo entrenó, se limita a
  cero, y solo entonces se linealiza (la curva sRGB, como `cloudLight` de
  athenea). Ningún importador limita ni aplica una gamma en la CPU.
- `GSResources.ColorFlags`: bit 0, los armónicos están presentes y se usan;
  bit 1, linealizar.

### 5.3 El empaquetado de athenea

Un asset empaquetado (`bPacked`) contiene el layout de dispositivo de athenea
(`shaders/athenea/common/packing.slang` en athenea), copiado del fichero:

| Array | Por splat | Contenido |
|---|---|---|
| `PackedPositions` | float4 | xyz en las unidades del fichero, opacidad lineal |
| `PackedShape` | 4 uint | [0] rotación, las tres menores (10 + 10 + 10 bits, índice de la mayor en los bits 0–1); [1] log de escala f16 x, y; [2] log de escala f16 z, base colour r; [3] base colour g, b |
| `PackedSH` | `PackedSHWords` uint | coeficientes rest f16, rgb por base, dos mitades por palabra; `PackedRestPerColour` de ellos por color |

`FGSDecodeAtheneaCS` escribe `Positions`, `Scales`, `Orientations`,
`SHAndOpacities` y `HighOrderSH` a partir de ellos en `InitRHI`, en dispatches
de como mucho 65535 grupos de 64. Los arrays float de un asset empaquetado se
quedan vacíos en la CPU.

### 5.4 La parte de la CPU

Como en athenea, la CPU lee ficheros, analiza cabeceras y copia bytes; la
decodificación es un kernel. Los importadores PLY y USD todavía convierten en
la CPU, como hacía upstream (logs de escala, la sigmoide, swizzles); la ruta
`.athc` es el modelo que las demás deben seguir.

## 6. Añadir un formato

1. Escribir `GSplat<Format>Importer.cpp` en `GaussiansEditor/Private` con
   `Is<Format>File` e `Import<Format>`, declarados en `GSplatImporter.h`.
2. Rellenar `UGSplatImportData` (swizzle, escala, gamma, lo que tiene el
   fichero) y `FGaussianSplatData`, como arrays float o empaquetado. Un
   formato que athenea ya decodifica en su GPU es mejor copiarlo empaquetado y
   darle un decodificador en `GSDecode.usf`.
3. Guardar `FileAxisX/Y/Z`, `bLinearizeColor` y los bounds
   (`RecalculateBounds` para arrays float, `SetBounds` desde el fichero para
   los empaquetados).
4. Añadir la extensión a los `Formats` del factory, a `FactoryCanImport`, y a
   la rama de `ImportInternal`.
5. Escribir en el log una línea si tiene éxito y un motivo en cada rechazo
   (operations §4.5).
6. Comprobarlo como en §7, y documentarlo en los tres manuales, en los dos
   idiomas.

## 7. Cómo se comprueba un cambio

Todavía no hay tests automáticos en el plugin. Un cambio se comprueba en una
máquina Windows contra athenea:

1. **Build**: `RunUAT BuildPlugin` (operations §2), sin ningún aviso nuevo.
2. **Importación**: un commandlet (`UnrealEditor-Cmd ... -run=pythonscript
   -script=setup_*.py -nullrhi`) importa el fichero de prueba y guarda un
   nivel; su línea de log da el recuento y lo que se leyó.
3. **Capturas**: `shot_level.py` renderiza el nivel desde las cámaras
   guardadas (`shots/cameras.json`: vistas frontal, lateral y alta del peón).
4. **Referencia**: el raster de athenea renderiza el mismo fichero desde las
   mismas cámaras (`athenea render --technique raster`, ojo y objetivo
   llevados de los centímetros de UE a los metros del fichero con los ejes
   intercambiados, focal de 45.861 mm para los 30° de UE).
5. **Comparación**: `oiiotool --diff` entre formatos, y lado a lado contra
   athenea.
6. **Tiempos**: `-game` con el profiler CSV (operations §7.2), en una GPU
   libre, comparados con el raster de athenea en el mismo frame.

Los scripts están en `Tools/athenea/` (§8). Ficheros de prueba: un peón de
ajedrez (`pawn_3dgs.ply`, 325 833 gaussianas, grado 3, pasado por flatten de
athenea), su exportación USD (`pawn.usdc`) y su `.athc` (`pawn.athc`); no se
distribuyen con el plugin.

## 8. Scripts

`Tools/athenea/` contiene lo que ejecuta §7, tal como se usó. Esperan un
proyecto en `~/athenea-unreal/TestProj` con el plugin en su carpeta `Plugins`,
los ficheros de prueba en `~/athenea-unreal/`, y escriben las capturas en
`~/athenea-unreal/shots/`.

| Fichero | Se ejecuta con | Hace |
|---|---|---|
| `setup.py` | `UnrealEditor-Cmd <proyecto> -run=pythonscript -script=setup.py -nullrhi` | importa `pawn_3dgs.ply` y guarda `/Game/Test` con un actor suyo |
| `setup_usd.py`, `setup_athc.py` | igual | importan `pawn.usdc` o `pawn.athc` en `/Game/TestUsd` o `/Game/TestAthc` |
| `shot.py` | `UnrealEditor <proyecto> -RenderOffscreen -ExecCmds="py shot.py"` | encuadra los splats del nivel del PLY desde tres vistas, escribe `cameras.json` y las capturas |
| `shot_level.py` | `-ExecCmds="py shot_level.py /Game/Nivel nombre"` | capturas de un nivel desde el `cameras.json` guardado |
| `gamecam.py` | como `setup.py` | una cámara que el jugador toma al empezar, en la vista frontal, para medir en `-game` |
| `ue_time.sh` | `ue_time.sh /Game/Nivel salida.csv`, desde una máquina con `ssh` a la Windows | mide un nivel en `-game` con el CSV profiler (límite de 30 fps) e imprime las medianas de las etapas de splats |
| `TestProj.uproject`, `DefaultEngine.ini`, `DefaultEditorPerProjectUserSettings.ini` | -- | el proyecto de prueba: solo SM6, sin auto-exposure, bloom, motion blur ni AA temporal; los ajustes del servidor MCP (operations §8) |
