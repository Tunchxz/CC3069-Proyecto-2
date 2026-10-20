# CC3069 - Computación Paralela y Distribuida

## Laboratorio 03: búsqueda de claves AES-128 con Open MPI

### Integrantes
- Dulce Ambrosio - 231143
- Daniel Chet - 231177
- Cristian Túnchez - 231359

### Descripción

El laboratorio analiza un programa secuencial que encuentra una clave AES-128 por fuerza bruta, corrige sus problemas conceptuales, y desarrolla una versión paralela con Open MPI. El rendimiento se evalúa con tiempos de ejecución y Speedup.

### Estructura del repositorio

```text
.
├── README.md
├── original/
│   └── busqueda_clave_aes_secuencial.c
├── mejorado/
│   ├── busqueda_clave_aes_secuencial_mejorado.c
│   └── busqueda_clave_aes_mpi.c
└── doc/
    └── Lab3 - Paralela.pdf
```

| Carpeta | Contenido |
|---|---|
| `original/` | Programa secuencial entregado en el enunciado, sin modificar. |
| `mejorado/` | Versión secuencial corregida y mejorada, y su versión paralela con Open MPI. |
| `doc/` | Informe del laboratorio (respuestas, capturas y análisis). |

### Programas

| Programa | Descripción |
|---|---|
| `original/busqueda_clave_aes_secuencial.c` | Búsqueda secuencial con AES-128-ECB. Clave y rango fijos en el código. |
| `mejorado/busqueda_clave_aes_secuencial_mejorado.c` | Secuencial mejorado: AES-128-CBC con IV aleatorio, verificación con un fragmento conocido del mensaje, prefijo de clave no nulo, borrado seguro de la clave, contexto de OpenSSL reutilizado, parámetros `-k` y `-b`, y código de salida distinto de cero si no encuentra la clave. |
| `mejorado/busqueda_clave_aes_mpi.c` | Versión paralela con Open MPI. Reparte el rango en bloques de 4096 candidatas entre los procesos de forma cíclica y coordina la finalización con `MPI_Allreduce(MPI_MIN)`. |

### Requisitos

- Entorno Linux o WSL (Ubuntu).
- `gcc`, Open MPI (`mpicc`, `mpirun`) y OpenSSL para desarrollo:

```bash
sudo apt update
sudo apt install libssl-dev
```

### Compilación y ejecución

Los comandos se ejecutan desde la raíz del repositorio.

**Original**

```bash
gcc -std=c11 -O2 -Wall -Wextra original/busqueda_clave_aes_secuencial.c -o busqueda_clave_aes_secuencial -lcrypto
./busqueda_clave_aes_secuencial
```

**Secuencial mejorado**

```bash
gcc -std=c11 -O2 -Wall -Wextra mejorado/busqueda_clave_aes_secuencial_mejorado.c -o busqueda_clave_aes_secuencial_mejorado -lcrypto
./busqueda_clave_aes_secuencial_mejorado
./busqueda_clave_aes_secuencial_mejorado -k 1048575
```

**Paralelo con Open MPI**

```bash
mpicc -std=c11 -O2 -Wall -Wextra mejorado/busqueda_clave_aes_mpi.c -o busqueda_clave_aes_mpi -lcrypto
mpirun -np 4 ./busqueda_clave_aes_mpi -k 1048575
```

### Opciones de los programas mejorados

| Opción | Descripción | Valor por defecto |
|---|---|---|
| `-k secreto` | Clave secreta (candidata) con la que se cifra el mensaje. | `12345` |
| `-b bits` | El rango explorado es de 2^bits candidatas (1 a 40). | `20` |
| `-c chunk` | Tamaño del bloque de reparto (solo versión MPI). | `4096` |

Si el secreto queda fuera del rango, el programa informa que no se encontró la clave. Esto sirve para comprobar que se recorre todo el rango sin omitir ni repetir candidatas.

### Resultados (peor caso: `-k 1048575`, promedio de 5 corridas)

| Procesos (n) | Tiempo medio (s) | Speedup | Eficiencia |
|---|---|---|---|
| 1 (secuencial mejorado) | 0.1100 | 1.00 | 100 % |
| 2 | 0.0587 | 1.87 | 94 % |
| 3 | 0.0390 | 2.82 | 94 % |
| 4 | 0.0303 | 3.63 | 91 % |

Speedup = T_sec / T_par(n), donde T_sec es el tiempo del secuencial mejorado. Las mediciones se hicieron en Ubuntu sobre WSL2. El detalle completo, las capturas y el análisis están en el informe de `doc/`.

> Con la clave original (12345) la búsqueda termina en milisegundos, por lo que no es representativa para medir Speedup. Por eso las mediciones usan el peor caso.
