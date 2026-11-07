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
| nube por chunks de athenea | `.athc` (versiones 1 y 2, las de athenea; 3, la de la web) | no | los de athenea: y arriba, metros | sRGB salvo con el bit de flag 1 (lineal) | los coeficientes rest del fichero (0 a 15 por color) | el empaquetado de athenea, decodificado en la GPU |
| splat de antimatter15 | `.splat` | no | los de COLMAP (derecha, abajo, delante), como un PLY 3DGS; escala 100 | sRGB | ninguno | sus registros de 32 bytes, decodificados en la GPU |
| PlayCanvas SOG | `.sog`, o el `meta.json` de un SOG sin empaquetar | no | como un PLY 3DGS (los de COLMAP) | sRGB | grado 0 a 3, a través de su paleta | los texels de sus imágenes y sus rangos y codebooks, decodificados en la GPU |
| glTF | `.glb`, `.gltf` con `KHR_gaussian_splatting` | no | los de glTF (+Y arriba, +Z el frente): (z, −x, y) hacia UE, así que una nube cae como su SPZ | sRGB, o lineal donde `colorSpace` dice `lin_...` | `SH_DEGREE_l_COEF_n`, grado 0 a 3 | sus buffers y sus accessors, leídos en la GPU en cualquier tipo de componente |
| SPZ de Niantic | `.spz` (versiones 2, 3, 4) | no | derecha-arriba-atrás, girado como athenea lo gira hasta el sistema de un PLY y luego como un PLY: (−z, x, y) hacia UE | sRGB | grado 0 a 3 (la cuarta banda de un fichero de grado 4 se descarta) | sus seis streams, descomprimidos, decodificados en la GPU |

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
| `primvars:athenea:splat:transferDirect` | la transferencia directa de una nube TX, 9 o 16 valores por gaussiana (`elementSize`); su presencia hace que el cielo ilumine la nube (§6) |
| `primvars:athenea:splat:transferIndirect` | su mitad indirecta, el triple de valores (rgb por coeficiente), que se conserva junto a la directa |
| `primvars:athenea:splat:metallic` | con una transferencia: lo que reduce el cuerpo (1 − metallic), y la parte del metal en el pulido |
| `primvars:athenea:splat:normal`, `:roughness`, `:materialRoughness` | con una transferencia: la superficie con la que refleja el pulido (la normal en el marco del fichero) |
| `:specularWeight`, `:specularColor`, `:specularIor`, `:schlickMetal` | el lóbulo especular (valores predeterminados 1, blanco, 1,5, desactivado) |
| `:coatWeight`, `:coatRoughness`, `:coatIor`, `:coatDarkening` | el coat sobre la base (predeterminado: ninguno) |
| `:sheenColor`, `:sheenRoughness` | el sheen (predeterminado: ninguno) |
| `primvars:athenea:splat:shadowBits` | las direcciones abiertas: 8 o 32 palabras por gaussiana (una rejilla octaédrica de 16 × 16 o 32 × 32 sobre la esfera entera); las 2 palabras de la primera transferencia no se leen |
| `primvars:athenea:splat:transferReflected` | el campo reflejado, 48 valores por gaussiana, junto a la mitad indirecta |
| `primvars:athenea:splat:curvature` | con una transferencia: el operador de forma, 3 valores por gaussiana (`elementSize` 3: uu, uv, vv en los dos primeros ejes de la gaussiana, uno sobre las unidades del stage); con lo que los lóbulos nítidos giran a lo largo de un splat (§6.1) |
| `upAxis` del stage | `Y` → (X, Z, Y); `Z` → (X, −Y, Z) |
| `metersPerUnit` del stage | posiciones multiplicadas por él y por 100 |

Todavía no se lee de una nube TX: las paredes finas, el vidrio
(`transmission`, el índice), la emisión, ni la transferencia zonal de una nube
con skinning.

