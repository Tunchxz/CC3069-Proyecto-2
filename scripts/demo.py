"""Demostracion reproducible de los incisos 1 y 2 con registro de terminal."""

import os
from pathlib import Path
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
ENV = dict(os.environ, OMPI_ALLOW_RUN_AS_ROOT="1", OMPI_ALLOW_RUN_AS_ROOT_CONFIRM="1")


def main():
    os.chdir(ROOT)
    folder = ROOT / "docs/measurements"
    folder.mkdir(parents=True, exist_ok=True)
    with (folder / "demo.txt").open("w", encoding="utf-8") as log:
        def emit(text):
            print(text, end="", flush=True)
            log.write(text)
            log.flush()

        def run(command):
            emit(f"\n$ {shlex.join(command)}\n")
            result = subprocess.run(command, env=ENV, text=True, capture_output=True, timeout=60)
            emit(result.stdout + result.stderr)
            if result.returncode:
                raise RuntimeError(f"Codigo de salida: {result.returncode}")

        run(["make", "-B", "build"])
        source = ROOT / "examples/mensaje.txt"
        if source.read_bytes() != b"Esta es una prueba de proyecto 2":
            raise RuntimeError("El ejemplo debe coincidir exactamente con el enunciado (sin salto final).")
        run(["cat", "examples/mensaje.txt"])
        emit("\n")
        run(["build/aes_tool", "encrypt", "-i", "examples/mensaje.txt", "-o", "build/mensaje.aes", "-k", "123456"])
        run(["od", "-An", "-tx1", "build/mensaje.aes"])
        run(["build/aes_tool", "decrypt", "-i", "build/mensaje.aes", "-o", "build/recuperado.txt", "-k", "123456"])
        run(["cat", "build/recuperado.txt"])
        emit("\n")
        run(["cmp", "examples/mensaje.txt", "build/recuperado.txt"])
        emit("OK: archivos original y recuperado identicos.\n")
        run(["build/search_seq", "-i", "build/mensaje.aes", "-b", "56"])
        run(["mpirun", "--oversubscribe", "-np", "4", "build/search_mpi", "-i", "build/mensaje.aes", "-b", "56", "-s", "cyclic"])
        emit("\nDemostracion completada; registro: docs/measurements/demo.txt\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
