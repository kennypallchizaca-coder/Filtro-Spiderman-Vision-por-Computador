# Guía de defensa del código de escritorio

Esta guía explica la versión de escritorio: cámara o video, efecto cómic, controles, comparación Mat/UMat, mediciones y visualización VTK. Android, el informe académico completo y el vlog quedan pendientes fuera de este alcance.

## 1. Relación con el material de clase

La base está en `Vision-Computador/Codigos`, la carpeta compartida por el alumno. Estos ejemplos permiten justificar las operaciones sin atribuir al docente las adaptaciones del proyecto:

| Material local | Qué aparece realmente | Cómo se utiliza aquí |
| --- | --- | --- |
| Ejemplo 3: HSV y umbral de color, Unidad 1 | `cvtColor` a HSV/Lab, `inRange` y controles `HMin/SMin/VMin/HMax/SMax/VMax` con `createTrackbar` | Se reutilizan las conversiones y la selección por rango para la máscara de neón. La modificación de S/V mediante LUT y el panel VTK son adaptaciones del proyecto. |
| Ejemplo 5: CLAHE y convolución, Unidad 2 | `createCLAHE`, límite 40, cuadrícula 8×8 y mediana de tamaño 5 sobre gris | Se conserva CLAHE, aplicado aquí a L de Lab con límite inicial 2. La mediana pasa a 3×3 sobre la imagen en color. |
| Ejemplo 6: Gaussiano y Sobel, Unidad 2 | Gaussiano 5×5, sigma 1,73, Sobel `CV_16S`, `convertScaleAbs` y `addWeighted` con pesos 0,5/0,5 | Se conserva esa secuencia y sus valores iniciales; tamaño y sigma se vuelven ajustables. El kernel propio y la máscara de tinta son ampliaciones del proyecto. |
| Ejemplo 9: sal y pimienta, Unidad 2 | Generación de ruido, `medianBlur(...,5)`, controles, Gaussiano y Sobel | Refuerza el uso de mediana para ruido impulsivo. No se reutiliza `generarRuido` ni se añade ruido artificial al video del proyecto. |

El bloque más directamente reutilizado es el del Ejemplo 6. Allí se calculan `gX/gY`, se convierten a `gXPos/gYPos` y se combinan con `addWeighted`. En `Efecto::procesar`, los nombres son `gx/gy/gxpos/gypos`, se mantiene la combinación 0,5/0,5 y se usa el prefijo `cv::`. Después se mezcla ese resultado con la respuesta del kernel propio. Por tanto, se reutilizan las operaciones y la secuencia de clase, con parámetros y etapas añadidas para el efecto.

El kernel ajustable aplica la idea de filtrado por una vecindad estudiada en convolución y tiene forma de variante laplaciana. Sus pesos concretos pertenecen al proyecto; no se afirma que sean los pesos exactos del ejemplo ni una invención matemática nueva.

Los códigos de clase revisados no incluyen las siguientes partes de esta versión:

- La plantilla `Efecto<Imagen>`, la ejecución `UMat/OpenCL` y la trama de puntos mediante operaciones matriciales. Permiten compartir el efecto entre CPU y GPU; introducen construcciones más avanzadas que las de los ejemplos.
- La ventana VTK, sus planos texturizados y los botones del panel. En clase se usan ventanas HighGUI, `imshow`, `waitKey` y trackbars; aquí se mantienen matrices y callbacks, adaptados a VTK.
- Los contadores CPU/GPU/RAM de Windows y la comparación automatizada de tiempos con calentamiento y CSV. El Ejemplo 5 ya guarda un histograma con `ofstream`; aquí se reutiliza la idea de escribir datos, con un formato y mediciones nuevos.

Para defenderlo, atribuye los filtros base a los ejemplos del docente y explica esas ampliaciones como trabajo del proyecto con asistencia de Codex. La estética cómic, las LUT de color, los puntos, el neón y el desplazamiento RGB combinan esas operaciones para el objetivo visual elegido.

## 2. Archivos y construcciones de C++

[Principal.hpp](../Principal.hpp) contiene los parámetros y el efecto. [Interfaz.hpp](../Interfaz.hpp) reúne controles y eventos. [Principal.cpp](../Principal.cpp) coordina entrada, procesamiento, CSV y VTK. [Mediciones.hpp](../Mediciones.hpp) declara la medición de recursos y [Mediciones.cpp](../Mediciones.cpp) implementa las llamadas a Windows.