Los valores se leen en la primera muestra de tiempo, así que una nube cuyos
arrays están animados importa su primer frame. La transformación propia del
prim no se aplica; el importador avisa cuando no es la identidad, y en su
lugar se coloca el actor.

### 4.4 Ficheros `.athc`

Lo que se lee: la cabecera, la tabla de chunks, y de cada chunk las posiciones
(float4: xyz en metros, opacidad lineal), las cuatro palabras de forma y las
palabras de armónicos, copiadas sin cambios en el asset. Se leen las versiones
1 y 2.

La versión 3 (`ATH3`, escrita por `athc-convert` y `usd-athc` de sparkwebGPU
para la web) guarda los mismos valores en secciones, las secciones de cada
bloque con gzip o sin él y almacenadas como las palabras de la v2
(codificación 0), como planos de bytes (1) o como planos de bytes de la
diferencia de cada lane de 16 bits con el elemento anterior (2). Lo que se
lee: la cabecera, la tabla de secciones, el índice de bloques, y de cada chunk
(bloque de tipo 1) o página de banda de error (tipo 2) las secciones `CORE` y
`SHRS`, descomprimidas con gunzip y conservadas tal como están almacenadas; la
GPU las lee en cualquiera de las tres codificaciones (las diferencias se suman
primero, un thread por palabra de un bloque). Las páginas de un fichero
paginado contienen todo su árbol de fusiones: se decodifica cada nodo, y un
nodo fusionado (su entrada de `TREE` cuenta hijos) se dibuja sin opacidad, de
modo que solo se ven las hojas. Los bounds salen de la cabecera.

También se leen el material y la transferencia de una nube TX, cuando la
transferencia es una que el plugin dibuja (16 o 9 valores directos, con o sin
la mitad indirecta y el campo: 112, 64, 16, 84, 36, 9 valores; una
transferencia zonal de 10 no se dibuja): `MATL` (la normal, metallic,
roughness, los bits de Schlick y de roughness del material, los lobes),
`SHAD` (celdas de 16 × 16 o 32 × 32) y `TXDI`, `TXIN`, `TXFD` en cualquiera de
las codificaciones o como CPCA (codificación 3, los clusters de la
transferencia, reconstruidos en la GPU a las mitades que da el decodificador
de la web) y `CURV` (el operador de forma, tres halves). El dispositivo las
escribe en los mismos buffers que llenan los arrays de una nube TX de USD, de
modo que la nube se reilumina bajo el sky light como describe el §6. No se
lee la curvatura de un fichero de versión 2 (la v2 de athenea y la de la web
usan el bit de flag 6 para cosas distintas).

No se leen: los niveles fusionados, `LODS`, `LODE`, `LODM`, `OCCL`,
`SKIN`, el esqueleto y el tier compacto. Un fichero escrito en otras unidades
que metros (las piezas web del peón) se coloca con la escala del actor.

Una nube TX de versión 2 (bit de flag 5) también tiene leídos su material
(bit 4), su transferencia y sus celdas abiertas, mediante los decodificadores
de la versión 3 de más abajo con cada sección en palabras de la v2: los bytes
de cada chunk se conservan tal como están en el fichero, y sus arrays se
describen donde la v2 los coloca. Sus palabras de armónicos pueden ser una en
grado 0 (el escritor de la web guarda una palabra ficticia, como permite el
layout de athenea).

Todavía no se lee: los niveles fusionados (los niveles de detalle del
fichero), la radiancia emitida (bit 2), las normales de una nube sin
transferencia, ni la curvatura y los tamaños de LoD (bit 6). La nube dibuja su
nivel más fino, completo.

### 4.5 Ficheros `.splat`, `.spz`, `.sog` y glTF

- **`.splat`** (antimatter15): registros de 32 bytes, posición float × 3,
  escala lineal float × 3, bytes RGBA (el color tal como se muestra y la
  opacidad), y la rotación como bytes w, x, y, z, cada uno (v − 128) / 128. El
  fichero se copia tal cual.
