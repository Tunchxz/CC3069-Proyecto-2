#!/usr/bin/env bash
# Comandos de cada captura del informe. Uso: bash scripts/capturas.sh <1-5>
# Muestra cada comando antes de ejecutarlo para que se vea en la captura.
set -u
cd "$(dirname "$0")/.."

run() {
    printf '\n\033[1;32m$ %s\033[0m\n' "$*"
    "$@"
}

# mpirun de Kali imprime un aviso por un archivo de ayuda faltante al salir con
# codigo distinto de 0; se filtra solo ese bloque y se muestra el codigo real.
mpi_limit() {
    printf '\n\033[1;32m$ %s\033[0m\n' "$*"
    "$@" 2>/dev/null
    echo "codigo de salida: $?"
}

prepare() {
    [ -x build/search_mpi ] || make -s build
    for key in 123456 18014398509481984 18014398509481985; do
        [ -f "build/k$key.aes" ] || ./build/aes_tool encrypt -i examples/mensaje.txt \
            -o "build/k$key.aes" -k "$key" >/dev/null
    done
}

clear
case "${1:-}" in
    1)
        [ -x build/aes_tool ] || make -s build
        run cat examples/mensaje.txt; echo
        run ./build/aes_tool encrypt -i examples/mensaje.txt -o build/mensaje.aes -k 123456
        run od -An -tx1 build/mensaje.aes
        run ./build/aes_tool decrypt -i build/mensaje.aes -o build/recuperado.txt -k 123456
        run cat build/recuperado.txt; echo
        run cmp examples/mensaje.txt build/recuperado.txt && echo "OK: archivos identicos"
        ;;
    2)
        run make -B build
        printf '\n\033[1;32m$ make test\033[0m\n'
        make test 2>&1 | grep -aE "^OK|ok$|^Ran|FAIL|ERROR" |
            sed -E 's/ \(__main__\.IntegrationTests\.[a-z_]+\)//'
        ;;
    3)
        prepare; clear
        run mpirun -np 4 --oversubscribe ./build/search_mpi -i build/k123456.aes -b 56 -s cyclic
        mpi_limit mpirun -np 4 --oversubscribe ./build/search_mpi -i build/k18014398509481984.aes -b 56 -s cyclic --seconds 3
        mpi_limit mpirun -np 4 --oversubscribe ./build/search_mpi -i build/k18014398509481985.aes -b 56 -s cyclic --seconds 3
        ;;
    4)
        prepare; clear
        run mpirun -np 4 --oversubscribe ./build/search_mpi -i build/k18014398509481984.aes -b 56 -s hybrid -t 1
        run mpirun -np 4 --oversubscribe ./build/search_mpi -i build/k18014398509481985.aes -b 56 -s hybrid -t 1
        ;;
    5)
        prepare; clear
        run mpirun -np 2 --oversubscribe ./build/search_mpi -i build/k123456.aes -b 56 -s hybrid -t 2
        run mpirun -np 1 --oversubscribe ./build/search_mpi -i build/k123456.aes -b 56 -s hybrid -t 4
        ;;
    *)
        echo "Uso: bash scripts/capturas.sh <1-5>"
        exit 2
        ;;
esac
