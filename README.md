# Proyecto 02 - Búsqueda de claves AES-128 con Open MPI

Avance para CC3069: cifrado y descifrado de archivos, búsqueda secuencial y MPI,
y propuesta de distribución alternativa. Se adapta el código mejorado del
Laboratorio 03 de Dulce Ambrosio, Daniel Chet y Cristian Túnchez.

## Preparación en Linux o WSL

Se requiere un compilador C11, Make, Open MPI, OpenSSL de desarrollo y Python 3
para las pruebas y los registros. En Debian/Ubuntu:

```bash
sudo apt update
sudo apt install build-essential openmpi-bin libopenmpi-dev libssl-dev python3
make build
make test
```

Ejecutar desde la raíz del proyecto dentro de WSL/Linux. Los binarios de `build/`
son ejecutables Linux. En WSL, copiar el proyecto al sistema de archivos de Linux (por ejemplo `~/`)
antes de probar o medir: con el binario en `/mnt/c`, `mpirun` se queda colgado
de vez en cuando al terminar con código distinto de 0. Las pruebas usan la biblioteca estándar `unittest` de
Python y verifican archivos, compatibilidad con OpenSSL, cobertura del reparto,
claves límite, parada colectiva y límite de tiempo.

## Demostración para la revisión

```bash
make demo
```

Compila y muestra original, cifrado hexadecimal, descifrado, comparación de
archivos y búsqueda con cuatro procesos. Guarda comandos y salidas reales en
`docs/measurements/demo.txt`. Tomar las capturas de esa terminal para la entrega.

## Inciso 1: archivos y claves

```bash
./build/aes_tool encrypt -i examples/mensaje.txt -o build/mensaje.aes -k 123456
od -An -tx1 build/mensaje.aes
./build/aes_tool decrypt -i build/mensaje.aes -o build/recuperado.txt -k 123456
cmp examples/mensaje.txt build/recuperado.txt
```

`cmp` debe terminar sin diferencias. Se admiten archivos vacíos, texto UTF-8 y
longitudes que no sean múltiplos de 16. El ejemplo contiene exactamente
`Esta es una prueba de proyecto 2`, sin salto de línea final.

Se conserva la conversión del Lab 03: clave AES de 16 bytes = prefijo hexadecimal
`5556472d43433330` + entero sin signo de 64 bits en orden big-endian.
Para `123456` resulta `5556472d43433330000000000001e240`.
El rango de búsqueda de 56 bits es un subconjunto educativo de AES-128;
no se está recorriendo todo el espacio de 128 bits.

El formato binario es IV aleatorio de 16 bytes seguido del cifrado AES-128-CBC
con relleno PKCS#7. El IV se guarda públicamente; la clave no se guarda en el
archivo. El formato es propio de este ejercicio y no incluye autenticación:
un relleno válido por sí solo no prueba que la clave sea correcta. La búsqueda
también exige la frase conocida. Se cargan los archivos en memoria, con un límite
ligeramente inferior a 2 GiB por las interfaces de EVP/MPI; las pruebas del
proyecto usan textos pequeños.

## Inciso 2: búsqueda sin proporcionar la clave

```bash
./build/search_seq -i build/mensaje.aes -b 56
mpirun -np 4 ./build/search_mpi -i build/mensaje.aes -b 56 -s cyclic
```

La búsqueda recibe solamente el archivo cifrado y la frase, que por defecto es
`es una prueba de`. Se descifra cada candidato y se busca esa frase en cualquier
posición del texto; la búsqueda es sensible a mayúsculas y a los bytes del texto.
`-p "otra frase"` permite cambiarla. Una frase demasiado corta aumenta la
posibilidad de aceptar una clave incorrecta.

| Opción de búsqueda | Valor predeterminado | Significado |
|---|---|---|
| `-b` | `20` | Rango `[0, 2^b)`, entre 1 y 56 bits. Usar 56 en las tres pruebas requeridas. |
| `-s` | `cyclic` | Solo MPI: estrategia del Lab 03 o `hybrid` (inciso 3). |
| `-t` | `1` | Solo MPI: hilos OpenMP por proceso, entre 1 y 256. |
| `-c` | `4096` | Solo MPI: candidatas por proceso antes de sincronizar. |
| `--seconds` | `0` | Límite de búsqueda; 0 significa sin límite. Se verifica al final de cada bloque/ronda. |

En un equipo con menos de cuatro ranuras disponibles se puede agregar
`--oversubscribe` a `mpirun`; registrar esa decisión al medir rendimiento.
Si la búsqueda alcanza el límite, informa `limite_tiempo`, no una búsqueda completa.
Los códigos de salida son 0 (éxito), 1 (rango agotado), 2 (error) y 3 (límite de tiempo).

Para preparar una de las claves grandes:

```bash
./build/aes_tool encrypt -i examples/mensaje.txt -o build/grande.aes -k 18014398509481984
mpirun -np 4 ./build/search_mpi -i build/grande.aes -b 56 -s cyclic --seconds 5
```

El segundo comando suele terminar con código 3. Repetir el cifrado con
`18014398509481985` para el otro caso. Omitir `--seconds` inicia la búsqueda
completa, que puede ser impracticable. La clave no se utiliza para escoger el
inicio ni modificar el rango de la búsqueda.

## Propuesta del inciso 3 y mediciones

La propuesta está en [docs/propuesta-inciso3.md](docs/propuesta-inciso3.md).
Para entenderla desde cero, ver [la explicación y comparación con el Lab 3](docs/explicacion-propuesta.md).
La estrategia híbrida reparte tramos contiguos entre procesos MPI y cada bloque
entre hilos OpenMP:

```bash
mpirun -np 4 ./build/search_mpi -i build/grande.aes -b 56 -s hybrid -t 1
mpirun -np 2 ./build/search_mpi -i build/grande.aes -b 56 -s hybrid -t 2 --seconds 5
mpirun -np 1 ./build/search_mpi -i build/grande.aes -b 56 -s hybrid -t 4 --seconds 5
```

```bash
make benchmark
```

Hace tres repeticiones de las búsquedas completables con la secuencial, la
cíclica 4x1 y la híbrida 4x1, 2x2 y 1x4. Limita a tres segundos las búsquedas
en las que la clave queda lejos del inicio de un tramo.
Guarda salidas, entorno, datos y promedios en `docs/measurements/`.
Las estimaciones se identifican explícitamente; no sustituyen mediciones completas.
Se compara el tiempo de búsqueda, excluyendo la preparación y el arranque de MPI.

## Organización

- `src/`: funciones compartidas, herramienta AES y búsquedas secuencial/MPI.
- `tests/`: cobertura de particiones y pruebas de integración.
- `scripts/`: demostración, mediciones y generación del PDF.
- `examples/`: texto solicitado en el enunciado.
- `docs/`: propuesta, resultados y registros de evidencia.
- `CC3069-Computacion-Paralela-y-Distribuida-Lab3/`: referencia original del equipo.

Para regenerar la propuesta PDF, instalar ReportLab en el entorno Python activo
y ejecutar `make proposal` (o `py scripts/build_proposal.py` en Windows).
La generación del PDF es opcional para compilar y probar los programas C.
