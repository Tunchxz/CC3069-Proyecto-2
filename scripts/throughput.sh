#!/usr/bin/env bash
# Tasa de candidatas por segundo de cada configuracion: la clave 2^64-1 queda
# fuera del rango de 56 bits, asi que cada corrida termina por --seconds.
set -u
cd "$(dirname "$0")/.."
logs=docs/measurements/logs
mkdir -p "$logs" build/bench_inputs
input=build/bench_inputs/fuera-de-rango.aes
./build/aes_tool encrypt -i examples/mensaje.txt -o "$input" -k 18446744073709551615 >/dev/null
seconds=${1:-3}
run() {
    local label=$1; shift
    { echo "\$ $*"; "$@" 2>&1; echo "exit_code=$?"; } > "$logs/throughput-$label.txt"
    grep -aE "Candidatas probadas|Tiempo" "$logs/throughput-$label.txt" | tr '\n' ' '
    echo "<- $label"
}
run seq build/search_seq -i "$input" -b 56 --seconds "$seconds"
run cyclic-4x1 mpirun --oversubscribe -np 4 build/search_mpi -i "$input" -b 56 -s cyclic -t 1 --seconds "$seconds"
run hybrid-4x1 mpirun --oversubscribe -np 4 build/search_mpi -i "$input" -b 56 -s hybrid -t 1 --seconds "$seconds"
run hybrid-2x2 mpirun --oversubscribe -np 2 build/search_mpi -i "$input" -b 56 -s hybrid -t 2 --seconds "$seconds"
run hybrid-1x4 mpirun --oversubscribe -np 1 build/search_mpi -i "$input" -b 56 -s hybrid -t 4 --seconds "$seconds"
