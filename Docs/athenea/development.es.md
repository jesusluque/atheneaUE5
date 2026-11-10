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
| `GaussiansEditor` | Editor | el factory, los importadores (PLY, USD, `.athc`, `.splat`, `.spz`, `.sog`, glTF), el exportador, los diálogos de importación y exportación |
| `AtheneaCodecs` | Editor | lo que necesitan los importadores para deshacer la compresión de un fichero: gzip (el zlib de UE), el descompresor de zstd y el decodificador de libwebp, compilados desde su código fuente (sin precompiled header, sin unity build) |

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
   targets RGBA16F que un pase de composición escribe en el GBuffer de UE. Con
   `r.Gaussians.Athenea.FrontToBack` la ruta sin iluminar también dibuja de
   delante a atrás (`GSFront.usf`): el sort ascendente, los quads a la
   profundidad de su splat (sin `SV_Depth`, así el test de profundidad va
   antes del shader), por lotes en `Gaussians.Front` con una copia de la
   profundidad de la escena; entre lotes `GSFrontSaturatePS` lleva la
   profundidad de los píxeles opacos al plano cercano; `GSFrontCompositePS`
   pone el buffer sobre SceneColor y `GSFrontVelocityPS` hace la velocidad
   de la escena con la profundidad más cercana.

El corte de los niveles de detalle y su morph están en `GSLod.ush`, que
incluyen el pase de culling (qué gaussianas) y el prepass (dónde se dibujan).
5. **Sombras**: el vertex factory (`GSVertexFactory.ush`) dibuja los splats en
   virtual shadow maps, opacos por encima de 0,9.

## 4. Lo que añade este fork

