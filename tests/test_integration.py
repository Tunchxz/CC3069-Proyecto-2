"""Pruebas reales de archivos, EVP/OpenSSL y finalizacion colectiva MPI."""

import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ENV = dict(os.environ, OMPI_ALLOW_RUN_AS_ROOT="1", OMPI_ALLOW_RUN_AS_ROOT_CONFIRM="1")
MESSAGE = b"Esta es una prueba de proyecto 2"
# Base del inciso 2 y la estrategia hibrida con uno y varios hilos por proceso.
VARIANTS = [("cyclic", 1), ("hybrid", 1), ("hybrid", 4)]


def run(args, code=0):
    result = subprocess.run([str(x) for x in args], cwd=ROOT, env=ENV,
                            capture_output=True, text=True, timeout=30)
    if result.returncode != code:
        raise AssertionError(f"{args}\ncode={result.returncode}, expected={code}\n"
                             f"{result.stdout}\n{result.stderr}")
    return result.stdout


class IntegrationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name)

    def encrypt(self, content=MESSAGE, key=123456):
        source = self.path / "original.txt"
        encrypted = self.path / "mensaje.aes"
        source.write_bytes(content)
        run(["build/aes_tool", "encrypt", "-i", source, "-o", encrypted, "-k", key])
        return source, encrypted

    def search(self, encrypted, bits=20, strategy="cyclic", processes=4,
               code=0, extra=(), chunk=4096, threads=1):
        return run(["mpirun", "--oversubscribe", "-np", processes, "build/search_mpi",
                    "-i", encrypted, "-b", bits, "-s", strategy, "-c", chunk,
                    "-t", threads, *extra], code)

    def test_encrypt_decrypt_round_trip(self):
        for content in [b"", b"a" * 16, b"b" * 32, MESSAGE,
                        "Texto con tildes: canción y ñ.\n".encode(),
                        bytes(range(256)) * 20]:
            with self.subTest(length=len(content)):
                source, encrypted = self.encrypt(content)
                recovered = self.path / "recuperado.txt"
                run(["build/aes_tool", "decrypt", "-i", encrypted,
                     "-o", recovered, "-k", 123456])
                self.assertEqual(source.read_bytes(), recovered.read_bytes())

    def test_openssl_compatibility_and_random_iv(self):
        _, encrypted = self.encrypt()
        first = encrypted.read_bytes()
        self.encrypt()
        self.assertNotEqual(first, encrypted.read_bytes())
        raw = self.path / "raw.bin"
        raw.write_bytes(first[16:])
        result = subprocess.run(["openssl", "enc", "-d", "-aes-128-cbc",
                                 "-K", "5556472d43433330000000000001e240",
                                 "-iv", first[:16].hex(), "-in", str(raw)],
                                capture_output=True, check=True, timeout=10)
        self.assertEqual(result.stdout, MESSAGE)

    def test_reject_invalid_input_without_overwriting(self):
        source, encrypted = self.encrypt(b"")
        run(["build/aes_tool", "encrypt", "-i", source, "-o", source, "-k", 1], 2)
        malformed = self.path / "bad.aes"
        malformed.write_bytes(b"short")
        output = self.path / "must_not_exist.txt"
        run(["build/aes_tool", "decrypt", "-i", malformed, "-o", output, "-k", 1], 2)
        # CBC: cambiar el ultimo byte del IV destruye el relleno del bloque vacio.
        damaged = bytearray(encrypted.read_bytes())
        damaged[15] ^= 1
        malformed.write_bytes(damaged)
        run(["build/aes_tool", "decrypt", "-i", malformed,
             "-o", output, "-k", 123456], 2)
        self.assertFalse(output.exists())
        for bad_key in ["-1", "12abc", "18446744073709551616"]:
            run(["build/aes_tool", "encrypt", "-i", source,
                 "-o", encrypted, "-k", bad_key], 2)
        run(["build/search_seq", "-i", "no-existe.aes"], 2)
        for args in [["-b", 57], ["-b", 0], ["-p", ""], ["-b", "-1"]]:
            run(["build/search_seq", "-i", encrypted, *args], 2)

    def test_recover_required_short_key(self):
        _, encrypted = self.encrypt()
        seq = run(["build/search_seq", "-i", encrypted])
        self.assertIn("Clave encontrada: 123456", seq)
        for strategy, threads in VARIANTS:
            result = self.search(encrypted, strategy=strategy, threads=threads)
            self.assertIn("Clave encontrada: 123456", result)
            self.assertIn("Procesos: 4", result)
            self.assertIn(f"Hilos por proceso: {threads}", result)
            self.assertIn(MESSAGE.decode(), result)

    def test_exhaustion_and_collective_termination(self):
        _, encrypted = self.encrypt(key=100)
        seq = run(["build/search_seq", "-i", encrypted, "-b", 4], 1)
        self.assertIn("Candidatas probadas: 16", seq)
        for processes in [1, 3, 5]:
            for strategy, threads in VARIANTS:
                result = self.search(encrypted, bits=2, processes=processes,
                                     strategy=strategy, code=1, chunk=3,
                                     threads=threads)
                self.assertIn("Estado: rango_agotado", result)
                self.assertIn("Candidatas probadas: 4", result)

    def test_first_last_and_partition_boundary(self):
        for key in [0, 63, 64, 65, 255]:
            _, encrypted = self.encrypt(key=key)
            for strategy, threads in VARIANTS:
                result = self.search(encrypted, bits=8, strategy=strategy, chunk=7,
                                     threads=threads)
                self.assertIn(f"Clave encontrada: {key}\n", result)

    def test_large_keys_fit_and_hybrid_finds_them(self):
        for key in [2**54, 2**54 + 1]:
            _, encrypted = self.encrypt(key=key)
            for threads in [1, 4]:
                result = self.search(encrypted, bits=56, strategy="hybrid",
                                     threads=threads)
                self.assertIn(f"Clave encontrada: {key}", result)

    def test_reject_invalid_thread_count(self):
        _, encrypted = self.encrypt()
        for threads in [0, 257, "-1"]:
            self.search(encrypted, strategy="hybrid", threads=threads, code=2)

    def test_time_limit_is_not_reported_as_exhaustion(self):
        _, encrypted = self.encrypt(key=2**64 - 1)
        seq = run(["build/search_seq", "-i", encrypted, "-b", 56, "--seconds", 1], 3)
        self.assertIn("Estado: limite_tiempo", seq)
        for strategy, threads in VARIANTS:
            result = self.search(encrypted, bits=56, strategy=strategy,
                                 code=3, extra=["--seconds", 1], threads=threads)
            self.assertIn("Estado: limite_tiempo", result)
            self.assertGreater(int(re.search(r"Candidatas probadas: (\d+)", result)[1]), 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