El programa empieza en `main`, dentro de `Principal.cpp`. `Principal.hpp` es una cabecera: se incorpora mediante `#include` y contiene las definiciones del efecto que necesitan tanto el programa como la interfaz. No es otro ejecutable. CMake compila `Principal.cpp` y `Mediciones.cpp` para producir la aplicación.

Las construcciones nuevas tienen una función concreta:

- `struct` agrupa datos relacionados, como `Parametros` o `Tiempos`; sus funciones son métodos. `private` reserva los detalles internos.
- `template<class Imagen>` permite compilar el mismo efecto con `Mat` y con `UMat`. Evita mantener dos algoritmos que puedan terminar usando parámetros diferentes.
- `vector<Imagen>(3)` guarda tres canales; `array<Rect,12>` guarda los doce botones de cada signo. El primero tiene tamaño dinámico y el segundo tamaño fijo.
- Una referencia `&` trabaja sobre el objeto existente. `const` impide modificarlo a través de esa referencia. El resultado devuelto por `procesar` sigue perteneciendo al efecto y se reutiliza en el siguiente fotograma.
- Asignar un `Mat` normalmente comparte sus datos mediante una cabecera; `clone()` crea una copia independiente cuando se necesita conservar una imagen.
- `auto` deduce el tipo; los bucles `for` recorren muestras o canales. Las lambdas son funciones pequeñas usadas al validar texto.
- `chrono::steady_clock` mide intervalos sin depender de cambios en la hora del sistema. `duration` convierte el intervalo a segundos o milisegundos.
- Un callback es una función que VTK llama al recibir un evento. Su argumento `void*` transporta la dirección de `EstadoInterfaz`, recuperada mediante `static_cast`.
- RAII libera recursos al destruir el objeto: archivos, imágenes y `unique_ptr` tienen dueño. `RecursosProceso` cierra la consulta PDH y prohíbe copiarse para evitar dos dueños del mismo identificador.
- `throw` comunica errores; `try/catch` los muestra y devuelve código 1. `#pragma once` evita incluir dos veces una cabecera; `inline` permite definir sus funciones en ella sin duplicar definiciones al enlazar.

`CV_8UC3` significa tres canales de enteros sin signo de ocho bits, en orden BGR; `CV_8UC1`, un canal. `CV_16S` conserva derivadas negativas y `CV_32F` conserva la respuesta flotante del kernel. `Mat_<float>` y las llaves construyen una matriz pequeña con valores explícitos.

## 3. Parámetros y controles

`Parametros::validar` rechaza valores fuera de rango antes de procesar. Las comparaciones escritas como `!(valor >= mínimo && valor <= máximo)` también rechazan NaN. `ajustarParametro` limita cada cambio con `clamp`.

| Parámetro | Inicial | Rango | Paso del panel |
| --- | --- | --- | --- |
| `umbral` | 40 | 0..255 | 5 |
| `tamGauss` | 5 | impares 3..51 | 2 |
| `sigma` | 1,73 | 0,1..10 | 0,25 |
| `pesoPropio` | 0,3 | 0..1 | 0,05 |
| `diagonal` | 1 | 0..3 | 0,25 |
| `radioPuntos` | 2 | 0..3 | 1 |
| `niveles` | 6 | 2..16 | 1 |
| `saturacion` | 1,25 | 0..3 | 0,05 |
| `brillo` | 1,05 | 0..3 | 0,05 |
| `neon` | 1,15 | 0..3 | 0,05 |
| `desplazamiento` | 1 | 0..3 píxeles | 1 |
| `contraste` | 2 | 0,1..8 | 0,25 |

`--kernel` modifica el tamaño del Gaussiano. El kernel propio permanece de 3×3; `--diagonal` modifica sus pesos. La línea de comandos también permite establecer sigma y peso propio; los otros parámetros visuales se ajustan en la ventana.

## 4. Recorrido completo de un fotograma