- **`.spz`** (Niantic): las versiones 2 y 3 son gzip alrededor de una cabecera
  de 16 bytes (`NGSP`, versión, recuento, grado, bits fraccionarios, flags) y
  seis streams; la versión 4 es una cabecera de 32 bytes, una tabla de
  contenidos y seis streams zstd. Los streams -- posiciones en punto fijo de
  24 bits, bytes de opacidad, bytes de color ((c / 255 − 0,5) / 0,15 del
  coeficiente constante), bytes de log de escala (s / 16 − 10), rotaciones
  (first-three en la versión 2, smallest-three desde la 3) y bytes de
  armónicos ((h − 128) / 128) -- se guardan descomprimidos y se decodifican
  en la GPU. La versión 4 sin armónicos tiene cinco streams. Las extensiones
  de sistema de coordenadas no se leen: se toma derecha-arriba-atrás, y se
  gira como lo gira athenea (negando y y z hasta la derecha-abajo-delante de
  un PLY 3DGS), y luego como entra un PLY, así que un fichero escrito como
  dice el formato queda de pie. Un fichero que tiene coordenadas y abajo sin
  que la extensión lo diga queda boca abajo, como en athenea con una vista y
  arriba (la vista por defecto de athenea lo muestra derecho, y un fichero
  conforme boca abajo: lee el sistema de un PLY como y arriba, por lo que las
  nubes de COLMAP piden allí `--rotate-x 180`). La versión 1 (posiciones
  float16, nunca publicada) se rechaza.
- Ni `.splat` ni `.spz` guardan sus bounds: el importador los toma de las
  posiciones, la única aritmética sobre los datos que queda en la CPU.
- **`.sog`** (PlayCanvas, versiones 1 y 2): un zip (stored o deflated, sin
  zip64) de imágenes WebP sin pérdida -- `means_l`, `means_u`, `quats`,
  `scales`, `sh0`, y opcionalmente `shN_labels` y `shN_centroids` -- y un
  `meta.json` de rangos (versión 1) o de codebooks de 256 entradas
  (versión 2); o los mismos ficheros sin empaquetar, que se importan por su
  `meta.json`. La CPU abre el archivo, decodifica cada imagen a sus texels
  (RGBA directo: son índices y enteros empaquetados, nunca premultiplicados)
  y analiza los metadatos; la GPU lee las posiciones del dominio logarítmico
  con signo de 16 bits, el color y la opacidad, las escalas en log y la
  rotación smallest-three, y los armónicos a través de la paleta, como hace
  el `sog_decode` de athenea. Sus bounds salen de los rangos de posición (el
  mapeo logarítmico conserva su orden). Ejes como los de un PLY 3DGS.
- **glTF** (`.glb`, o `.gltf` con sus buffers en ficheros contiguos o en URIs
  `data:`): la primera primitiva `POINTS` con `KHR_gaussian_splatting` --
  `POSITION`, `KHR_gaussian_splatting:ROTATION` (x y z w), `:SCALE` (lineal),
  `:OPACITY` (lineal) y `:SH_DEGREE_l_COEF_n` (rgb), con `COLOR_0` como color
  donde falta el coeficiente constante (y su alfa como opacidad donde falta
  `OPACITY`) -- la disposición que escriben el `flatten` de athenea y el
  splat-transform de PlayCanvas. La CPU analiza el JSON y copia los buffers;
  cada accessor se lee en la GPU desde una tabla de descriptores (dirección,
  stride, tipo de componente, normalización), así que se leen floats y
  enteros normalizados (`KHR_mesh_quantization`) y vistas intercaladas. Sus
  bounds son el `min` y el `max` de `POSITION`, que glTF exige. Ejes: +Y
  arriba de glTF, +Z el frente del asset y +X su izquierda -- la
  derecha-arriba-atrás de un SPZ girada media vuelta sobre la vertical --,
  tomados como `(z, −x, y)`, de modo que una nube escrita en ambos formatos
  cae en el mismo sitio y mirando al mismo lado. La transformación del nodo
  no se aplica (un aviso lo dice; colocar el actor en su lugar), las demás
  primitivas de gaussianas se dejan fuera (un aviso las cuenta), y los
  accessors sparse y cualquier otra extensión requerida
  (`EXT_meshopt_compression`, ...) se rechazan. Los `.glb` de malla, sin la
  extensión, se dejan al importador de glTF propio de UE.

