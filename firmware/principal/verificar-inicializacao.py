"""Reinicia pela RTS e verifica somente o boot do Incremento 1, sem enviar comandos."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import sys
import time

import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--seconds", type=int, default=30, choices=range(10, 121), metavar="10..120")
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    result = {
        "startedAtUtc": datetime.now(timezone.utc).isoformat(),
        "port": args.port,
        "baudRate": 115200,
        "requestedSeconds": args.seconds,
        "success": False,
        "scope": "Boot do Incremento 1; sem observacoes simuladas, partidas ou perifericos.",
    }
    received = bytearray()
    try:
        result["elfSha256"] = hashlib.sha256(args.elf.read_bytes()).hexdigest()
        with serial.Serial(port=None, baudrate=115200, timeout=0.2) as connection:
            connection.dtr = False
            connection.rts = False
            connection.port = args.port
            connection.open()
            connection.reset_input_buffer()
            # CP2102 da DevKit: RTS aciona EN; DTR permanece desativado.
            connection.rts = True
            time.sleep(0.1)
            connection.rts = False
            start = time.monotonic()
            while time.monotonic() - start < args.seconds:
                received.extend(connection.read(connection.in_waiting or 1))
            result["elapsedSeconds"] = round(time.monotonic() - start, 3)

        text = received.decode("utf-8", errors="replace")
        boot_count = text.count("BOOT firmware=principal increment=1")
        match = re.search(r"^ELF_SHA256 ([0-9a-f]{64})\r?$", text, re.MULTILINE)
        result["reportedElfSha256"] = match.group(1) if match else None
        alive = [
            {"uptimeMs": int(a), "ready": int(b), "heapBytes": int(c), "stackFreeBytes": int(d)}
            for a, b, c, d in re.findall(
                r"^ALIVE uptime_ms=(\d+) ready=(\d+) heap_bytes=(\d+) stack_free_bytes=(\d+)\r?$",
                text, re.MULTILINE
            )
        ]
        result["alive"] = alive
        result["bootCount"] = boot_count
        result["checks"] = {
            "singleBoot": boot_count == 1,
            "elfMatches": result["reportedElfSha256"] == result["elfSha256"],
            "validatedCore": bool(re.search(r"^PLATFORM arduino=3\.3\.12 idf=v5\.5\.5\r?$", text, re.MULTILINE)),
            "boardConfiguration": bool(re.search(r"^BOARD chip=ESP32[^\r\n]* cpu_mhz=240 flash_bytes=4194304 psram_bytes=0 reset=\d+\r?$", text, re.MULTILINE)),
            "initialState": "READY increment=1 state=AwaitingPosition record=started ply=0" in text,
            "alive": len(alive) >= 4 and all(item["ready"] == 1 for item in alive),
            "monotonicUptime": all(a["uptimeMs"] < b["uptimeMs"] for a, b in zip(alive, alive[1:])),
            "noRuntimeFailure": not re.search(r"ERROR|Guru Meditation|panic|abort\(\)|Stack canary|watchdog", text, re.IGNORECASE),
        }
        result["success"] = all(result["checks"].values())
    except Exception as error:
        result["failure"] = f"{type(error).__name__}: {error}"
    finally:
        (args.output_dir / "serial.log").write_bytes(received)
        result["finishedAtUtc"] = datetime.now(timezone.utc).isoformat()
        (args.output_dir / "boot-result.json").write_text(
            json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
        )
    print(json.dumps(result, indent=2, ensure_ascii=False))
    return 0 if result["success"] else 1


if __name__ == "__main__":
    sys.exit(main())