`Efecto<Imagen>::procesar` valida una entrada BGR no vacía y ejecuta las operaciones siguientes en orden. `validarColor` concentra esa comprobación. Se mantienen nombres reconocibles del código inicial: `comic`, `mediana`, `gris`, `imgGauss`, `gx`, `gy`, `binaria`, `hsv`, `moverazul`, `moverRojo` y `canalesGlich`.

### Contraste y suavizado

La conversión BGR→Lab separa luminosidad y componentes de color. `split` obtiene `canalesLab`; CLAHE modifica solamente `canalesLab[0]`. Después, `merge` y Lab→BGR recuperan el color en `comic`.

CLAHE calcula histogramas locales, recorta sus acumulaciones excesivas, redistribuye ese exceso y utiliza distribuciones acumuladas para transformar intensidades. Interpola entre regiones para reducir discontinuidades. `Size(8,8)` es una cuadrícula de ocho regiones por eje, no ventanas de ocho píxeles. `contraste` controla el límite de recorte.

`medianBlur(comic, mediana, 3)` toma la mediana de cada vecindad 3×3, por canal. Es un filtro no lineal útil frente a valores aislados. `cvtColor` produce `gris`. `GaussianBlur` crea `imgGauss` mediante un promedio ponderado:

```text
G(u,v) = C · exp(-(u²+v²)/(2·sigma²)), con suma(G)=1
imgGauss(x,y) = suma de G(u,v)·gris(x+u,y+v)
```

El tamaño limita la vecindad y sigma determina la dispersión de sus pesos. La mediana y el Gaussiano cumplen funciones diferentes; el segundo reduce variaciones que podrían convertirse en contornos espurios.

### Kernel propio y contornos

`crearKernel(a)` genera:

```text
           [ -a    -1       0 ]
K(a) =     [ -1   4+2a     -1 ] / (4+2a)
           [  0    -1      -a ]
```

La suma es cero: una región constante ideal produce respuesta cero. Los cambios de intensidad producen respuesta. `a=1` recupera la matriz original dividida por seis; aumentar `a` cambia la contribución de dos vecinos diagonales. La división mantiene una escala controlada al variar esos pesos, pero no convierte la matriz en un promedio.

`filter2D` realiza correlación. La convolución invierte el kernel 180 grados; esta matriz es simétrica bajo esa inversión y ambas operaciones coinciden aquí. `filtroPropio` es flotante; `convertScaleAbs` produce `filtroPropioAbs` tomando valor absoluto, redondeando y saturando a 0..255.

Sobel calcula `gx` y `gy` con derivadas 3×3. `gxpos` y `gypos` contienen sus valores absolutos saturados. El código combina:

```text
D = sat8(0,5·abs8(gx) + 0,5·abs8(gy))
M = sat8((1-pesoPropio)·D + pesoPropio·filtroPropioAbs)
binaria(x,y) = 255 si M(x,y) > umbral; 0 en caso contrario
```

Esta aproximación usa sumas ponderadas, no `sqrt(gx²+gy²)`. Cada conversión de ocho bits puede introducir redondeo o saturación. La máscara blanca identifica dónde se pintará negro mediante `comic.setTo(0, binaria)`.

En estas fórmulas, `sat8` significa redondear y limitar a 0..255; `abs8` significa tomar valor absoluto y convertir de esa misma forma a ocho bits. Son abreviaturas explicativas, no funciones adicionales del proyecto.

### Cuantización y trama de puntos

La LUT `tabla` tiene 256 entradas:

```text
Q(i) = sat8(round(i·(niveles-1)/255) · 255/(niveles-1))
```

Con seis niveles genera 0, 51, 102, 153, 204 y 255 por canal. `LUT(mediana, tabla, comic)` reduce la gradación antes de añadir la tinta y el neón.

`aplicarTrama` conserva el dibujo de círculos del original y evita recorrer cada píxel desde C++. Divide la imagen en celdas de ocho píxeles, muestreadas en `(8c+4, 8f+4)`. Para brillo `I` y radio máximo `R`:

```text
r = round((255-I)·R/255)
```

Una celda oscura produce un círculo mayor. Una baldosa 8×8 almacena el menor radio que incluye cada píxel, utilizando `circle` para conservar su rasterización exacta. `repeat` extiende esa baldosa; `warpAffine` con `muestreo` y `WARP_INVERSE_MAP` recoge los centros; `resize` con vecino más próximo replica cada brillo en su celda. `convertTo` calcula radios y `compare(CMP_GE)` crea `puntos`, la máscara negra.

