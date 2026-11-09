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
11. [Transferencia, el cuerpo y el pulido](#11-transferencia-el-cuerpo-y-el-pulido)
12. [.splat, .spz, .sog y glTF](#12-splat-spz-sog-y-gltf)
13. [Lo que no está hecho](#13-lo-que-no-está-hecho)

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

**Versión 3, la de la web.** sparkwebGPU escribe `.athc` v3 para el navegador
(su `docs/docs/athc-v3.md`): los valores de la v2 en secciones y tiers, las
secciones de cada bloque con gzip y almacenadas como las palabras de la v2,
como planos de bytes o como planos de bytes de diferencias de 16 bits, de
modo que una nube publicada es una de estas. En lugar de deshacer los planos
en la CPU, el importador conserva el `CORE` y el `SHRS` de cada bloque tal
como quedan almacenados tras el gunzip, con una tabla de bloques, y un solo
kernel lee una palabra en cualquier codificación: en su sitio para las
palabras, cuatro bytes de cuatro planos para los planos de bytes, y desde un
scratch para las diferencias, que un pase suma antes, un thread por palabra de
las filas de un bloque (las dos lanes de una palabra tienen un solo dueño, de
modo que dos threads no escriben la misma palabra). La CPU descomprime y
copia, nada más.

- **Comprobado.** El fixture `every_stream` de sparkwebGPU existe como v2 y
  como v3 en cada codificación, los mismos 300 splats (sus bytes de `CORE` y
  `SHRS` decodificados son idénticos). Capturada desde la misma cámara, cada
  v3 difiere de la v2 en 0,0001 a 0,005 de media, y la v2 difiere de sí misma
  en 0,0007 de una ejecución a otra: unos pocos splats mayores que el
  encuadre, cerca de la cámara, se dibujan o no de una ejecución a otra (desde
  otro lado, uno de ellos cubría la mitad del encuadre en unas ejecuciones y
  nada en otras). La decodificación está dentro de ese ruido; el fixture no es
  una prueba más afilada que esta desde una cámara.
- **Páginas de banda de error.** Una pieza publicada (la parte superior del
  peón, `car_M_Pawn_Top_W`, 698 791 nodos en 233 páginas) es un árbol
  paginado: cada bloque, una página de hojas y fusiones juntas, sin chunks. Su
  sección `TREE` cuenta los hijos de cada nodo; el decodificador la lee y
  dibuja una fusión sin opacidad, de modo que solo se ven las hojas, hasta que
  se lean los niveles. Sus dos arrays de una palabra se cortan de la tira de
  filas enteras que deja el escritor (la página de un nodo y el sitio de su
  primer hijo con la cuenta, una junto a otra), así que la palabra w del nodo
  e es la palabra 2e + w de esa tira. Leída como dos columnas, la mitad de
  las fusiones no contaba hijos, y en el peón entero las gruesas -- de 1 a
  2 cm sobre una pieza de 4 cm, con opacidad 0,99 -- quedaban detrás como un
  cuadrilátero inclinado y una elipse blanca. Sus unidades son metros y la pieza es pequeña: la
  cabeza de un peón de ajedrez, de 2,2 cm de ancho, así que una cámara que la
  encuadraba quedaba dentro del plano cercano de 10 cm de UE y no dibujaba
  nada; el nivel escala el actor por 100 (esta nota llamó primero mal a las
  unidades). En el grado 0 una cabecera v3
  dice una palabra de armónicos, como la palabra ficticia de la v2; la primera
  comprobación la rechazó.
- **Una nube TX desde el fichero de la web.** Las secciones se describen una
  vez (sus palabras y arrays) y cada bloque nombra el lugar, la codificación y
  el scratch de cada sección, de modo que una lectura toma cualquier palabra de
  cualquier sección y un pase suma cualquier palabra codificada por
  diferencias. Una sección CPCA (Sloan et al. 2003, la codificación 3 de la
  web) la reconstruye un thread por elemento en el scratch, en el orden de
  operaciones del decodificador de la web: la media, luego cada coeficiente por
  su paso por su dirección, en f32, y después la mitad más cercana. Un último
  kernel escribe la transferencia, metallic, la superficie (normal, roughness,
  lobes, los bits de la palabra pbr), las celdas y el campo en los buffers que
  llenan los arrays del importador de USD, de modo que el shading no sabe de
  dónde vinieron.
- **Comprobado contra el USD.** La nube de prueba TX de athenea
  (`paint_tx.usdc`, 272 907 gaussianas, transferencia de 112 valores, celdas
  de 16 × 16, el material) escrita a `.athc` v3 con `usd-athc` de sparkwebGPU
  (las palabras de la v2, gzip), y después con su `athc-convert --planes`
  (cada sección en su codificación más pequeña: `CORE` y `TXIN` como planos de
  bytes, `MATL` como planos de diferencias, el resto palabras) y
  `--cpca 16,48` (la transferencia como CPCA: de 51,8 MB a 6,7 MB). Cada una
  dibujada bajo el mismo sky light desde la cámara de prueba, capturas lineales
  contra las del USD:

  | Fichero | Media | RMS |
  |---|---|---|
  | palabras, gzip | 0,00108 | 0,0045 |
  | codificaciones más pequeñas | 0,00109 | 0,0046 |
  | CPCA | 0,00142 | 0,0052 |

  Sobre valores de hasta 3. Las dos primeras son la transferencia f16 del
  fichero, el material en bytes y las normales de 16 bits contra los floats
  del USD; CPCA añade 0,0003, su propia pérdida. Una primera importación CPCA
  se rechazó: el tamaño en bruto de una sección CPCA en el índice de bloques es
  su tamaño decodificado, no el de su payload, y el gunzip se comprobaba contra
  él. El `.athc` guarda metros, de modo que su nivel no lleva ×100 (el stage
  USD dice centímetros con números de metros, y su actor sí); una primera
  captura con ×100 puso la cámara dentro del suelo.
- **El `.athc` v2 propio de athenea con una transferencia** sigue el mismo
  camino: un chunk v2 dispone los mismos arrays (posiciones, forma,
  armónicos, cola, el material, la transferencia, las celdas), de modo que el
  importador conserva los bytes de cada chunk y describe los arrays donde
  están, cada sección en palabras de la v2 y la transferencia entera como una
  sola sección. No se añadió ningún kernel. La nube de pintura escrita como v2
  (`athc-convert --v2`, 115,8 MB) contra el USD: media 0,00113 (RMS 0,0049);
  contra la v3 en palabras, 0,0006. Un fichero v2 de grado 0 escrito por las
  herramientas de la web guarda una palabra de armónicos, que la comprobación
  de la cabecera v2 rechazaba; ahora se acepta.

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

## 11. Transferencia, el cuerpo y el pulido

Una nube TX no lleva radiancia: su color es un albedo, y un frame la ilumina a
partir de la transferencia que athenea horneó en cada gaussiana. La primera
etapa del port es el cuerpo: `albedo (1 − metallic) ⟨T_direct + T_indirect, L⟩`,
con `L` la radiancia del cielo sobre los mismos 16 armónicos, el
`transferredBody` de athenea.

**El cielo.** El sky light de UE, proyectado en cada frame por un pase de
compute sobre esos armónicos en el marco propio de la nube (development
§5.2.1). Comprobado leyendo de vuelta de la GPU los 16 coeficientes y
proyectando el mismo HDRI en Python con la convención de domo de athenea
(UsdLux): con `SourceCubemapAngle` 180 coinciden a menos del 2,8 %, UE un
2,8 % más brillante. Una búsqueda entre los 48 mapeos de ejes con signo y los
giros alrededor de la vertical no encontró ningún espejo: el ángulo es lo
único que hay que fijar.

**La prueba.** La escena de la bola de pintura de athenea convertida con
`mesh2splat --transfer` (272 946 gaussianas, 16 valores directos y 48
indirectos cada una), bajo el mismo HDRI de estudio en ambos, desde la cámara
de la escena. La bola es un metal (metallic 1): no tiene cuerpo, y se dibuja
negra hasta que se porte el pulido. El suelo (albedo 1, metallic 0):

| | raster de athenea | UE, el cuerpo |
|---|---|---|
| media del suelo, lineal | 1,046 | 0,92 |
| el 5 % más oscuro (la sombra de la bola) | 0,372 | 0,38 |

Los valores de UE son los de la captura (operations §7.1) divididos entre su
exposición, 1,72, medida a partir de la radiancia media del cielo proyectado.
La sombra coincide; la media del suelo es un 12 % más baja, que es lo que
athenea añade mediante el sol que separa de este cielo (su log: un sol de
14,8° de diámetro) y con el que ilumina a través de los bits de direcciones
abiertas, todavía sin portar.

**El pulido.** El resto de la suma de athenea para una superficie opaca vino
después, función por función (`GSPolish.ush`): las capas de coat y sheen con
el oscurecimiento del coat de OpenPBR, las BRDF de entorno (dieléctrica con su
Fresnel exacto, conductora mediante el índice artístico de MaterialX, el metal
de Schlick), las celdas de direcciones abiertas leídas a lo largo del lóbulo
especular, y el campo reflejado acoplado al cielo por la transferencia
indirecta. Para ello el cubemap del sky light se sube a 1024 texels por cara
(128 por defecto). Medido sobre el mismo frame, frente a athenea, con el disco
de la bola ajustado a partir de la imagen de UE:

| | athenea | UE | UE / athenea | error relativo absoluto medio |
|---|---|---|---|---|
| bola | 0,058 | 0,071 | 1,22 | 0,47 |
| bola, mitad superior (el cielo) | 0,081 | 0,102 | 1,26 | 0,60 |
| bola, mitad inferior (el suelo, mediante el campo) | 0,035 | 0,041 | 1,16 | 0,16 |
| suelo | 1,032 | 0,934 | 0,91 | 0,11 |

Antes del campo la mitad inferior de la bola era negra: sus direcciones
cerradas no reflejaban nada.

**El sol.** athenea saca el sol de un cielo de sus armónicos y lo devuelve
como una luz direccional, sombreada por las celdas a lo largo de él y rebotada
a través de la transferencia indirecta (`env_sun`, `transferViewless`). El
pase del cielo hace ahora lo mismo (development §5.2.1): sobre 16384
direcciones, un histograma de la luminancia logarítmica da la mediana, el sol
es lo que queda por encima de max(16 mediana, 0,02 pico) a menos de diez
grados del pico allí donde el pico es 32 veces la mediana, y se conserva solo
donde es tan compacto como un disco. Para este HDRI UE lo encuentra en
(−0,37, 0,58, 0,72), con 17,4° de diámetro e irradiancia 1,49; athenea en
(−0,51, 0,63, 0,59), 14,8°, 1,35 (su propio recorrido del lat-long). Las
reflexiones todavía leen los mips prefiltrados de UE, que conservan el sol.

**Un viñeteo, no la transferencia.** Las tablas de arriba se midieron a
través del post-proceso de UE, cuyo viñeteo (0,4 por defecto) oscurece el
frame hacia sus bordes: una vista de depuración de un único color plano se
leía 1,27 en el centro y 0,95 abajo. Con el viñeteo desactivado en la
captura, el color plano es el mismo en todas partes y el factor de exposición
de la captura es 2,02. Medido de nuevo, sol incluido:

| | athenea | UE | UE / athenea | error relativo absoluto medio |
|---|---|---|---|---|
| suelo | 1,032 | 1,048 | 1,016 | 0,024 |
| bola, mitad inferior (el campo) | 0,036 | 0,037 | 1,046 | 0,091 |
| bola, mitad superior (la reflexión del cielo) | 0,082 | 0,094 | 1,156 | 0,554 |
| bola | 0,059 | 0,066 | 1,123 | 0,413 |
| la sombra de la bola (el 5 % más oscuro de athenea) | 0,270 | 0,304 | 1,127 | 0,172 |

El cuerpo y el campo coinciden; lo que queda está en la reflexión del cielo
en la bola: el sol todavía en los mips prefiltrados de UE y sin sombra allí,
y la pendiente de una reflexión nítida a lo largo de un splat, que athenea
dibuja y esto no (sus ventanas se difuminan).

**El cielo sin su sol en las reflexiones.** athenea prefiltra su cielo con el
sol cortado (`env_residual`) y refleja el sol analíticamente a través de cada
lóbulo (`sunReflectPdf`): la suma dividida se queda una cuarta parte corta para
un sol compacto. El plugin hace lo mismo: un cubo cortado, el cubo del sky
light leído a 256 texels por cara con los texels del sol (el corte de env_sun:
a menos de diez grados del sol y tan brillantes como el umbral con el que se
encontró) puestos a nada, filtrado por caja hacia abajo por sus mips; luego el
residual, cada mip muestreado con GGX a partir del corte a la rugosidad en la
que las capturas de UE lo ponen (128 muestras, cada una leída en el nivel del
corte que corresponde a su ángulo sólido). Medido sobre el mismo frame frente a
athenea (`tx_regions.py`):

| | cubo prefiltrado de UE | residual muestreado de los mips de UE | residual a partir del corte |
|---|---|---|---|
| bola | 1,117 (0,317) | 1,121 (0,394) | 1,081 (0,239) |
| bola, mitad superior | 1,181 (0,569) | 1,169 (0,704) | 1,124 (0,437) |
| bola, mitad inferior | 0,994 (0,067) | 1,027 (0,086) | 0,996 (0,043) |
| suelo | 1,017 (0,026) | 1,023 (0,031) | 1,012 (0,022) |

UE / athenea, con el error relativo absoluto medio. La columna del medio fue la
primera versión: cortaba el sol de los propios mips de UE, que están
prefiltrados a partir de todo el cielo, así que su sol es un halo bajo
cualquier umbral; se quedaba en el mapa y el sol analítico venía encima. Los
mips de caja del corte no contienen sol alguno. Lo que queda en la mitad
superior de la bola es la nitidez: athenea dibuja un coat liso por píxel (su
pendiente de reflexión) y muestra la sala en él, cosa que un color por splat no
puede.

**Tiempo** (`ue_time.sh /Game/TestTx`, RTX 3070, 1280 × 720, medianas de 300
frames): construir el residual en cada frame costaba 1,3 ms de GPU
(`GPU/GaussiansSky` 1,69 ms frente a 0,40 ms de la proyección sola); es del
mundo, así que se construye una vez por cubo y color de un sky light y se
conserva, compartido por todas las nubes TX. Conservado, `GPU/GaussiansSky` es
0,397 ms frente a 0,389 ms con el residual apagado, y `GPUTime` 11,90 ms frente
a 11,69 ms.

**Los lóbulos nítidos, por píxel.** Lo que el residual dejó en la mitad
superior de la bola fue la nitidez: athenea lee por píxel un coat suave (por
debajo de 0,2) y un pulido de base nítido (por debajo de 0,5), a lo largo del
espejo al que gira cada píxel según la curvatura de la superficie, y el color
único de un splat no puede mostrar una ventana reflejada dentro de él.
Portado (el lóbulo nítido de `SplatSlope` de athenea, `splat_project`,
`splat_blend`): se lee el operador de forma de la nube
(`primvars:athenea:splat:curvature`, `CURV`), el prepass gira la normal un
píxel a la derecha y hacia abajo, saca del color la lectura del centro y
entrega dos palabras de payload al raster, que lee de nuevo el residual y el
sol en cada píxel. Medido en el mismo frame:

| | el residual | y los lóbulos nítidos |
|---|---|---|
| bola | 1,081 (0,239) | 1,030 (0,124) |
| bola, mitad superior | 1,124 (0,437) | 1,048 (0,213) |
| bola, mitad inferior | 0,996 (0,043) | 0,997 (0,035) |
| suelo | 1,012 (0,022) | 1,012 (0,022) |

UE / athenea (error relativo absoluto medio). La ventana y las dos luces que
refleja el coat caen ahora donde las dibuja athenea. La nube de pintura como
`.athc` v3 (las codificaciones más pequeñas, y CPCA) da lo mismo (bola 1,033,
mitad superior 1,051); su v2 no conserva curvatura y se queda en la del
residual (1,085). `GPUTime` 11,84 ms frente a 12,07 ms con el residual
apagado: dentro del ruido entre ejecuciones.

**Vidrio.** El vidrio de una nube TX salía opaco en UE: el plugin no leía la
transmisión. La lente del raster de athenea se porta tal cual (`lensExit`,
`lensExitThrough`, `lensFarReflection`, `transmittedRoughness`, la rama del
vidrio sólido y el cuerpo como `body2`), con la transmisión y el bit de pared
fina en la ranura libre de la superficie y el índice como un valor de la nube.
El cristal del dragón -- surfels sin malla, horneados por el nuevo
`splat-transfer` de athenea -- se dibuja en UE como el vidrio lila claro que
dibuja el raster de athenea desde la misma cámara. Una pared fina se dibuja
con la opacidad de su fichero y su pulido base escalado por el
`thinWallGain` de athenea; no había a mano una nube TX con paredes finas para
medirla contra el fotograma de athenea.

Dos errores primero: la primvar de curvatura tiene tres valores por gaussiana
y se leía con el lector de uno por gaussiana, que la descartaba (la vista de
depuración 6 muestra qué condición se cumple); y un bloque de versión 3
crecido por la sección `CURV` a 29 palabras se rellenaba hasta 34, no los 32
por los que avanza el shader, de modo que leía las secciones de cada bloque
posterior desde un lugar equivocado (la bola CPCA salió al 3 % de athenea).

**Medidas que primero fueron erróneas, conservadas como lo que no hay que
repetir.** La captura HDR de UE satura cerca de 1, así que leía el suelo un
26 % oscuro y cada coeficiente proyectado como 1 aproximadamente; y el stage
de prueba dice centímetros (su root layer no fija unidades) mientras que sus
números son metros, así que la nube entró primero con 2 cm de diámetro.

## 12. .splat, .spz, .sog y glTF

El usuario pidió todos los formatos. Estos cuatro son los más comunes después
de PLY; athenea lee los tres primeros (`io/SplatReaders.cpp`, `io/Sog.cpp`) y
escribe el cuarto; el plugin los lee como lee un `.athc`: se conservan los
bytes del fichero, y un kernel por formato los decodifica (`GSDecodeSplatCS`,
`GSDecodeSpzCS`, `GSDecodeSogCS`, `GSDecodeGltfCS`).

- **Compresión.** Las versiones 2 y 3 de `.spz` son gzip, que deshace el zlib
  de UE; la versión 4 es zstd y `.sog` es WebP, y UE no incluye ninguno de los
  dos. Con el OK del usuario, el código fuente de sus decodificadores se tomó
  de los repositorios propios de los proyectos (zstd v1.5.7, libwebp v1.5.0,
  ambos BSD) y se puso en un módulo propio, `AtheneaCodecs`, solo de editor.
- **Ejes.** athenea gira un SPZ, derecha-arriba-atrás según el formato,
  negando y y z hasta la derecha-abajo-delante de un PLY 3DGS, el sistema en
  que lee sus PLY. El plugin compone ese giro con su mapeo de PLY, `(z, x, −y)`,
  en `(−z, x, y)`, de modo que un SPZ queda de pie como un PLY. Una primera
  versión tomó el sistema de PLY de athenea como y arriba, `(x, −z, −y)`, lo
  que dejaba derecho un gato pintado -- como lo deja la vista por defecto de
  athenea, que lee el sistema de un PLY como y arriba (las nubes de COLMAP
  piden allí `--rotate-x 180`) -- y dibujaba boca abajo un SPZ de versión 4
  escrito por splat-transform, frente al `.glb` que la misma herramienta
  escribió de la misma nube. El gato tiene coordenadas y abajo sin que la
  extensión de sistema de coordenadas lo diga: el fichero es el que está mal,
  y ahora queda boca abajo, como en athenea con una vista y arriba. Un
  `.splat` conserva los ejes de COLMAP, como un PLY 3DGS.
- **Comprobado.** Un SPZ de versión 2 (el gato, 206 232 gaussianas, rotaciones
  first-three), uno de versión 3 (404 092, smallest-three) y un `.splat` (el
  enano, 524 288) se importan y se dibujan. Una versión 4 (el `biker.spz` de
  PlayCanvas, 152 746, grado 0) se rechazó al principio: el grado 0 tiene
  cinco streams, no seis; ahora se lee.
- **`.sog`** sigue `io/Sog.cpp` y `scene/sog_decode.slang` de athenea: el zip
  y las imágenes WebP se deshacen en la CPU (libwebp, de `AtheneaCodecs`), y
  el resto es un kernel. Comprobado con el fixture propio de athenea,
  `sh3.sog` (64 gaussianas, versión 2, una paleta de grado 3), frente al PLY
  del que se escribió, ambos por los mismos ejes y espacio de color:
  diferencia media 0,0017 (RMS 0,015) entre los dos frames, que es la
  cuantización de SOG -- los dos frames del mismo par en athenea difieren en
  0,0014. Una primera ejecución leía el ancho de la paleta y sus coeficientes
  de color en los slots del otro, y los armónicos dibujaban un color negativo
  recortado a negro.
- **glTF** (`KHR_gaussian_splatting`) se lee como lo lee glTF: accessors de
  cualquier tipo de componente, normalizados o no, con cualquier stride. En
  vez de un kernel por cada disposición, el importador escribe una tabla de
  descriptores de accessor después de los buffers y un solo kernel lee
  cualquiera de ellos, así que un fichero cuantizado
  (`KHR_mesh_quantization`, el `COLOR_0` normalizado de PlayCanvas) no cuesta
  nada más. Los bounds son el `min` y el `max` de `POSITION`, que glTF exige,
  así que no queda aritmética sobre los datos en la CPU.
- **Los ejes de glTF** no son los de un SPZ: también +Y arriba, pero +Z es el
  frente del asset, donde el +Z de un SPZ es su parte de atrás. Los dos
  ficheros que splat-transform escribió de una misma nube (`biker.glb`,
  `biker.spz`) quedaron al principio de espaldas uno al otro; ahora glTF se
  toma como `(z, −x, y)`, y los dos frames difieren en 0,00044 de media (RMS
  0,0020), que son los colores de 8 bits y el punto fijo del SPZ.
- **El importador de glTF propio de UE** también toma `.glb`. El factory
  responde por un fichero solo cuando su JSON nombra la extensión, así que un
  `.glb` de malla se queda con Interchange; al soltarlo en el Content
  Browser, cuál de los dos pregunta UE primero no se ha comprobado (los
  scripts nombran el factory).

**El `.athz` de la escena web.** Lo que publica la web es un zip de una
escena y sus nubes `.athc`, así que con `.athc` v3 leído, la escena es un
bucle sobre sus ficheros: cada nube por el mismo lector, el cielo por el
importador HDR propio de UE. Una importación de UE crea un solo asset, así que
la primera nube es el asset pedido y las demás se crean a su lado. El pawn
publicado (`athenea-pawn.athz`, 455 MB: dos piezas del cuerpo de 1,78 M nodos
cada una, la parte superior de 0,70 M, transferencias CPCA, páginas de banda
de error) se importa de una vez, y se reilumina bajo su cielo. Lo que muestra
más allá del pawn (un cuadrado grande inclinado y una elipse blanca detrás) no
se entiende todavía: probablemente son nodos fusionados de las piezas del
cuerpo que se dibujan cuando deberían estar ocultos.

**El `.athz` de athenea, decodificado en UE.** El decodificador es el de
athenea (`athz_decode.slangh`, `athz_tables.slang`), portado a HLSL tal cual:
un grupo de 256 hilos son ocho tandas de 32 carriles, la tabla rANS de cada
plano en memoria de grupo, los carriles renormalizando desde un solo flujo de
palabras mediante una máscara, las transformaciones de prefijo sumadas a lo
largo de una tanda, las transformaciones de padre leídas del nivel superior. Su
salida son los arrays de un chunk de `.athc` en el scratch, así que los splats
pasan luego por los decodificadores de la versión 3 con una codificación más
(4: las palabras ya están en el scratch), la transferencia incluida. La parte
superior del peón (`top.athz`, versión 3, 292 056 gaussianas, 7 niveles
fusionados, 129 páginas, transferencia de 112 valores) frente a su fuente USD
(`M_Pawn_Top_W.usdc`) bajo el mismo cielo: diferencia media de 0,0019 en un
frame cuyos brillos llegan a 7; el `.athz` lleva el campo reflejado que el USD
de aquí no lleva. Los niveles fusionados se decodifican solo como padres de los
splats.

## 13. Lo que no está hecho

- **Transferencia (TX), más allá del pulido opaco:** una pared fina medida
  contra el fotograma de athenea, una pendiente a lo largo
  de un splat para los lóbulos que no son nítidos (`slopeX`, `slopeY` de
  athenea), el ensanchamiento de un pliegue (su `bakeSpread`), la curvatura de
  un fichero v2, las luces analíticas, la transferencia zonal de las nubes con skinning, y una captura del cielo en
  tiempo real.
- **Niveles y streaming de `.athc`.** Los niveles fusionados, el corte por
  frame y el streaming de chunks; las normales, la emisión y los extras. En la
  versión 3 la curvatura, el codebook de oclusión, una transferencia zonal, el
  skin y el tier compacto; las fusiones de un árbol paginado se decodifican y
  se ocultan, no se omiten.
- **Otros formatos.** `.athv` (vídeo HEVC), `.athl` (las capas de luz de la
  web), la capa K de un `.athz`; la extensión de sistema de coordenadas de un SPZ; la transformación de
  un nodo de glTF, más de una primitiva de gaussianas, los accessors sparse y
  `EXT_meshopt_compression`.
- **La transformación del ParticleField** (una rotación tendría que girar los
  armónicos).
- **Defectos de upstream restantes** de `DESIGN.es.md`: el desplazamiento del
  ViewRect, el GBuffer iluminado sin normalizar por el alpha, el crash del
  importador con un array de metallic/roughness vacío, los buffers
  transitorios dimensionados a `MaxSplats`, los 64 workgroups y los 32 bits
  del sort.
- **Almacenamiento f16** para las rutas PLY y USD, y su conversión en la GPU.
- **Tests automáticos** en el plugin.

## El paquete que hace BuildPlugin (8 nov)

`RunUAT BuildPlugin` compila el plugin sin unity build, cada fichero solo,
donde la compilación de un proyecto los había juntado. `GSplatImporter.h`
nombraba `class UGSplatAsset&` dentro de `namespace GSplatImporter`, lo que
declara un `GSplatImporter::UGSplatAsset` nuevo y vacío; juntos, un fichero
que había visto la clase real iba antes, y solo `GSplatSceneImporter.cpp`
fallaba (C2664, C2027). La cabecera declara ahora `UGSplatAsset` en el ámbito
global, antes del namespace.

Después, la compilación se paraba en ficheros que usaban tipos que nunca
incluían y que traía la cabecera precompilada compartida del proyecto: los del
RHI (`RHIFwd.h`, `RHIResources.h`, `RHIFeatureLevel.h`), los del render graph
(`RenderGraphFwd.h`, `RenderGraphResources.h`), `FGlobalShader`
(`GlobalShader.h`, `ShaderParameterStruct.h`), `TAutoConsoleVariable`
(`HAL/IConsoleManager.h`), `FPaths` y los mapeos de directorios de shaders
(`Misc/Paths.h`, `ShaderCore.h`), `TQueue` (`Containers/Queue.h`),
`FPackageName` y `UPackage` (`Misc/PackageName.h`, `UObject/Package.h`), y
`FSceneView`, declarado donde solo se nombra una referencia. En
`GaussianSplatInterface.h` los tipos del RHI eran la causa de los errores de
`override` del proxy: sin declarar, se caían los tres métodos del contador de
índices de la interfaz. Cada fichero incluye ahora lo que usa, y el paquete
compila.