### 4.6 Mensajes

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
| `the extra header is cut short.` / `a transfer of <t> values in <w> words, which is not drawn.` / `chunk <c> is cut short.` / `its chunks hold <n> splats where the header says <m>, or past 4 GB.` | una nube TX de versión 2 que esto no lee |
| `a version 3 header cut short.` / `ATH3 version <v>, this reads 3.` / `a version 3 header that does not add up (...)` | una cabecera `ATH3` que no lo es |
| `the section table is cut short.` / `the block index is cut short.` / `no CORE section.` / `CORE of <w> words a splat, where 9 were due.` | las tablas de un fichero de versión 3, rotas |
| `section <s> in encoding <e>, compression <c>, which this does not read.` | una sección que se lee aquí almacenada como las pasadas compactas, como CPCA fuera de la transferencia o con una compresión distinta de gzip |
| `a block's section <s> holds <n> bytes where <m> were due.` / `a block runs past the file.` / `a block's section <s> does not gunzip to its <n> bytes.` | un bloque cortado o roto |
| `its chunks or pages hold <n> splats where the header says <m>.` / `<n> chunks, more than one dispatch takes.` | los bloques no suman lo que dice la cabecera, o son más de 65 535 |
| `a header that does not add up (...)` | los recuentos de la cabecera del `.athc` se contradicen |
| `the chunk table is cut short.` / `chunk <c> is cut short.` | el fichero termina antes de tiempo |
| `chunk <c> holds <n> splats, where <m> were due.` | el recuento de un chunk difiere del que implica la cabecera |
| `<n> bytes is not a whole number of 32-byte .splat records.` | un `.splat` cortado o que no lo es |
| `not a readable gzip stream.` | un `.spz` que empieza como gzip y no se descomprime |
| `an SPZ header this reads (NGSP, version 2 or 3) was not found (version <v>).` | el gzip no contenía ninguna cabecera SPZ, o contenía otra versión |
| `an SPZ header this reads (NGSP, version 4) was not found (version <v>).` | un `.spz` sin comprimir que no es de versión 4, o cuya tabla de contenidos queda fuera del fichero |
| `stream <s> runs past the file.` / `stream <s> does not decompress.` | un stream de versión 4 cortado o roto |
| `degree <d>, <b> fractional bits, <n> points: not a cloud this reads.` | una cabecera SPZ fuera de rango |
| `<n> bytes of streams where <m> were due.` | los streams son más cortos de lo que dice la cabecera |
| `not a zip bundle` / `a broken zip directory` / `a broken zip entry` / `a truncated zip entry` | un `.sog` que no es un zip legible |
| `zip64 bundles are not read` / `zip method <m> for '<name>'` / `'<name>' does not inflate` | un zip que este lector no admite |
| `no meta.json.` / `meta.json does not parse.` / `meta.json names no splats.` | los metadatos del SOG faltan o están rotos |
| `SOG version <v>.` | no es la versión 1 ni la 2 |
| `a version 2 SOG without its 256-entry codebooks.` | un `meta.json` de versión 2 sin sus codebooks |
| `no <image>.` / `<image> is not a WebP image.` / `<image> holds fewer texels than <n> splats.` | una imagen de atributo ausente, rota o demasiado pequeña |
| `glTF binary version <v>.` / `a glb chunk runs past the file.` / `a glb without its JSON chunk.` / `its JSON does not parse.` | un `.glb` que no es un binario glTF 2.0 legible |
| `requires <extension>, which is not read.` | `extensionsRequired` nombra algo distinto de `KHR_gaussian_splatting` y `KHR_mesh_quantization` |
| `no buffers, buffer views, accessors or meshes.` / `no primitive with KHR_gaussian_splatting.` / `no gaussians: POSITION missing or empty.` | un glTF sin gaussianas |
| `buffer <b> has no data.` / `buffer <b>: a data URI that does not decode.` / `buffer <b>: <path> cannot be read.` / `buffer <b> holds <n> bytes where <m> were due.` | un buffer ausente o corto |
| `<attribute> names accessor <i>, which is not there.` / `<attribute> names buffer view <v>, which is not there.` / `<attribute> runs past its buffer.` | un atributo que apunta fuera del fichero |
| `<attribute> is a sparse accessor, which is not read.` / `<attribute> is a <type> of component type <t>, which is not a gaussian's.` / `<attribute> is not aligned to its components, or lies past 4 GB.` | un accessor que este lector no admite |
| `<attribute> holds <n> elements where the others hold <m>.` | atributos con recuentos distintos |
| `POSITION has no min and max, which glTF requires.` | no hay bounds que tomar |

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
- **Transferencia** (una nube TX): el base colour es un albedo, y el splat
  dibuja la suma de athenea para una superficie opaca: el cuerpo,
  `albedo · (1 − metallic) · ⟨T_direct + T_indirect, L⟩` bajo las capas de
  coat y sheen, donde `L` es el sky light del nivel proyectado sobre los
  mismos 16 armónicos en el marco propio de la nube, en cada frame; el pulido
  del lóbulo base y del coat, cada uno el mip prefiltrado del sky light a lo
  largo de la dirección especular, estrechado por las direcciones abiertas y
  rellenado por el campo reflejado donde están cerradas; y el sheen. Se dibuja
  por la ruta sin iluminar, translúcido y ordenado. Todavía no se dibujan: el
  sol que athenea separa de un cielo, la pendiente de una reflexión a lo largo
  de un splat, el vidrio ni las paredes finas.