Se usan divisiones redondeadas hacia arriba para cubrir celdas parciales. Si su centro queda fuera, el muestreo recibe blanco 255 y genera radio cero: coincide con el bucle original, que no dibujaba ese centro. Los círculos válidos se recortan al borde de la imagen. Radio cero desactiva la trama.

### Neón y separación de canales

`aplicarNeon` convierte a HSV. En ocho bits, H recorre 0..179 y S/V 0..255. La máscara incluye H entre 90 y 170 y S entre 51 y 255, calculados antes de modificar los canales.

```text
V1 = sat8(neon·V) dentro de la máscara; fuera, V1=V
S1 = sat8(saturacion·S)
V2 = sat8(brillo·V1)
```

`actualizarLUT` prepara cada multiplicación para los 256 valores posibles. Se mantienen dos pasos para V: saturar tras neón y después tras brillo conserva el comportamiento original; multiplicar los factores directamente puede dar otro resultado. `valorNeon.copyTo` aplica el primer cambio sólo bajo `mascaraNeon`. `merge` y HSV→BGR terminan la operación.

`aplicarGlitch` desplaza azul a la derecha y rojo a la izquierda; verde permanece igual. Para desplazamiento entero `d`:

```text
Bsalida(x)=Bentrada(max(x-d,0))
Rsalida(x)=Rentrada(min(x+d,ancho-1))
```

Se añaden márgenes replicados a azul y rojo, se transforma con vecino más próximo y borde constante, y se recorta la zona central. Así el resultado conserva el borde replicado original usando una ruta compatible con `warpAffine` OpenCL. Desplazamiento cero omite el efecto.

## 5. Mat, UMat y memoria reutilizada

`Efecto<Mat>` y `Efecto<UMat>` comparten las mismas instrucciones y parámetros. Mat utiliza la ruta CPU; UMat permite a la Transparent API elegir implementaciones OpenCL. Esto no demuestra que cada operación se ejecute en GPU: existen condiciones de soporte y retornos a CPU.

Los buffers públicos conservan etapas y canales: `lab/canalesLab`, `comic/mediana/gris/imgGauss`, derivadas `gx/gy/gxpos/gypos`, `gradiente/binaria`, `hsv/canales`, `filtroPropio/filtroPropioAbs`, `canalesGlich` y `tabla`. Las matrices pequeñas `kernelPropio`, `moverazul` y `moverRojo` siguen siendo Mat.

Los privados tienen estos usos:

- `clahe`: conserva el objeto de contraste; `contrasteAnterior` evita configurarlo de nuevo sin cambios.
- `radioMinimo`, `brilloCeldas`, `brilloExpandido`, `radioCelda`, `puntos`: baldosa, centros, expansión, radios y máscara. `tamanoTrama/radioAnterior` invalidan la baldosa al cambiar tamaño o radio; `muestreo` fija la separación de ocho píxeles.
- `tablaSaturacion/tablaBrillo/tablaNeon`, `mascaraNeon/valorNeon`: transformaciones de HSV. Los factores anteriores invalidan sus LUT.
- `azulExtendido/rojoExtendido/azulMovido/rojoMovido`: márgenes y desplazamientos. `desplazamientoAnterior` invalida las matrices de traslación.
- `nivelesAnteriores` y `diagonalAnterior`: invalidan la LUT de cuantización y el kernel.

Los valores anteriores empiezan en -1 para obligar a preparar cada caché en su primer uso. Los resultados intermedios se recalculan por fotograma, mientras OpenCV puede reutilizar sus asignaciones de memoria. No se guarda una imagen final antigua como sustituto de procesar la nueva.

## 6. Funciones de Principal.cpp e interfaz

`milisegundos` convierte intervalos del reloj. `prepararOpenCL` comprueba el runtime, crea y enlaza un contexto para la selección solicitada, verifica que sea GPU e imprime nombre, fabricante y controlador. La selección predeterminada es `NVIDIA:GPU:RTX 4070`. Un modo exclusivamente GPU falla si no puede seleccionarla; `both` puede continuar como CPU si falta. Al arrancar con `--mode=cpu` no se intenta seleccionar GPU; para alternar los tres modos con Espacio, se debe iniciar con `--mode=both` y disponer de OpenCL.

