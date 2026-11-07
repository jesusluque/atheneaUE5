# athenea para Unreal: diseño

Copyright (c) 2026 jesus luque.

Un fork de StarLightGS (CoSTAR National Lab, MIT, upstream 8720289) que
reproduce las nubes de gaussianas de athenea en Unreal Engine 5.7+. La licencia
de upstream sigue en `LICENSE`; los ficheros escritos aquí llevan la cabecera
de arriba.

## Lo que da upstream

- Un asset de arrays float32 por splat (posición, cuaternión, escala lineal,
  color DC + opacidad, normal y metallic/roughness opcionales), un structured
  buffer inmutable cada uno, 56 B por splat sin iluminar y 76 B iluminado.
- Un frame por vista:
  1. un culling de recorte, que solo se ejecuta cuando algo cambia;
  2. un prepass de compute (frustum, proyección EWA, HZB) que añade a un único
     buffer para todos los componentes visibles;
  3. un único radix sort global de 32 bits sobre la z de vista;
  4. un único draw indirecto de quads instanciados.
- **Splats sin iluminar** se mezclan "over" después de la translucidez de UE,
  antes de TSR y del tonemap.
- **Splats iluminados** se acumulan de delante a atrás en cuatro targets
  RGBA16F. Después, un pase de composición escribe el GBuffer de UE donde la
  cobertura supera 0,3, de modo que son superficies opacas con alpha test.
- **Sombras**: los splats se dibujan de nuevo mediante un vertex factory en
  virtual shadow maps, opacos por encima de una opacidad de 0,9, con el número
  completo para cada luz.

## Lo que athenea necesita de él

El producto de athenea es la reproducción raster de nubes de transferencia
(TX), translúcidas y ordenadas. Una nube TX no lleva su radiancia: lleva lo que
el frame necesita para iluminarla bajo cualquier cielo y cualquier luz
(docs/decisions.md, "A transfer right under any sky and any light"). El frame
recombina la transferencia directa e indirecta (grado 3) con los armónicos del
cielo, estrecha cada reflexión con los bits de direcciones abiertas
(`shadowBits`), muestra el campo reflejado donde están cerrados y evalúa los
lóbulos del material contra el cielo prefiltrado. Por orden:

1. **Color horneado, dependiente de la vista.** Hecho (7ed3cf4).
   - Los armónicos de grado 1 a 3 se importan y se evalúan en el prepass a lo
     largo de la vista, en el marco del fichero.
   - El comentario PLY de athenea da los ejes y el espacio de color; el color
     se linealiza después de la suma y se multiplica por `View.PreExposure`.
   - Coincide con el raster de athenea del mismo fichero desde las mismas
     cámaras, salvo por el tonemapper de UE.
   - Todavía en float, 180 B por splat; f16 llega con el lector USD.
2. **Reproducción TX.** `splat_relight` portado al prepass como una
   permutación junto a `ENABLE_RELIGHTING`, con el sky light de UE como cielo:
   su cubemap capturado proyectado a armónicos de grado 3 y prefiltrado por
   roughness en la GPU, una vez por captura.
3. **Importación de las nubes de athenea**, como un factory junto al de PLY.
   - El intercambio es la propia exportación de athenea: un ParticleField USD
     con `primvars:athenea:splat:*`. El importador USD de UE no conoce ni los
     esquemas ParticleField ni estos primvars, así que el factory del plugin
     lee el stage él mismo a través del OpenUSD que incluye UE, por nombre de
     atributo, sin depender de que esa versión traiga los esquemas
     ParticleField.
   - Lleva la escala en las unidades del stage; sin un x100 fijo en el código.
   - La decodificación a los buffers se hace en la GPU cuando es aritmética
     sobre datos. La CPU solo lee el fichero.
4. **Niveles de detalle y streaming:** el árbol de fusión de athenea
   (`lodTree`) y el corte por frame. Esto llega más tarde; upstream tiene un
   límite fijo de 5M por vista y 8,39M por componente.
5. **Modo iluminado a partir de materiales exactos:** el base colour, la
   normal, la roughness y el metal de mesh2splat, por la ruta de GBuffer de
   upstream. Las sombras de los shadow maps de nube translúcida de athenea
   sustituirían a los blobs VSM opacos.
6. **Composición con mallas de UE:** profundidad, y el shadow catcher de suelo
   de athenea como gaussianas.

## Defectos de upstream que corregir primero

- **Appends fuera de límites:** el append más allá de `MaxSplats` escribe
  fuera de límites (solo se comprueba `start >= capacity`). DX12 descarta esas
  escrituras; Metal y Vulkan no.
- **Comprobación de límite del vértice de sombra:** `Index > count` debería
  ser `>=`.
- **Sin exposición:** el color sin iluminar ignora `View.PreExposure`.
- **Desplazamiento del ViewRect:** las vistas con un desplazamiento de
  ViewRect distinto de cero (pantalla dividida, estéreo) rasterizan en el
  lugar equivocado.
- **GBuffer iluminado sin normalizar:** los valores no se dividen por el alpha
  acumulado.
- **Crash del importador:** escribe en un array de metallic/roughness vacío
  cuando existe pero no está marcado.
- **Buffers transitorios:** dimensionados a `MaxSplats` (unos 340 MB con 5M),
  no al número real.
- **Sort:** limitado a 64 workgroups, y siempre ordena los 32 bits.

## Dónde se ejecuta

Un portátil Windows con una RTX 3070 Laptop GPU (8 GB), UE 5.8.4 del launcher
de Epic, VS Build Tools 2022 17.14. El plugin se compila con `RunUAT BuildPlugin`, y los frames
se renderizan sin ventana con `-RenderOffscreen`. Se juzgan contra el path
tracer de athenea con el peón y el Corvette, frames completos.

## Preguntas abiertas

- Un binario plano escrito por athenea, solo si leer el stage resulta lento.
- Si los splats TX deberían ordenarse junto con la propia translucidez de UE,
  en lugar de después de ella.