| Pieza | Dónde | Qué hace |
|---|---|---|
| Armónicos | `GSColor.ush`, `GSPrepass` | grados 1 a 3 evaluados a lo largo de la vista en el marco del fichero; límite a cero; curva sRGB cuando `bLinearizeColor` |
| Ejes del fichero | `FGaussianSplatData::FileAxisX/Y/Z`, `GSResources` | dónde caen los ejes del fichero en el espacio local, para llevar la dirección de vista de vuelta al marco en el que se ajustaron los armónicos |
| Comentario PLY | `GSplatImporter.cpp`, `ParsePly` | el comentario `flatten` de athenea fija el swizzle, la escala y el espacio de color |
| Lector USD | `GSplatUsdImporter.cpp` | el primer `ParticleField3DGaussianSplat` de un stage mediante el OpenUSD de USDCore |
| Lector `.athc` | `GSplatAthcImporter.cpp` | el nivel más fino de un `.athc`, copiado tal como lo empaqueta athenea; en una versión 3, sus chunks o páginas, con sus secciones `CORE`, `SHRS`, `TREE`, `MATL`, `SHAD`, `TXDI`, `TXIN` y `TXFD` tal como están almacenadas, con descriptores de sección y una tabla de bloques (`PackedFormat` 5): `GSAthc3UndeltaCS` suma los planos de diferencias, `GSAthc3CpcaCS` reconstruye CPCA, `GSDecodeAthc3CS` los splats y `GSDecodeAthc3TxCS` los buffers de la transferencia |
| Lectores `.splat`, `.spz` | `GSplatFileImporter.cpp`, `GSDecodeSplatCS` y `GSDecodeSpzCS` en `GSDecode.usf` | los bytes propios de un fichero (descomprimidos en `.spz`), decodificados en la GPU (`PackedFormat` 1 y 2) |
| Lector `.sog` | `GSplatSogImporter.cpp`, `GSDecodeSogCS` en `GSDecode.usf` | los texels de las imágenes, sus rangos y codebooks (`PackedFormat` 3), decodificados en la GPU; un pequeño lector de zip propio |
| Lector `.athz` | `GSplatAthzImporter.cpp`, `GSAthzTablesCS` y `GSAthzDecodeCS` en `GSDecode.usf` | las páginas de un `.athz`, la lista de páginas, los planos, las frecuencias y un descriptor por nivel (`PackedFormat` 6); decodificado en el scratch al cargar, y los splats se leen luego como un bloque `.athc` v3 de codificación 4 (palabras ya en el scratch) |
| Lector de escena web | `GSplatSceneImporter.cpp` | un zip `.athz`: `scene.json`, cada `.athc` por el lector `.athc` (un fichero de sus bytes bajo `Intermediate/athenea/athz`), el cielo por el importador propio de UE; el lector de zip es el del importador SOG (`GSplatImporter::ReadZip`) |
| Lector glTF | `GSplatGltfImporter.cpp`, `GSDecodeGltfCS` en `GSDecode.usf` | los buffers del fichero y una tabla de 21 descriptores de accessor (`PackedFormat` 4, la tabla en `PackedStreams[0]`), cada accessor leído en la GPU |
| Decodificador en GPU | `GSDecode.usf`, `FGSDecodeAtheneaCS` en `GaussianSplatResource.cpp` | el empaquetado de athenea a los buffers float que leen los pases, una vez por recurso |
| Correcciones | ver `decisions.es.md` | build en 5.8, append fuera de límites, pre-exposure, límite de sombra, culling con bool, flags del vertex factory, contenido del plugin, inicialización de los bounds |
| Estadística de GPU | `GSViewExtension.cpp` | `GPU/GaussiansUnlit`, junto a `GPU/GaussiansLit` de upstream |
| Transferencia | `GSColor.ush` (`GSTransferredBody`), `FGaussianSplatData::Transfer` | el cuerpo de una nube TX: albedo (1 − metallic) ⟨T_direct + T_indirect, L⟩, el `transferredBody` de athenea |
| Proyección del cielo | `GSSkySH.usf`, `FGSSkySHCS` en `GSRasterizePass.cpp` | el cubemap del sky light sobre 16 armónicos en el marco del fichero de una primitiva, un grupo por primitiva TX y vista, antes de su prepass |
| Vidrio | `GSLensExit`, `GSLensExitThrough`, `GSLensFarReflection`, `GSTransmittedRoughness`, la rama de vidrio sólido en `GSTransferredRadiance` (`GSColor.ush`); la ranura 19 de la superficie (la transmisión, más 2 para una pared fina) | la lente de `splat_relight` de athenea, el cuerpo como su `body2`: la parte opaca, luego lo que pasa, por la lente, y el reflejo de la cara lejana |
| Lóbulos nítidos | el giro en `GSPrepass` (`GSCompute.usf`), la separación en `GSTransferredRadiance` (`GSColor.ush`), la lectura en `GSSplatting_PS` (`GSSharpSky`); `GSSunWorldCS` en `GSSkyResidual.usf` | el lóbulo nítido de athenea dibujado por píxel: dos `uint4` por splat (`OutSharpA`, `OutSharpB`) del prepass al raster, el sol en el mundo en un buffer de dos elementos |
| Cielo residual | `GSSkyResidual.usf` (`GSSkyCutCS`, `GSSkyDownCS`, `GSSkyResidualCS`), en `GSRasterizePass.cpp` | el cubo del sky light con su sol cortado y filtrado por caja (el corte), y luego prefiltrado por GGX en el cubo que leen las reflexiones; se construye una vez por cubo y color del sky light (`GAtheneaResidual`) |
| Pulido | `GSPolish.ush`, `GSTransferredRadiance` en `GSColor.ush` | la suma opaca `relitByDome` de athenea: capas, BRDF de entorno, celdas de direcciones abiertas, el campo reflejado y su acoplamiento al cielo |
| Depuración | `r.Gaussians.Athenea.Debug`, `.LogSky` | vistas del cielo proyectado, y su lectura de vuelta al log |

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
  bit 1, linealizar; bit 2, el color es un albedo iluminado por una
  transferencia.

### 5.2.1 La transferencia y su cielo

- `Transfer` contiene `4 · TransferDirectCount` floats por splat: los valores
  directos (9 o 16) y después los indirectos, rgb por coeficiente, en el orden
  de athenea. `TransferMetallic` contiene uno por splat.
- El cielo se proyecta sobre la base de entorno de athenea
  (`envBasisValue`, light/environment.slang): los armónicos reales de 3DGS.
- `GSSkySH.usf` toma 4096 direcciones en una esfera de Fibonacci **en el marco
  del fichero**, lleva cada una al mundo mediante `FileAxisX/Y/Z` y el
  `LocalToRelativeWorld` de la primitiva, lee allí el `ProcessedTexture` del
  sky light en el mip de unos 32 texels por cara, multiplicado por
  `View.SkyLightColor`, y suma la base en el marco del fichero. La
  transferencia y el cielo comparten entonces un marco sea cual sea la
  rotación del actor, espejo incluido.
- El pulido lee el cubo residual (o, con `r.Gaussians.Athenea.Residual 0`, el
  del sky light) en el mip en el que las reflexiones propias de UE lo leen a
  la rugosidad de un lóbulo (`ComputeReflectionCaptureMipFromRoughness`:
  `mips − 1 − (1 − 1.2 log2 r)`), con su dirección llevada al mundo de la
  misma manera; el residual prefiltra cada mip a esa rugosidad. El sol vuelve
  a través de cada lóbulo mediante `GSSunReflectPdf` (`sunReflectPdf` de
  athenea), por el peso del lóbulo, la irradiancia del sol y la parte que las
  celdas dejan pasar.