`Tiempos` distingue CPU, subida, filtrado UMat y bajada; -1 significa ruta no ejecutada. `procesoGPU()` suma las tres fases. `procesarFrame` desactiva OpenCL para Mat y lo activa para UMat. Usa `ocl::finish()` antes de medir y al completar cada fase para esperar al trabajo pendiente. Incluye transferencia de entrada y recuperación de salida, aunque esos intervalos no equivalen a un perfilador de transferencias físicas de la tarjeta.

`escogerEtapa` devuelve efecto final, gris, Gaussiano, respuesta del kernel o máscara. Aunque se vea una etapa temprana, se procesa el efecto completo. Modo 0 ejecuta CPU, modo 1 UMat y modo 2 ambos secuencialmente sobre el mismo fotograma; este último muestra el resultado UMat y compara tiempos.

`esCamara` reconoce índices formados sólo por dígitos. `abrirFuente` intenta DirectShow y después el backend automático, o abre un archivo. Solicita tamaño a la cámara. `leerFrame` comprueba lectura, redimensiona al tamaño acordado y, cuando se permite, rebobina videos agotados. La visualización termina al acabar un archivo; perder la cámara produce un error.

`crearCarpetaPadre` prepara la carpeta de salida. `abrirCSV` sobrescribe benchmarks y añade registros en vivo, escribiendo cabecera cuando corresponde. `campoCSV` entrecomilla texto y duplica sus comillas internas. `escribirFila` guarda parámetros, etapa, tiempos, recursos y dispositivo; deja CPU/GPU vacíos si no hay muestra válida y comprueba errores de escritura. Al pulsar B, el registro se guarda en `resultados/mediciones_en_vivo.csv`; en comparación se escribe una fila CPU y otra OpenCL por fotograma. `capturarClip` guarda fotogramas originales en MJPG con metadato de 30 FPS, sin medir con ello la cadencia física de la cámara.

En [Interfaz.hpp](../Interfaz.hpp), `numero` formatea decimales. `EstadoInterfaz` conserva parámetros, modo, etapa, selección, disponibilidad y órdenes de salir/registrar/restaurar. `dibujarControles` dibuja doce filas en dos columnas y guarda los rectángulos de cada botón. `eventoTeclado` sólo atiende pulsaciones `KEY_DOWN`; arriba/abajo seleccionan y derecha/izquierda ajustan. Espacio recorre CPU→UMat→comparación, Tab recorre cinco etapas, B alterna CSV, R restaura la vista y Esc/q salen.

`eventoRaton` reconoce clic izquierdo. VTK entrega Y desde abajo; la imagen del panel usa Y desde arriba. Por eso convierte `yImagen=altoVentana-1-yVTK` y resta la esquina del panel antes de probar `Rect::contains`. Esto mantiene los botones alineados al redimensionar.

`visualizar` crea una única `Viz3d` con dos `WImage3D`: original a la izquierda y efecto a la derecha. Son imágenes como texturas en planos 3D, no reconstrucción de profundidad. La pose combina orientación y traslación de la cámara virtual; las normales y vectores verticales orientan los planos. Arrastrar permite inspeccionar la escena; textos y panel son superposiciones de pantalla.

Cada vuelta captura, toma una copia de parámetros/etapa, procesa, actualiza texturas, recursos y controles, y llama `spinOnce` para renderizar y atender eventos. El CSV utiliza esa copia anterior a los eventos para etiquetar correctamente el fotograma ya medido. `--smoke` cierra tras el límite pedido y guarda una captura; terminar antes es un error.

`main` interpreta argumentos, valida cantidad 1..10000 y tamaño 8..3840 por 8..2160, abre la fuente y elige captura, benchmark o visualización. El benchmark exige video fijo, un solo modo por proceso y rebobinado válido. Rechaza una salida que identifique el mismo archivo de entrada, y la captura rechaza videos que ya existen para conservar las pruebas anteriores. `--help` explica opciones; las excepciones terminan con un mensaje y código de error.

## 7. Recursos y tiempos: qué significan

