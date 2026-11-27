"""Terminal do tabuleiro sensorial para USB serial ou simulador nativo."""
import argparse
from contextlib import contextmanager
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
import zlib


HELP = """Comandos de partida
  new               Inicia outra partida e descarta a posição ativa.
  confirm           Confirma o lance após mover as peças.
  promote Q|R|B|N   Escolhe dama, torre, bispo ou cavalo. Depois use confirm.
  finish            Encerra e salva o PGN.
  resign white|black  Registra abandono das brancas ou pretas e salva.
  status / pgn      Consulta os sensores e a posição / exibe o registro.
  save              Tenta novamente uma gravação pendente.
  pasta CAMINHO     Altera a pasta dos próximos PGNs.
  ajuda / sair      Mostra esta ajuda / fecha o terminal."""

HINTS = {
    "CONFIRM Accepted": "Lance aceito.",
    "CONFIRM NoMove": "Nenhum movimento detectado.",
    "CONFIRM PromotionRequired": "Escolha a peça com promote Q, R, B ou N e confirme.",
    "CONFIRM UnreliableInput": "Aguarde os sensores. Confira a posição com status.",
    "CONFIRM NoLegalMatch": "Lance inválido. Reponha a posição anterior e refaça o lance.",
    "CONFIRM Ambiguous": "Movimento ambíguo. Reponha a posição anterior.",
    "CONFIRM PositionMismatch": "Reponha as peças na última posição aceita.",
    "ERROR SavePending": "Use save antes de iniciar outra partida.",
}


@contextmanager
def connection(args):
    if args.sim:
        with subprocess.Popen([str(args.sim.resolve())], stdin=subprocess.PIPE,
                              stdout=subprocess.PIPE) as process:
            try:
                yield process.stdin, process.stdout
            finally:
                process.stdin.close()  # EOF encerra o simulador
        return

    try:
        import serial
    except ImportError as exc:
        raise RuntimeError("Instale a dependência com python -m pip install pyserial==3.5") from exc
    with serial.Serial(port=None, baudrate=115200, timeout=0.25, write_timeout=2) as port:
        port.dtr = port.rts = False
        port.port = args.port
        port.open()
        # Alguns adaptadores reiniciam a placa ao abrir a porta.
        time.sleep(1.5)
        port.reset_input_buffer()
        yield port, port


def request(writer, reader, command):
    writer.write((command + "\n").encode("ascii"))
    writer.flush()
    deadline = time.monotonic() + 5
    frame = bytearray()
    while time.monotonic() < deadline:
        chunk = reader.readline(1024)
        if not chunk and writer is not reader:
            raise ConnectionError("O simulador encerrou antes de DONE.")
        frame.extend(chunk)
        if len(frame) > 16384:
            raise ValueError("Resposta excedeu o limite do protocolo.")
        if frame.endswith(b"DONE\n") or frame.endswith(b"DONE\r\n"):
            lines = frame.decode("utf-8").replace("\r\n", "\n").split("\n")
            if lines[-2] == "DONE":
                return lines[:-2]
    raise TimeoutError("Resposta incompleta. Reconecte o terminal e consulte status.")


def archive(directory, game_id, data):
    directory.mkdir(parents=True, exist_ok=True)
    final = directory / f"partida-{game_id}.pgn"
    # Sincronizar e fechar antes de publicar. Nenhum leitor vê um PGN parcial.
    stream = tempfile.NamedTemporaryFile(dir=directory, prefix=".partida-", suffix=".tmp", delete=False)
    temporary = Path(stream.name)
    try:
        with stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        try:
            if os.name == "nt":
                os.rename(temporary, final)  # no Windows falha se o destino existe
            else:
                os.link(temporary, final)    # criação exclusiva no mesmo volume
        except FileExistsError:
            if final.read_bytes() != data:
                raise FileExistsError(f"{final} já existe com outro conteúdo. Use pasta CAMINHO.")
    finally:
        temporary.unlink(missing_ok=True)
    return final


def save_pending(send, notification, directory):
    offer = re.fullmatch(r"SAVE_PENDING ([0-9a-f]{16}-[0-9]{1,10}) ([0-9]{1,5}) ([0-9]{1,10})", notification)
    if not offer:
        raise ValueError("Identificação de PGN pendente inválida.")
    game_id, length, checksum = offer.groups()
    block = send("pgn " + game_id)
    if len(block) < 3 or block[0] != "PGN_BEGIN" or block[-1] != "PGN_END":
        print("Bloco PGN incompleto ou sessão alterada. Consulte status e tente save.")
        return False
    data = ("\n".join(block[1:-1]) + "\n").encode("utf-8")
    if len(data) != int(length) or zlib.crc32(data) != int(checksum):
        print("Tamanho ou CRC do PGN divergente. Use save para repetir a transferência.")
        return False
    try:
        path = archive(directory, game_id, data)
    except OSError as exc:
        print(f"Falha ao salvar: {exc}\nPGN pendente. Corrija a pasta e envie save.")
        return False
    print(f"PGN gravado em {path}")
    if send("saved " + game_id) != ["OK saved"]:
        raise ValueError("O arquivo foi gravado, mas a placa não confirmou a sessão.")
    return True


def interact(writer, reader, args):
    def send(command):
        return request(writer, reader, command)

    if "PROTOCOL 2" not in send("hello"):
        raise ValueError("O dispositivo não respondeu com o protocolo 2 do tabuleiro.")
    directory = args.destino.resolve()
    print(HELP)
    if args.sim:
        print("Simulação: sync monta a posição inicial. Experimente lift e2, place e4 e confirm.")
    print(f"Pasta dos PGNs: {directory}")
    pending = False

    def execute(command):
        nonlocal pending
        for line in send(command):
            if line.startswith("SAVE_PENDING "):
                pending = not save_pending(send, line, directory)
            else:
                print(line)
                if line in HINTS:
                    print(HINTS[line])

    execute("status")  # Recupera também um PGN pendente na reconexão.
    while True:
        try:
            command = input("xadrez> ").strip()
        except EOFError:
            break
        if command in ("sair", "exit", "quit"):
            break
        if command in ("ajuda", "help"):
            print(HELP)
            execute("help")
        elif command.startswith("pasta "):
            directory = Path(command[6:].strip()).expanduser().resolve()
            print(f"Pasta dos PGNs: {directory}. Use save para repetir.")
        elif command.split(" ", 1)[0] == "saved":
            print("A confirmação de gravação é enviada automaticamente.")
        elif command and len(command) <= 96 and command.isascii() and command.isprintable():
            execute(command)
        else:
            print("Digite um comando ASCII com até 96 caracteres. Use ajuda.")

    if pending:
        print("PGN pendente de gravação. A sessão em RAM depende da alimentação do ESP32."
              if args.port else "O simulador terminou com um PGN pendente.", file=sys.stderr)
    return int(pending)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--port", metavar="PORTA", help="porta USB, por exemplo COM7 ou /dev/ttyUSB0")
    source.add_argument("--sim", type=Path, metavar="EXECUTÁVEL", help="executável tabuleiro_sim")
    parser.add_argument("--destino", type=Path, default=Path("build/partidas"), help="pasta dos PGNs")
    args = parser.parse_args()
    try:
        with connection(args) as (writer, reader):
            return interact(writer, reader, args)
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"Terminal encerrado: {exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    sys.exit(main())
