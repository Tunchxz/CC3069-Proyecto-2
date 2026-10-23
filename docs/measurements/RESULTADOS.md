# Mediciones

Registro UTC: 2026-10-23T23:15:41.544101+00:00

Entorno: Linux-6.6.114.1-microsoft-standard-WSL2-x86_64-with-glibc2.43; 32 CPU lógicas.
Compilador: gcc (Debian 16.2.0-1) 16.2.0. OpenSSL: OpenSSL 3.6.3 9 Jun 2026 (Library: OpenSSL 3.6.3 9 Jun 2026).

Promedios de 3 corridas por búsqueda completada; bloques de 4096.
Configuraciones MPI como procesos x hilos por proceso, siempre 4 núcleos en total. La base es la estrategia cíclica del inciso 2 (4x1); la híbrida reparte tramos contiguos entre procesos y cada bloque entre hilos OpenMP.
El tiempo mide solo la búsqueda (máximo de los procesos), excluye lectura, difusión inicial, arranque de mpirun y presentación del resultado.

| Clave | Bits | Secuencial (s) | Cíclica 4x1 (s) | Híbrida 4x1 (s) | Híbrida 2x2 (s) | Híbrida 1x4 (s) | T_cíclica / T_híbrida 4x1 |
|---|---:|---:|---:|---:|---:|---:|---:|
| 123456 | 56 | 0.017104 | 0.004859 | 0.019511 | 0.009987 | 0.005107 | 0.25 |
| 262144 | 20 | 0.036291 | 0.011796 | 0.000637 | 0.019836 | 0.010063 | 18.51 |
| 262145 | 20 | 0.037262 | 0.010552 | 0.000628 | 0.019692 | 0.009901 | 16.79 |
| 18014398509481984 | 56 | Interrumpida | Interrumpida | 0.000589 | Interrumpida | Interrumpida | No medido |
| 18014398509481985 | 56 | Interrumpida | Interrumpida | 0.000602 | Interrumpida | Interrumpida | No medido |

## Búsquedas interrumpidas y estimaciones

Las configuraciones en las que la clave queda lejos del inicio de un tramo se ejecutaron realmente durante aproximadamente 3 segundos con `--seconds 3`. Salida 3 indica límite, no rango agotado ni clave encontrada.
La extrapolación supone tasa constante y el mismo equipo. No constituye un tiempo final medido.

| Clave | Configuración | Candidatas/s agregadas medidas | Candidatas previstas hasta parada | Días estimados |
|---|---|---:|---:|---:|
| 18014398509481984 | cyclic-4x1 | 27814405 | 18014398509494273 | 7496.1 |
| 18014398509481984 | hybrid-2x2 | 27949606 | 36028797018968065 | 14919.7 |
| 18014398509481984 | hybrid-1x4 | 28102464 | 18014398509481985 | 7419.3 |
| 18014398509481985 | cyclic-4x1 | 27941828 | 18014398509494274 | 7461.9 |
| 18014398509481985 | hybrid-2x2 | 27812550 | 36028797018968066 | 14993.2 |
| 18014398509481985 | hybrid-1x4 | 27597801 | 18014398509481986 | 7554.9 |

## Interpretación y evidencia

La estrategia híbrida cambia el orden de exploración. Con 4 procesos, su ventaja para las claves grandes procede de que están al inicio del segundo cuarto; con 2 o 1 procesos los tramos son más largos y esas claves vuelven a quedar lejos. No elimina el costo exponencial ni garantiza mejorar para cualquier clave.
Con los mismos 4 núcleos, cambiar procesos por hilos no aumenta las candidatas por segundo; compara el costo de coordinar procesos (MPI_Allreduce) contra el de coordinar hilos (OpenMP).
Los casos de 20 bits son demostraciones reducidas del límite entre cuartos. Los casos exigidos por el enunciado usan 56 bits.
Una clave baja puede favorecer la distribución cíclica; comparar también el número de candidatas y no solo el tiempo.
Los tiempos muy cortos tienen variación relativa alta: se conservan todas las corridas para inspección.
Los comandos y salidas completos están en `logs/`; los datos sin resumir, en `results.json`.
Reproducir desde la raíz con `make benchmark`. Las capturas de pantalla para la entrega deben tomarse de la terminal al ejecutar la demostración.