`ramActualMiB` consulta `GetProcessMemoryInfo` con el proceso actual y convierte `WorkingSetSize` mediante `bytes/1024²`. Es RAM residente del proceso, con bibliotecas y buffers; no es VRAM ni memoria exclusiva de una operación. Se actualiza en cada `leer()`.

`tiempoCpu` consulta `GetProcessTimes`, une las dos mitades de cada `FILETIME` y suma tiempo de usuario y kernel de todos los hilos. `RecursosProceso::Estado` conserva la lectura anterior, reloj, cantidad de procesadores lógicos, consulta PDH y muestra. El porcentaje se calcula así:

```text
CPU % = 100·delta(tiempoCPU)/(10^7·delta(segundos)·procesadores)
```

El factor `10^7` convierte unidades de 100 ns a segundos. Se limita a 0..100 y `cpuDisponible` requiere un par válido tras al menos un segundo. CPU y GPU se conservan entre consultas de un segundo; por ello filas consecutivas pueden repetir la misma muestra de recursos.

El constructor abre PDH, añade en inglés `GPU Engine(*)/Utilization Percentage` y toma la lectura inicial. `leer` recoge otra, pregunta el tamaño necesario, reserva el buffer y obtiene el arreglo. Filtra nombres que empiezan por `pid_<PID>_`, valida estado y número finito, y conserva el motor máximo. El destructor cierra la consulta y sus contadores. Un contador ausente no detiene el filtro.

La etiqueta es «GPU proceso, motor máximo»: incluye renderizado y cómputo y puede abarcar motores distintos. Una muestra válida de 0 se diferencia de «sin muestra». Un cero durante un benchmark breve tampoco demuestra que ningún filtro utilizó GPU. El contexto NVIDIA verificado y las pruebas aportan otra evidencia, pero atribuir cada operación exigiría perfilado específico.

`benchmark` calienta hasta cumplir ambas condiciones: al menos 15 fotogramas y 1,1 segundos. Después rebobina y registra las mismas entradas. `ms_filtro` es el recorrido del efecto; GPU añade `ms_subida/ms_bajada` para formar `ms_proceso`. `ms_ciclo` añade lectura/redimensionado y, en vivo, interfaz/renderizado hasta antes de escribir CSV. La RAM se compara en procesos separados para evitar sumar las dos rutas en una misma ejecución.

En el modo de comparación, CPU y UMat se ejecutan una después de otra: sus filas tienen tiempos de filtrado distintos, pero comparten `ms_ciclo` y la muestra de recursos del proceso. Para comparar RAM y rendimiento de cada modo por separado, usa los comandos de benchmark de la sección siguiente.

`fps_proceso=1000/ms_proceso` expresa capacidad de procesamiento. Los FPS de ventana incluyen el ciclo interactivo y se suavizan con `0,9·anterior+0,1·actual`; la cámara y el renderizado pueden limitarla. El calentamiento excluye inicialización de las filas medidas, sin eliminar variaciones de temperatura, carga o cachés.

## 8. Compilar, comprobar y repetir resultados

[CMakeLists.txt](../CMakeLists.txt) exige C++17, localiza los módulos OpenCV y enlaza `psapi/pdh` para las mediciones Windows. Produce un único ejecutable: `interciclo_vision.exe`. Se compila directamente con CMake y se ejecuta el `.exe`, desde la terminal con MSYS2 UCRT64 y OpenCV configurados.

Durante el desarrollo se comprobaron el kernel, los 256 niveles de brillo de la trama, límites HSV, glitch, validación de entradas, LUT y recorrido completo. También se revisaron clics y conversión Y, límites de los controles, teclado, modos, etapas y medición de recursos.

La comparación registrada sobre RTX 4070 Laptop GPU obtuvo diferencia absoluta media 1,86342 a 640×480 y máximo 255. Diferencias pequeñas de cálculo pueden cruzar el umbral duro y cambiar un contorno entero; por eso se admitió una diferencia media menor a cinco niveles. Esto no demuestra igualdad visual perfecta.

Para configurar una copia recién clonada, sigue primero los requisitos y el comando de configuración del [README](../README.md). Después, desde PowerShell en la raíz:

```powershell
cmake --build build --parallel 2
.\build\interciclo_vision.exe
# Cerrar la ventana antes de recompilar o medir.
.\build\interciclo_vision.exe --capture=resultados/entrada.avi --frames=120 --mode=cpu
.\build\interciclo_vision.exe --benchmark --source=resultados/entrada.avi --mode=cpu --frames=120 --kernel=5 --output=resultados/cpu_k5_nuevo.csv
.\build\interciclo_vision.exe --benchmark --source=resultados/entrada.avi --mode=gpu --frames=120 --kernel=5 --output=resultados/gpu_k5_nuevo.csv
```

Los videos de cámara no se incluyen en Git. Si el clip anterior ya existe en tu PC, conserva ese archivo o elige un nombre nuevo y usa esa misma ruta al medir:

```powershell
.\build\interciclo_vision.exe --capture=resultados/nueva_entrada.avi --frames=120 --mode=cpu
.\build\interciclo_vision.exe --benchmark --source=resultados/nueva_entrada.avi --mode=cpu --frames=120 --output=resultados/cpu_nuevo.csv
.\build\interciclo_vision.exe --benchmark --source=resultados/nueva_entrada.avi --mode=gpu --frames=120 --output=resultados/gpu_nuevo.csv
```

Para comparar otra carga, repite los comandos con `--kernel=31` y nombres de salida nuevos. El ejecutable muestra media y mediana del filtrado, media del proceso, FPS de proceso y RAM máxima; guarda las muestras completas en CSV con punto decimal. No calcula automáticamente desviación, percentil 95 ni un archivo `resumen.csv`.

La carpeta `resultados` no es necesaria para compilar: el programa la crea al grabar un clip o escribir mediciones. Sus CSV son datos generados; `Mediciones.hpp` y `Mediciones.cpp` son código necesario para compilar. Conserva los CSV que utilices como evidencia en el informe.

Referencia histórica del desarrollo, 120 muestras por caso, OpenCV 5.0.0 Release, AMD Ryzen 9 7845HX y NVIDIA GeForce RTX 4070 Laptop GPU con controlador 617.42. Estas cifras están anotadas aquí; antes de defenderlas, conserva los CSV de tu propia ejecución:

| Modo | Gauss | Filtro medio ms | Proceso medio ms | FPS proceso | RAM máxima MiB |
| --- | --- | --- | --- | --- | --- |
| CPU | 5 | 3,80055 | 3,80055 | 263,12 | 90,46 |
| UMat/OpenCL | 5 | 3,60775 | 4,02893 | 248,20 | 199,97 |
| CPU | 31 | 4,52691 | 4,52691 | 220,90 | 91,02 |
| UMat/OpenCL | 31 | 3,32740 | 3,67336 | 272,23 | 232,39 |

En esa ejecución, el costo completo GPU con Gauss 5 fue mayor aunque su filtro fue algo menor. Con Gauss 31 mejoró el tiempo completo. Son resultados de ese equipo y esa entrada; no garantizan que GPU siempre gane. Se registró motor GPU válido 0 en esas ejecuciones breves; no permite atribuir la carga de cada filtro ni sustituye los tiempos sincronizados.

Otra ejecución de referencia registró 600 muestras a 1280×720 con Gauss 31, sin ventana VTK. Obtuvo aproximadamente 8,71 ms de filtro, 9,77 ms de proceso, 234,34 MiB de RAM máxima y motor GPU medio 59,304 %, con máximo 100 %. La media incluye muestras iniciales de cero. Se observó actividad GPU durante el procesamiento sin renderizado VTK; sigue siendo una medición del proceso, no un perfil de cada operación. Puedes repetir ese procedimiento con tu clip, conservando los nuevos resultados:

```powershell
.\build\interciclo_vision.exe --benchmark --source=resultados/entrada.avi --mode=gpu --frames=600 --width=1280 --height=720 --kernel=31 --output=resultados/gpu_carga.csv
```

## 9. Guion oral de tres a cinco minutos

