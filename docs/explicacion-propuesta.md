# La propuesta explicada desde cero

## ¿Qué problema resolvemos?

Tenemos un mensaje cifrado y queremos descubrir su clave probando posibilidades
una por una: eso es **fuerza bruta**. En este proyecto, cada vez que probamos una
clave, desciframos y buscamos `es una prueba de` como señal de que acertamos.

Usamos cuatro **procesos**: cuatro ejecuciones del programa que colaboran al
mismo tiempo. Pueden ejecutarse en una misma computadora.

**La propuesta cambia dónde empieza a buscar cada proceso y qué claves le tocan.**

## ¿Cómo trabajaba nuestro Lab 3?

La versión secuencial tenía un trabajador. La versión MPI repartía paquetes
de 4096 claves por turnos entre los trabajadores. Ese reparto se llama **cíclico**.

Imaginemos claves del **0 al 39**, cuatro procesos y paquetes de **2 claves**.
Usamos 2 para explicarlo; el programa real utiliza 4096.

| Momento | Proceso 0 | Proceso 1 | Proceso 2 | Proceso 3 |
|---|---|---|---|---|
| Primera ronda | 0, 1 | 2, 3 | 4, 5 | 6, 7 |
| Segunda ronda | 8, 9 | 10, 11 | 12, 13 | 14, 15 |
| Tercera ronda | 16, 17 | 18, 19 | 20, 21 | 22, 23 |

Todos trabajan simultáneamente en cada ronda. Después de terminar su paquete,
participan en una consulta: **«¿Alguien encontró la clave?»**. Si alguien acertó,
todos paran; de lo contrario, siguen. Esa consulta es `MPI_Allreduce` en el código.

## ¿Qué proponemos?

Dividir toda la lista en **cuatro sectores consecutivos**. Cada proceso empieza
al principio de su sector y avanza hacia adelante. Eso significa **particiones
contiguas**: sectores con claves consecutivas.

| Proceso | Sector asignado | Primera ronda | Segunda ronda |
|---|---|---|---|
| 0 | 0 al 9 | 0, 1 | 2, 3 |
| 1 | 10 al 19 | 10, 11 | 12, 13 |
| 2 | 20 al 29 | 20, 21 | 22, 23 |
| 3 | 30 al 39 | 30, 31 | 32, 33 |

Seguimos usando paquetes y la consulta después de cada ronda. El primer cambio
es la asignación de las claves.

- Si la clave es **10**, la propuesta la prueba en la primera ronda; el Lab 3
  llega en la segunda.
- Si la clave es **2**, el Lab 3 la prueba en la primera ronda; la propuesta
  llega en la segunda.

## Segundo nivel: hilos dentro de cada proceso

El segundo cambio ocurre dentro de cada proceso. En lugar de probar su paquete
clave por clave, el proceso lo reparte entre varios **hilos** (OpenMP) que
comparten su memoria. Cuando un hilo termina su parte, toma la siguiente que
quede libre (planificación dinámica). Cada hilo usa su propio contexto de
descifrado. Si un hilo acierta, levanta una **bandera** compartida y los demás
hilos de ese proceso dejan de probar sin esperar a que termine el paquete.

Solo el hilo principal de cada proceso participa en la consulta entre procesos
(`MPI_Allreduce`); por eso MPI se inicia en modo `MPI_THREAD_FUNNELED`.
Así podemos usar los mismos cuatro núcleos como 4 procesos × 1 hilo,
2 × 2 o 1 × 4.

Atención: los sectores dependen del número de **procesos**, no de hilos.
Con 2 procesos solo hay dos sectores (el segundo empieza en `2^55`) y con 1
proceso el recorrido vuelve a empezar en 0. En esas configuraciones las claves
grandes del enunciado ya no están al inicio de un sector.

## ¿Por qué favorece las claves grandes del proyecto?

La lista real contiene `2^56` candidatas, desde 0 hasta `2^56 - 1`. Dividirla
entre cuatro da sectores de `2^54` claves.

Por eso el proceso 1 comienza en `2^54`, que es **18014398509481984**.
La otra clave solicitada, **18014398509481985**, es la siguiente.
Es la misma situación que la clave 10 del ejemplo: está al principio del
segundo sector. El reparto cíclico debe recorrer muchísimas claves anteriores
antes de llegar allí.

El programa de búsqueda no recibe la clave secreta. Los sectores se calculan
solo usando el tamaño de la lista y el número de procesos. Estas claves son
favorables para la propuesta porque coinciden con uno de esos límites.

## ¿Qué comprobamos?

Con 4 procesos × 1 hilo, para **123456** la versión cíclica tardó
aproximadamente **0.005 segundos** y la de sectores **0.020 segundos**: la
propuesta fue unas cuatro veces más lenta. Recuperó cada clave grande en menos
de una milésima de segundo. (Estas cifras se midieron con el prototipo de
sectores sin hilos; repetir `make benchmark` para las configuraciones 2 × 2 y 1 × 4.)
Las búsquedas cíclicas grandes se detuvieron después de tres segundos sin
encontrarlas; sus tiempos finales no se midieron.

**La propuesta no mejora todas las búsquedas.** Su ventaja depende de dónde está
la clave. Ambas estrategias siguen siendo búsquedas por fuerza bruta y no
prometemos una mejora general para claves elegidas al azar.

Los [resultados completos](measurements/RESULTADOS.md) incluyen comandos,
repeticiones y estimaciones. Se midió el mismo programa actual cambiando solo
el reparto; no se comparó directamente contra el ejecutable original del Lab 3.

## ¿Qué más cambió del Lab 3 al proyecto?

| Aspecto | Lab 3 del equipo | Proyecto actual |
|---|---|---|
| Mensaje | `Puedes lograrlo!`, fijo en el código y de 16 bytes | Texto desde archivo, con distintas longitudes |
| Señal de acierto | Empieza con `Puedes` y el resto es imprimible | Contiene `es una prueba de`; se valida el relleno |
| Preparación | El programa MPI primero cifraba usando la clave `-k` | El cifrado es un paso separado; la búsqueda recibe el archivo cifrado |
| Rango | Por defecto 20 bits, máximo 40 | Hasta 56 bits |
| Reparto | Paquetes por turnos | Se conserva y se añade la estrategia híbrida: sectores entre procesos e hilos OpenMP dentro de cada uno |

Leer archivos, ampliar el rango y usar la nueva frase corresponden a los
**incisos 1 y 2**. **La propuesta del inciso 3 es la estrategia híbrida.**
Para compararla usamos el mismo programa actual y cambiamos `-s cyclic`
por `-s hybrid -t <hilos>`, manteniendo los demás parámetros.

## Cómo presentarlo con sus palabras

> En el Lab 3 repartimos paquetes de claves por turnos entre los procesos.
> Proponemos dos niveles: MPI divide el rango completo en sectores consecutivos,
> uno por proceso, y dentro de cada proceso varios hilos OpenMP se reparten cada
> paquete. Con cuatro procesos buscamos simultáneamente desde cuatro puntos
> distintos, lo que beneficia las claves al inicio de un sector, como las dos
> grandes del enunciado. También mostraremos los casos donde funciona peor, como
> 123456 o las configuraciones con menos procesos, para comparar ventajas y limitaciones.

El [documento técnico](propuesta-inciso3.md) conserva fórmulas, funcionamiento
de la comunicación y referencias para ampliar la explicación.
