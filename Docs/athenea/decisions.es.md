# athenea para Unreal: decisiones

Copyright (c) 2026 jesus luque.

Cada cambio que este fork hace a StarLightGS (upstream 8720289), por qué se
hizo, lo que se midió y lo que no está hecho. Lo más reciente al final. Cómo
usar el plugin está en `operations.es.md`; cómo está organizado el código, en
`development.es.md`.

## Contenido

1. [El fork y dónde se ejecuta](#1-el-fork-y-dónde-se-ejecuta)
2. [Defectos de upstream corregidos antes del build](#2-defectos-de-upstream-corregidos-antes-del-build)
3. [Build contra UE 5.8.4](#3-build-contra-ue-584)
4. [El primer frame](#4-el-primer-frame)
5. [Armónicos a lo largo de la vista](#5-armónicos-a-lo-largo-de-la-vista)
6. [Un PLY de athenea indica sus ejes y su espacio de color](#6-un-ply-de-athenea-indica-sus-ejes-y-su-espacio-de-color)
7. [ParticleFields USD mediante el OpenUSD de UE](#7-particlefields-usd-mediante-el-openusd-de-ue)
8. [.athc empaquetado, decodificado en la GPU](#8-athc-empaquetado-decodificado-en-la-gpu)
9. [La curva sRGB, no una potencia de 2,2](#9-la-curva-srgb-no-una-potencia-de-22)
10. [Tiempos](#10-tiempos)
11. [Lo que no está hecho](#11-lo-que-no-está-hecho)

## 1. El fork y dónde se ejecuta

Se eligió StarLightGS (CoSTAR National Lab, MIT) como base porque ya hace en
UE lo que athenea necesita primero: un asset de gaussianas, un prepass de
compute con culling de frustum y HZB y proyección EWA, un único radix sort
global por vista, y un draw indirecto de quads instanciados mezclados sobre la
escena. Lo que le falta es lo que hace que las nubes de athenea sean de
athenea: color dependiente de la vista, los formatos de athenea, niveles de
detalle y transferencia. `DESIGN.es.md` da el orden.

Se compila y se prueba en un portátil Windows (RTX 3070 Laptop GPU, 8 GB) con
UE 5.8.4 del launcher de Epic.

## 2. Defectos de upstream corregidos antes del build

Encontrados leyendo el código, corregidos antes de cualquier build (commit
51a9446):

- **Un append más allá del buffer.** El prepass añade los splats visibles a
  un único buffer de `r.Gaussians.MaxSplats`. Upstream solo comprobaba que el
  inicio de un grupo estuviera dentro, así que el grupo que cruzaba el final
  escribía más allá. DX12 descarta esas escrituras; Metal y Vulkan no. Ahora
  el grupo conserva lo que cabe y el contador se fija a la capacidad.
- **Sin pre-exposure.** El color sin iluminar es luz de escena, y todo lo
  demás en SceneColor se multiplica por la pre-exposure de la vista; los
  splats no. Ahora sí, en el pixel shader, solo en la ruta sin iluminar.
- **El límite del vértice de sombra.** El vertex factory leía un splat más
  allá del recuento (`Index > Count` donde se quiere decir `>=`).

## 3. Build contra UE 5.8.4

Upstream dice que soporta 5.8; no compilaba con el 5.8.4 del launcher (commit
bb201b1):

- **Includes.** Las cabeceras de UE 5.8 ya no traen lo que upstream daba por
  supuesto: `ShaderParameterMacros.h` y `UniformBuffer.h` para los uniform
  buffers, `VertexFactory.h`, `SceneManagement.h`, `SceneView.h`,
  `MeshBatch.h`, `Materials/MaterialInterface.h` y `Materials/Material.h`,
  `Engine/World.h`, `UObject/ConstructorHelpers.h`, `Misc/Paths.h`,
  `PrimitiveUniformShaderParameters.h`, `RenderingThread.h`,
  `RHICommandList.h`, `RHIResourceUtils.h`. Unos 680 errores antes; ninguno
  después.
- **Categorías.** UHT en 5.8 exige una `Category` en cada propiedad y función
  expuesta al editor o a Blueprints; upstream tenía diez sin ella.

La máquina también necesitó dos cosas fuera del código: desactivar Windows Smart App Control (bloqueaba la propia DLL
de reglas de UnrealBuildTool) y añadir el SDK de .NET Framework 4.8 a las
Build Tools.

## 4. El primer frame

Cinco defectos más separaban un build correcto de una imagen:

- **El plugin se empaquetaba sin su contenido** (commit d9a9174):
  `"CanContainContent": "true"` es una cadena, que 5.8 lee como false, así que
  `BuildPlugin` descartaba `Content/` y el constructor del componente fallaba
  en un assert por un material ausente. Ahora es un booleano.
- **`&=` sobre un bool.** El código de culling combinaba la visibilidad con
  `bVisible &= ...`, que el compilador de shaders de 5.8 rechaza (X3020). Seis
  sitios escriben ahora `bVisible = bVisible && (...)` (commit 0a89bfb).
- **Demasiados UAVs para SM5.** El prepass enlaza 10 UAVs; SM5 permite 8. El
  plugin necesita un proyecto solo SM6 (operations §3).
- **Permutaciones de lightmap.** El vertex factory declaraba
  `SupportsStaticLighting` pero no lee coordenadas de lightmap, así que todas
  las permutaciones de base pass con lightmap del material predeterminado
  fallaban al compilar. El factory solo dibuja profundidad de sombra; el flag
  ya no está (commit 07c6c4a).
- **Bounds sin inicializar.** `FGaussianSplatData::Bounds` no tenía
  inicializador, lo que el test de inicialización de miembros de UE notifica
  al cargar; ahora empieza a cero (commit 4968e2b).

La primera imagen (peón, 325 833 gaussianas, sin iluminar): dibujado, derecho
tras una rotación del actor, y diminuto, porque la cámara estaba encuadrada
sobre los bounds del actor, que incluyen el billboard del editor (128 cm); los
bounds del componente (unos 4 × 4 × 7 cm) lo encuadran.

## 5. Armónicos a lo largo de la vista

Upstream reserva 45 floats por splat para los grados 1 a 3, nunca los importa
(el importador rellena el array con ceros) y nunca los lee: todas las nubes se
dibujaban solo con su base colour. Commit 7ed3cf4:

- El importador PLY lee `f_rest_0..44` cuando están los 45 y los conserva en
  el layout del fichero, splat a splat.
- El prepass los evalúa (`GSColor.ush`, las constantes y signos de 3DGS) a lo
  largo de la dirección del ojo al splat. Esa dirección se lleva al espacio
  local del asset, y luego al marco del fichero mediante `FileAxisX/Y/Z`,
  porque los armónicos se ajustaron en los ejes del fichero, no en los de UE.
- La ruta iluminada no los usa: su color es un albedo.

**Comprobado contra athenea.** El raster de athenea renderizó el mismo PLY
desde las mismas tres cámaras (frontal, lateral, alta), con la autoexposición,
el bloom, el motion blur y el anti-aliasing temporal de UE desactivados.
Forma, encuadre y los reflejos de la cabeza de vidrio caen en los mismos
sitios en las tres vistas, incluida la mancha oscura de la cabeza vista desde
arriba. La única diferencia es el color de la cabeza, que el tonemapper
filmic de UE lleva del verde azulado hacia el blanco; eso es la transformación
de display de UE, no los splats.

## 6. Un PLY de athenea indica sus ejes y su espacio de color

`athenea flatten` escribe `comment athenea flatten: degree 3, sRGB
display-referred, y up, metres`. El swizzle predeterminado de upstream (Z, X,
−Y) es la convención de COLMAP con y abajo, así que el peón de athenea, con y
arriba, entraba boca abajo, y su color se tomaba como lineal. El importador
lee ahora el comentario (commit 7ed3cf4, operations §4.2): `y up` da (X, Z,
Y), `metres` escala 100, `sRGB display-referred` linealización. La elección
por asset se hace a partir del fichero, no a mano.

**La gamma pasa a la GPU.** Upstream elevaba el base colour a 2,2 en la CPU,
antes que nada; con armónicos eso es incorrecto, porque un color
display-referred es la suma de todas las bandas y se linealiza una vez,
después de la suma. La CPU ya no limita ni aplica una gamma; el prepass limita
a cero y linealiza después de sumar.

## 7. ParticleFields USD mediante el OpenUSD de UE

La elección: leer la propia exportación `.usd` de athenea en lugar
de inventar un fichero de intercambio. El importador USD de UE no conoce ni
`ParticleField3DGaussianSplat` ni los `primvars:athenea:splat:*` de athenea,
así que el plugin lee el stage él mismo, mediante el OpenUSD que UE incluye en
su plugin USDCore (0.26.3, que tiene los esquemas ParticleField) (commit
7591c05).

- Lee lo que lee el propio lector de athenea (operations §4.3).
- Su factory reclama un fichero USD solo cuando el stage contiene un
  ParticleField de gaussianas, con una prioridad mayor que el importador de
  UE, así que cualquier otro stage sigue yendo al de UE.
- **Versiones de fichero.** athenea escribe con OpenUSD 26.08 y UE lee con
  26.03; los 40 ficheros `.usdc` recientes comprobados usan todos crate 0.8.0,
  que ambos leen.

**Medido.** La exportación USD del peón (`.usdc` de 76 MB) se importa en unos
2 s: 325 833 gaussianas, grado 3, sRGB.

**Licencia.** OpenUSD está bajo la Tomorrow Open Source Technology License 1.0
(la Apache 2.0 modificada de Pixar). UE lo distribuye bajo esa licencia; el
plugin enlaza contra la copia de UE y no redistribuye nada de él.

## 8. .athc empaquetado, decodificado en la GPU

Un `.athc` contiene el empaquetado de dispositivo de athenea: 32 bytes por
splat antes de los armónicos, y 23 palabras de armónicos f16 en grado 3,
frente a 236 bytes como floats. Convertirlo en la CPU rompería la regla de
athenea de que la CPU solo copia bytes, y cuadruplicaría lo que guarda el
asset. Así que (commit 2e98148):

- El importador copia los chunks del nivel más fino tal como están en el asset
  (`bPacked`: `PackedPositions`, `PackedShape`, `PackedSH`).
- Cuando se crea el recurso, `GSDecode.usf` escribe los buffers float que
  leen los pases, un thread por splat: la rotación de las tres menores, los
  logs de escala f16, el base colour f16 y los coeficientes rest, con el mismo
  mapeo de ejes, convención de escala y regla del cuaternión que usa el
  importador PLY.
- Los bounds salen de la cabecera del fichero.

**Medido.** El `.athc` del peón (60,6 MB, 5 chunks, 8 niveles) se importa sin
leer los niveles. Desde las tres cámaras, él y la exportación USD renderizan
igual: diferencia absoluta media de 3,8 × 10⁻⁶, RMS de 3 × 10⁻⁴ en una escala
de 0 a 1; los pocos píxeles que difieren más (como mucho 0,21) están en bordes
de splats, donde las rotaciones de 10 bits y los logs de escala f16 del
`.athc` difieren de los floats de USD.

## 9. La curva sRGB, no una potencia de 2,2

athenea linealiza una nube display-referred con la curva sRGB
(`cloudLight` → `srgbToLinear`). El plugin había usado la potencia 2,2 de
upstream, que se aparta de ella en las sombras. Ahora usa la curva (commit
2e98148).

## 10. Tiempos

La vista frontal del peón (325 833 gaussianas, grado 3) a 1280 × 720, desde
el nivel del PLY, en modo `-game` con el CSV profiler, un límite de 30 fps,
en la RTX 3070 Laptop GPU del portátil Windows, comprobada libre (61 °C al empezar, 65 °C
al acabar). Medianas de los últimos 300 de 450 frames:

| Etapa | Mediana | Más rápido |
|---|---|---|
| Prepass (`GPU/GaussiansPrePass`): culling, proyección, armónicos | 2,40 ms | -- |
| Sort (`GPU/GaussiansSort`) | 1,29 ms | -- |
| Raster (`GPU/GaussiansRaster`) | 5,34 ms | -- |
| Los splats, las tres juntas | 9,0 ms | -- |
| El frame entero en la GPU (`GPUTime`) | 11,90 ms | 11,44 ms |

El raster de athenea dibuja el mismo frame en **14,1 ms** (el mejor de su
ejecución) en un Apple M5 Pro. Son máquinas distintas, así que esto dice que
el plugin está en el mismo rango que el raster de athenea, no cuál es más
rápido; una comparación en una misma máquina necesita athenea compilado en
la misma máquina Windows.

Dos ejecuciones anteriores no son válidas y se conservan como lo que no hay
que repetir: una dio 440 ms por frame mientras otro trabajo (en
WSL, que `nvidia-smi` no lista) tenía la GPU al 100 % y a 780 MHz; la otra,
sin límite de frames, calentó la GPU a 87 °C y la limitó a 780 MHz durante la
ejecución, y dio 84,6 ms para los splats. Los frames se miden ahora con un
límite de 30 fps en una GPU comprobada libre, y el fichero del profiler se
copia solo cuando deja de crecer.

## 11. Lo que no está hecho

- **Transferencia (TX).** El reiluminado de athenea (`splat_relight`) portado
  al prepass, con el sky light de UE como cielo. La pieza más grande que
  queda.
- **Niveles y streaming de `.athc`.** Los niveles fusionados, el corte por
  frame y el streaming de chunks; las normales, la emisión y los extras.
- **Otros formatos.** `.athv` (vídeo HEVC), `.athz` y `.athl` (los de la
  web), `.spz`, `.splat`, `.sog`, glTF `.glb` con `KHR_gaussian_splatting`.
- **La transformación del ParticleField** (una rotación tendría que girar los
  armónicos).
- **Defectos de upstream restantes** de `DESIGN.es.md`: el desplazamiento del
  ViewRect, el GBuffer iluminado sin normalizar por el alpha, el crash del
  importador con un array de metallic/roughness vacío, los buffers
  transitorios dimensionados a `MaxSplats`, los 64 workgroups y los 32 bits
  del sort.
- **Almacenamiento f16** para las rutas PLY y USD, y su conversión en la GPU.
- **Tests automáticos** en el plugin.