- El giro de un lóbulo nítido (`splat_project` de athenea): los dos primeros
  ejes de la gaussiana son los del fichero llevados por su rotación
  (`R' · FileAxisX`, `R' · FileAxisY`, R' su rotación local, que es M R Mᵀ para
  el mapeo M del fichero); un píxel a la derecha y hacia abajo sobre su plano
  sale del jacobiano de la proyección (`JW`, píxeles por metro, la y de la
  vista negada para un píxel hacia abajo), en las unidades del fichero a
  través de la escala del actor y `FileUnitsToCm`; el operador de forma por
  ese paso, a lo largo de esos ejes, es el giro de la normal en el marco del
  fichero, como máximo 0,5. La rugosidad de un lóbulo se ensancha con el giro
  propio del píxel (α² += (|dx|² + |dy|²) / 6). El payload: A = el espejo en el
  centro en el mundo (octaédrico), sus pasos, el peso del coat (una
  luminancia) y su rugosidad; B = el peso (rgb) y la rugosidad del pulido, el
  sol a través del pulido (rgb) y del coat, cada uno por la parte que las
  celdas dejan pasar.
- `TransferSurface` contiene 20 floats por splat (`GS_SURFACE_STRIDE`): normal
  (3), rugosidad, peso especular, color (3) e índice, peso, rugosidad, índice y
  oscurecimiento del coat, color (3) y rugosidad del sheen, el metal de
  Schlick, la rugosidad del material, y uno sin usar. `TransferCells` contiene
  `CellWords` palabras por splat; `TransferField`, 48 floats.
- Qué apertura toma un lóbulo sigue a athenea: el lóbulo base usa las celdas
  y el campo solo cuando la rugosidad del material es 0,3 o más
  (`kPolishFieldRoughness`), y por debajo de eso el ajuste de Lagarde del
  término constante de la transferencia; el coat usa las celdas allí donde la
  nube las conserva.
- El pase del cielo también encuentra el sol del cielo, como hace `env_sun`
  de athenea: sobre 16384 direcciones de igual área en un mip de unos 64
  texels por cara, un histograma de la luminancia en log2 (80 bins de medio
  stop) da la mediana; el sol son las direcciones por encima de max(16
  mediana, 0,02 pico) a menos de 0,175 rad del pico, cuando el pico es 32
  veces la mediana, y se conserva solo cuando su dispersión ponderada por la
  luz no supera la de un disco de su ángulo sólido (si no, sus direcciones
  vuelven a los armónicos). `SkySH[16]` es su dirección y su semiángulo (0:
  ninguno), `SkySH[17]` su irradiancia y el umbral. El cuerpo suma
  `irradiance · cos · share / π`, con la fracción leída de las celdas a lo
  largo del sol, y el rebote `irradiance · T_indirect(sun)`; el acoplamiento
  del campo lleva el sol a sus dos sumas.
- El cubemap se lee del proxy del sky light de la escena
  (`FScene::SkyLight->ProcessedTexture`): el uniform buffer de reflexión de UE
  (`CreateReflectionUniformBuffer`) no se exporta a los plugins.

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
la CPU, como hacía upstream (logs de escala, la sigmoide, swizzles); las rutas
`.athc`, `.splat` y `.spz` son el modelo que las demás deben seguir. Esas dos
conservan los bytes propios del fichero (`PackedBytes`, `PackedFormat`, y en
`.spz` `PackedStreams` y `PackedSpz`), subidos como un byte-address buffer; la
única aritmética de la CPU ahí son los bounds, que ninguno de los dos formatos
guarda.

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
   (operations §4.6).
6. Comprobarlo como en §7, y documentarlo en los tres manuales, en los dos
   idiomas.

## 7. Cómo se comprueba un cambio

Los importadores tienen tests de automatización
(`Source/GaussiansEditor/Private/Tests/`, `Athenea.Import.*`): cada fichero de
prueba lo lee su importador y se comprueba lo que la importación hizo con él
-- todo formato se lee, tiene gaussianas y límites; los 8 niveles de
`pawn.athc` y sus palabras; el árbol paginado de una escena web, una raíz y
sus 349.076 fusiones; los 7 niveles de un `.athz`, sus grupos tras sus
splats; el medio y las constantes de un frame de `vdb2splat`
(un frame frío sin temperatura lee 0); la transferencia de una nube TX. No
necesitan GPU (el decodificado se hace al crear un recurso) y toman los
ficheros de prueba de `ATHENEA_UE_TEST_FILES` (por defecto `~/athenea-unreal`),
saltándose el que no esté:

```
UnrealEditor-Cmd.exe <proyecto> -nullrhi -unattended -ExecCmds="Automation RunTests Athenea; Quit"
```

Más allá de ellos, un cambio se comprueba en una máquina Windows contra
athenea:

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
| `setup_tx.py` | como `setup.py`, o `-ExecCmds="py setup_tx.py paint.athc /Game/TestTxW paint 1 --quit"` | importa `paint_tx.usdc` y `studio.hdr` y guarda `/Game/TestTx`: la nube (escalada 100, con su stage diciendo centímetros), un sky light con el HDRI, la cámara de la escena de prueba; o bien otra nube en otro nivel a otra escala (1 para un fichero en metros) |
| `shot_tx.py` | `-ExecCmds="py shot_tx.py 0 90 180"` | capturas PNG (y el HDR de UE, que satura) de `/Game/TestTx` con el sky light girado cada uno de esos ángulos |
| `capture_tx.py` | `-ExecCmds="py capture_tx.py 180 nombre [/Game/Level] [cvar=valor;...]"` | la captura lineal (operations §7.1) de `/Game/TestTx` (o del nivel dado) a un ángulo, con las variables de consola fijadas antes, y el cielo proyectado en el log |
| `capture_relight.py` | `-ExecCmds="py capture_relight.py /Game/Level name 120 [width height]"` | las nubes relit de un nivel bajo una luz que gira: desde la cámara del nivel (o encuadradas sobre sus nubes si no tiene), ese número de PNG con tone mapping en `shots/name/` mientras el cubemap de la sky light da una vuelta y la luz direccional, si la hay, gira con él |
| `ply_parts.py` | `python ply_parts.py FILE.ply [GIB]` (Python normal, sin Unreal) | un PLY binario cortado en partes de registros consecutivos de menos de ese tamaño (3,5 GiB por defecto), `FILE_0.ply` ... junto a él, para una captura que pasa de los 4 GiB de registros que admite un `.athc` (el [tutorial](tutorial-large-scene.es.md)) |
| `shot_dbg.py` | `-ExecCmds="py shot_dbg.py 100 101 ..."` | capturas con `r.Gaussians.Athenea.Debug` en cada valor |
| `setup_files.py` | `-ExecCmds="py setup_files.py a.spz b.glb@100 --quit"` | importa cada fichero (de `athenea-unreal` en la carpeta personal) con la factory del plugin en `/Game/Files` y guarda un nivel `/Game/F_<nombre>_<ext>` con un actor de él; `@k` escala el actor, para un fichero en unidades distintas de metros |
| `shot_framed.py` | `-ExecCmds="py shot_framed.py +y x3 /Game/F_a /Game/F_b"` | una captura de cada nivel, encuadrada sobre los bounds de sus splats desde un lado (`±x`, `±y`), `x<k>` k veces más lejos, en `shots/framed_<nivel><lado>.png` |
| `ue_time.sh` | `ue_time.sh /Game/Nivel salida.csv ["cvar valor,..."]`, desde una máquina con `ssh` a la Windows | mide un nivel en `-game` con el CSV profiler (límite de 30 fps), con las variables de consola fijadas antes, e imprime las medianas de las etapas de splats |
| `setup_scene.py` | `-ExecCmds="py setup_scene.py /Game/Nivel sky.hdr 100 0:0.05:0.18 0:0.05:0 39.6 a.athc b.athc --quit"` | varias nubes y el sky light de un HDRI en un nivel, una cámara desde el frame de los ficheros (`x:y:z`, porque `-ExecCmds` separa por comas), cada actor y la cámara escalados (una escena mucho menor que el plano cercano de UE) |
| `tx_regions.py` | `python3 tx_regions.py ue.exr athenea.exr [exposure]` | la bola del frame de prueba TX, sus mitades y el suelo, UE (sobre el factor de exposición de la captura, 2,02) frente al frame de athenea: medias, cociente y error relativo absoluto medio |
| `TestProj.uproject`, `DefaultEngine.ini`, `DefaultEditorPerProjectUserSettings.ini` | -- | el proyecto de prueba: solo SM6, sin auto-exposure, bloom, motion blur ni AA temporal; los ajustes del servidor MCP (operations §8) |