- **Iluminado** (el de upstream): el base colour como albedo, escrito en el
  GBuffer de UE donde la cobertura supera 0,3. No usa los armónicos.

### 6.1 El cielo que ilumina una nube TX

- El **sky light** del nivel, con un **cubemap especificado** (un HDRI
  importado como `TextureCube`). Todavía no se lee una captura en tiempo real;
  sin cubemap, una nube TX se dibuja negra.
- **Orientación.** athenea mapea un domo como lo hace UsdLux (la primera fila
  de la imagen es +y, su columna central +z). Un sky light con el mismo HDRI
  coincide con él con `SourceCubemapAngle` **180** (medido: los 16
  coeficientes proyectados a menos del 2,8 % de los de athenea).
- **Hemisferio inferior.** El domo de athenea es la esfera entera: desactivar
  `LowerHemisphereIsBlack` en el sky light.
- **Brillo.** El color y la intensidad del sky light escalan la proyección
  (`View.SkyLightColor`), igual que escalan la iluminación de cielo propia de
  UE.
- **Su sol.** Un sol compacto en el sky light (un cielo despejado, el softbox
  principal de un estudio) se encuentra y se saca de los armónicos, e ilumina
  una nube TX como una luz direccional sombreada por sus direcciones abiertas,
  como hace athenea. `r.Gaussians.Athenea.LogSky 1` registra en el log dónde
  se encontró, su cono y su irradiancia.
- **Reflexiones.** El pulido y el coat de una nube TX leen el cielo sin su sol
  -- el cubo del sky light con los texels del sol cortados, prefiltrado por
  rugosidad en un cubo de 256 texels por cara -- y toman el sol
  analíticamente a través de cada lóbulo, como hace athenea. El cubo se
  construye una vez por cubo y color del sky light y se conserva mientras
  estos no cambien; si cambia alguno, se reconstruye en el siguiente frame.
  `r.Gaussians.Athenea.Residual 0` lee en su lugar el cubo prefiltrado propio
  de UE, sol incluido.