1. **Primeros 40 segundos:** mostrar original/efecto y explicar la relación con ejemplos 3, 5, 6 y 9; señalar los nombres de matrices conservados.
2. **Hasta el minuto 2:** recorrer CLAHE, mediana, Gaussiano, Sobel, kernel de suma cero, cuantización, puntos, HSV y glitch. Mostrar las cinco etapas y variar umbral/peso diagonal.
3. **Minuto 2 a 3:** explicar que la plantilla comparte algoritmo Mat/UMat y que `finish` permite medir trabajo terminado. Comparar filtro frente a recorrido completo.
4. **Minuto 3 a 4:** mostrar los CSV de tus mediciones con Gauss 5/31 y explicar los límites de RAM, motor GPU y FPS. Explicar un caso donde las transferencias cambian la conclusión.
5. **Cierre hasta el minuto 5:** mostrar un botón, rotación/restauración VTK y registro en vivo. Delimitar el alcance de escritorio.

## 10. Veinte preguntas probables

1. **¿Qué parte viene de clase?** CLAHE, mediana, Gaussiano, Sobel, HSV y la idea de controles; las adaptaciones están identificadas en la tabla inicial.
2. **¿Inventaste el Laplaciano?** No. Ajusté una variante de suma cero y conservé la matriz original cuando `a=1`.
3. **¿Por qué la suma del kernel es cero?** Para cancelar intensidad constante y responder a cambios espaciales.
4. **¿Convolución o correlación?** `filter2D` correlaciona; aquí coinciden por la simetría de 180 grados del kernel.
5. **¿Por qué mediana y Gaussiano?** La mediana elimina valores aislados; el Gaussiano suaviza antes de derivar.
6. **¿Qué modifica sigma?** La dispersión de los pesos Gaussianos; el tamaño determina cuántos vecinos se consideran.
7. **¿Calculas magnitud euclídea de Sobel?** Uso la combinación ponderada de derivadas absolutas saturadas mostrada en el código.
8. **¿Por qué `CV_16S` y `CV_32F`?** Conservan signos y precisión antes de convertir las respuestas a ocho bits.
9. **¿Seis niveles significan seis colores?** Son seis intensidades por canal antes de las transformaciones posteriores.
10. **¿Cómo decides el radio de un punto?** Muestreo el centro de su celda y redondeo `(255-I)·R/255`.
11. **¿Por qué vectorizar la trama?** Para expresar el trabajo con operaciones OpenCV que aceptan Mat y UMat, conservando círculos y bordes comprobados.
12. **¿Por qué dos LUT sobre V?** La primera puede recortar valores; conservar ambas mantiene redondeo y saturación secuenciales.
13. **¿Qué pasa en el borde del glitch?** Los márgenes replicados proporcionan el color extremo y se recortan después de trasladar.
14. **¿UMat garantiza GPU en todas las operaciones?** No; permite T-API y algunas operaciones pueden volver a CPU.
15. **¿Cómo seleccionas la tarjeta?** Creo un contexto explícito y compruebo tipo GPU, nombre y controlador.
16. **¿Por qué esperar con `finish`?** Para medir la finalización y no solamente el envío de trabajo a una cola.
17. **¿Los 272 FPS son de la cámara?** Son capacidad calculada del procesamiento; la ventana mide otro ciclo.
18. **¿La RAM mostrada es VRAM?** Es Working Set del proceso Windows, que incluye bibliotecas y buffers residentes.
19. **¿Por qué puede diferir un píxel en 255?** Una diferencia pequeña cerca del umbral puede activar o quitar tinta negra; se informa también el error medio.
20. **¿Qué representa la escena 3D?** Dos planos texturizados que permiten inspección interactiva, sin reconstruir geometría del objeto filmado.

## 11. Comprobación práctica antes de defender

- Compilar con la aplicación cerrada y comprobar que la terminal identifique la NVIDIA al activar OpenCL.
- Abrir cámara y verificar original vertical, a la izquierda, junto al efecto; disponer del video fijo como alternativa.
- Probar un botón de cada columna, flechas, Espacio, Tab y R; arrastrar la escena y restaurarla.
- Cambiar umbral, peso propio, diagonal y tamaño Gaussiano explicando qué operación se modifica.
- Pulsar B, variar un parámetro, detener B y revisar que el CSV conserve parámetros y etapa de cada fotograma.
- Tener abiertos código, ejemplos de clase y tus CSV de mediciones. Interpretar celdas vacías como falta de muestra y distinguir filtro, proceso y ventana.

La defensa puede sostener lo que ejecuta y registra esta versión de escritorio. La ejecución Android, el informe completo y el vlog requieren trabajo y evidencia propios.
