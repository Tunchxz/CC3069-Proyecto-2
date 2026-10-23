"""Mediciones reproducibles; las busquedas inalcanzables tienen limite real."""

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import platform
import re
import shlex
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[1]
MESSAGE = b"Esta es una prueba de proyecto 2"
ENV = dict(os.environ, OMPI_ALLOW_RUN_AS_ROOT="1", OMPI_ALLOW_RUN_AS_ROOT_CONFIRM="1")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--seconds", type=int, default=3,
                        help="limite para cada corrida con la clave lejos del inicio de su tramo")
    args = parser.parse_args()
    if args.repeats < 1 or args.seconds < 1:
        parser.error("repeats y seconds deben ser positivos")
    os.chdir(ROOT)
    output = ROOT / "docs/measurements"
    logs = output / "logs"
    inputs = ROOT / "build/bench_inputs"
    logs.mkdir(parents=True, exist_ok=True)
    inputs.mkdir(parents=True, exist_ok=True)
    source = inputs / "original.txt"
    source.write_bytes(MESSAGE)
    records = []

    def execute(label, command, expected=0):
        command = list(map(str, command))
        result = subprocess.run(command, env=ENV, capture_output=True, text=True,
                                timeout=max(60, args.seconds + 30))
        transcript = (f"$ {shlex.join(command)}\n{result.stdout}"
                      f"{result.stderr}\nexit_code={result.returncode}\n")
        (logs / f"{label}.txt").write_text(transcript, encoding="utf-8")
        if result.returncode != expected:
            raise RuntimeError(transcript)
        return result.stdout

    environment = {
        "utc": datetime.now(timezone.utc).isoformat(),
        "platform": platform.platform(), "logical_cpus": os.cpu_count(),
        "gcc": execute("version-gcc", ["gcc", "--version"]).splitlines()[0],
        "mpi": execute("version-mpi", ["mpirun", "--version"]).strip(),
        "openssl": execute("version-openssl", ["openssl", "version"]).strip(),
    }
    cases = [(123456, 56), (2**18, 20), (2**18 + 1, 20),
             (2**54, 56), (2**54 + 1, 56)]
    # (estrategia, procesos MPI, hilos por proceso); siempre 4 nucleos en total.
    configs = [("seq", 1, 1), ("cyclic", 4, 1), ("hybrid", 4, 1),
               ("hybrid", 2, 2), ("hybrid", 1, 4)]
    chunk = 4096

    def position(key, bits, strategy, processes):
        """Desplazamiento de la clave dentro del recorrido de su proceso."""
        return key if strategy != "hybrid" else key % ((1 << bits) // processes)

    for key, bits in cases:
        encrypted = inputs / f"{key}.aes"
        cipher_path = encrypted.relative_to(ROOT)
        execute(f"encrypt-{key}", ["build/aes_tool", "encrypt", "-i",
                                   source.relative_to(ROOT), "-o", cipher_path, "-k", key])
        for strategy, processes, threads in configs:
            if strategy == "seq" and key >= 2**54:
                continue
            # Solo las claves al inicio de un tramo son alcanzables; el resto se limita.
            limited = position(key, bits, strategy, processes) >= 2**40
            repeats = 1 if limited else args.repeats
            label = strategy if strategy == "seq" else f"{strategy}-{processes}x{threads}"
            for trial in range(1, repeats + 1):
                command = (["build/search_seq"] if strategy == "seq" else
                           ["mpirun", "--oversubscribe", "-np", processes, "build/search_mpi",
                            "-s", strategy, "-c", chunk, "-t", threads])
                command += ["-i", cipher_path, "-b", bits]
                if limited:
                    command += ["--seconds", args.seconds]
                text = execute(f"{key}-{label}-{trial}", command, 3 if limited else 0)
                if not limited and f"Clave encontrada: {key}\n" not in text:
                    raise RuntimeError("La busqueda encontro una clave inesperada")
                row = {"key": key, "bits": bits, "strategy": strategy,
                       "processes": processes, "threads": threads, "config": label,
                       "trial": trial,
                       "status": "limite_tiempo" if limited else "encontrada",
                       "tested": int(re.search(r"Candidatas probadas: (\d+)", text)[1]),
                       "seconds": float(re.search(r"Tiempo: ([0-9.]+)", text)[1]),
                       "command": shlex.join(map(str, command))}
                records.append(row)
                print(f"{key} b={bits} {label} #{trial}: "
                      f"{row['seconds']:.6f}s, {row['tested']} candidatas, {row['status']}", flush=True)

    estimates = []
    for row in records:
        if row["status"] != "limite_tiempo":
            continue
        key, processes = row["key"], row["processes"]
        offset = position(key, row["bits"], row["strategy"], processes)
        # Los otros procesos completan su bloque; el ganador termina al encontrarla.
        rounds_before = offset // chunk // (processes if row["strategy"] == "cyclic" else 1)
        expected_tests = (rounds_before * processes * chunk + (processes - 1) * chunk
                          + offset % chunk + 1)
        rate = row["tested"] / row["seconds"]
        estimates.append({"key": key, "config": row["config"], "rate_total_keys_s": rate,
                          "predicted_tests": expected_tests,
                          "estimated_days": expected_tests / rate / 86400})
    payload = {"environment": environment, "repeats": args.repeats,
               "chunk": chunk, "records": records, "estimates": estimates}
    (output / "results.json").write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")

    def mean_for(key, label):
        values = [r["seconds"] for r in records if r["key"] == key and
                  r["config"] == label and r["status"] == "encontrada"]
        return statistics.mean(values) if values else None

    labels = ["seq", "cyclic-4x1", "hybrid-4x1", "hybrid-2x2", "hybrid-1x4"]
    lines = ["# Mediciones", "", f"Registro UTC: {environment['utc']}", "",
             f"Entorno: {environment['platform']}; {environment['logical_cpus']} CPU lógicas.",
             f"Compilador: {environment['gcc']}. OpenSSL: {environment['openssl']}.", "",
             f"Promedios de {args.repeats} corridas por búsqueda completada; bloques de {chunk}.",
             "Configuraciones MPI como procesos x hilos por proceso, siempre 4 núcleos en total. "
             "La base es la estrategia cíclica del inciso 2 (4x1); la híbrida reparte tramos contiguos entre procesos y cada bloque entre hilos OpenMP.",
             "El tiempo mide solo la búsqueda (máximo de los procesos), excluye lectura, difusión inicial, arranque de mpirun y presentación del resultado.", "",
             "| Clave | Bits | Secuencial (s) | Cíclica 4x1 (s) | Híbrida 4x1 (s) | Híbrida 2x2 (s) | Híbrida 1x4 (s) | T_cíclica / T_híbrida 4x1 |",
             "|---|---:|---:|---:|---:|---:|---:|---:|"]
    for key, bits in cases:
        means = [mean_for(key, label) for label in labels]
        fmt = lambda n: f"{n:.6f}" if n is not None else "Interrumpida"
        cyclic, hybrid = means[1], means[2]
        ratio = f"{cyclic / hybrid:.2f}" if cyclic is not None and hybrid else "No medido"
        lines.append(f"| {key} | {bits} | " + " | ".join(map(fmt, means)) + f" | {ratio} |")
    lines += ["", "## Búsquedas interrumpidas y estimaciones", "",
              f"Las configuraciones en las que la clave queda lejos del inicio de un tramo se ejecutaron realmente durante aproximadamente {args.seconds} segundos con `--seconds {args.seconds}`. Salida 3 indica límite, no rango agotado ni clave encontrada.",
              "La extrapolación supone tasa constante y el mismo equipo. No constituye un tiempo final medido.", "",
              "| Clave | Configuración | Candidatas/s agregadas medidas | Candidatas previstas hasta parada | Días estimados |",
              "|---|---|---:|---:|---:|"]
    for estimate in estimates:
        lines.append(f"| {estimate['key']} | {estimate['config']} | {estimate['rate_total_keys_s']:.0f} | "
                     f"{estimate['predicted_tests']} | {estimate['estimated_days']:.1f} |")
    lines += ["", "## Interpretación y evidencia", "",
              "La estrategia híbrida cambia el orden de exploración. Con 4 procesos, su ventaja para las claves grandes procede de que están al inicio del segundo cuarto; con 2 o 1 procesos los tramos son más largos y esas claves vuelven a quedar lejos. No elimina el costo exponencial ni garantiza mejorar para cualquier clave.",
              "Con los mismos 4 núcleos, cambiar procesos por hilos no aumenta las candidatas por segundo; compara el costo de coordinar procesos (MPI_Allreduce) contra el de coordinar hilos (OpenMP).",
              "Los casos de 20 bits son demostraciones reducidas del límite entre cuartos. Los casos exigidos por el enunciado usan 56 bits.",
              "Una clave baja puede favorecer la distribución cíclica; comparar también el número de candidatas y no solo el tiempo.",
              "Los tiempos muy cortos tienen variación relativa alta: se conservan todas las corridas para inspección.",
              "Los comandos y salidas completos están en `logs/`; los datos sin resumir, en `results.json`.",
              "Reproducir desde la raíz con `make benchmark`. Las capturas de pantalla para la entrega deben tomarse de la terminal al ejecutar la demostración.", ""]
    (output / "RESULTADOS.md").write_text("\n".join(lines), encoding="utf-8")
    print("Resultados: docs/measurements/RESULTADOS.md", flush=True)


if __name__ == "__main__":
    main()