- **Lóbulos nítidos, por píxel.** Donde la nube conserva su curvatura, un coat
  más suave que 0,2 y un pulido de base más suave que 0,5 (no uno al que
  responda el campo reflejado) se leen por píxel, como los dibuja athenea: el
  prepass saca del color la lectura del centro y entrega el espejo allí y su
  giro un píxel a la derecha y hacia abajo; el raster lee el cielo residual y
  el sol a lo largo del espejo al que gira cada píxel, de modo que una ventana
  o una lámpara reflejada en un coat curvo se dibuja donde cae dentro del
  splat. Solo con el residual activado. El frame reserva para ello 32 bytes
  por splat (`r.Gaussians.MaxSplats` de ellos) solo cuando dibuja una nube así.

### 6.2 Vistas de depuración

| Variable de consola | Valor | Efecto |
|---|---|---|
| `r.Gaussians.Athenea.Debug` | `0` | una nube TX dibuja su cuerpo |
| | `1` | la radiancia media del cielo proyectado (`SkySH[0] · C0`) |
| | `2` a `5` | el cielo proyectado reconstruido a lo largo de +y, −y, +z, −z del fichero |
| | `6` | si se hacen los lóbulos nítidos de un splat: en rojo el giro calculado, en verde la curvatura conservada, en azul el cielo residual |
| | `100` a `115` | el coeficiente `n − 100` del cielo proyectado, más 4 |
| `r.Gaussians.Athenea.Residual` | `1` (por defecto) | una nube TX refleja el cielo sin su sol y el sol analíticamente |
| | `0` | refleja el cubo prefiltrado propio del sky light, sol incluido |
| `r.Gaussians.Athenea.LogSky` | `1` | el cielo proyectado de la siguiente nube TX se lee de vuelta de la GPU y se registra una vez en el log (`LogTemp: athenea sky: 0:(r g b) ...`); vuelve a 0 por sí sola |

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

La captura HDR de UE (`capture_hdr`) no es luz de escena lineal: satura cerca
de 1. Para obtener valores que comparar, capturar con un `SceneCapture2D` en
`SCS_FINAL_COLOR_HDR` a un render target `RTF_RGBA32F` y exportarlo
(`capture_tx.py`); `SCS_SCENE_COLOR_HDR` se salta el post-proceso, y con él el
pase en el que se dibujan los splats sin iluminar. El post-proceso de UE además
oscurece los bordes del frame (`VignetteIntensity` 0,4): `capture_tx.py` lo
pone a 0 en el componente de captura. Con los ajustes de abajo y sin viñeteo,
la captura es la luz de la escena por una exposición fija, medida en 2,02 (la
radiancia media del cielo proyectado, leída de vuelta, frente a la misma
dibujada como un color plano).

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
  `GPU/GaussiansPrePass`, `Sort` y `Raster` son sus etapas, y
  `GPU/GaussiansSky` los pases de cielo de una nube TX (la proyección, y el
  cubo residual en los frames que lo construyen), dentro del prepass.
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

- El plugin es un fork de StarLightGS (MIT), y el trabajo propio de este fork
  está bajo la misma licencia MIT: `LICENSE` conserva la línea de copyright de
  upstream y añade la de este proyecto. Los ficheros añadidos aquí llevan esa
  línea en su cabecera.
- No se copia código fuente del motor en el repositorio; el plugin incluye
  las cabeceras de UE cuando se compila.
- A OpenUSD se llega a través del plugin USDCore de UE, bajo la propia
  licencia de OpenUSD (Tomorrow Open Source Technology License 1.0); el plugin
  no redistribuye nada de él.
- `Source/AtheneaCodecs` incluye dos decodificadores como código fuente, cada
  uno con su propia licencia al lado: el descompresor de zstd (v1.5.7, BSD,
  `Private/zstd/LICENSE`) y el decodificador de libwebp (v1.5.0, BSD,
  `Private/libwebp/COPYING` y `PATENTS`). gzip es el propio zlib de UE.
