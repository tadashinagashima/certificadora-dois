"""Terminal do tabuleiro sensorial para USB serial ou simulador nativo."""
import argparse
from contextlib import contextmanager
from functools import lru_cache
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
import unicodedata
import zlib


HELP = """Comandos de partida
  nomes             Escolhe os nomes exibidos no letreiro do OLED.
  new               Inicia outra partida e descarta a posição ativa.
  promote Q|R|B|N   Promove o peão a dama, torre, bispo ou cavalo.
  finish            Encerra e salva o PGN.
  resign white|black  Registra abandono das brancas ou pretas e salva.
  status / pgn      Consulta os sensores e a posição / exibe o registro.
  maquina / local    Máquina joga de pretas / dois jogadores. Mantém a posição.
  tecnico           Mostra o status bruto da placa, incluindo FEN e diagnóstico.
  save              Tenta novamente uma gravação pendente.
  pasta CAMINHO     Altera a pasta dos próximos PGNs.
  ajuda / sair      Mostra esta ajuda / fecha o terminal."""

HINTS = {
    "CONFIRM Accepted": "Lance aceito.",
    "CONFIRM UnexpectedMove": "Reponha a posição anterior e execute o lance indicado pela máquina.",
    "CONFIRM NoMove": "Nenhum movimento detectado.",
    "CONFIRM PromotionRequired": "Escolha a peça com promote Q, R, B ou N.",
    "CONFIRM UnreliableInput": "Aguarde os sensores. Confira a posição com status.",
    "CONFIRM NoLegalMatch": "Lance inválido. Reponha a posição anterior e refaça o lance.",
    "CONFIRM Ambiguous": "Movimento ambíguo. Reponha a posição anterior.",
    "CONFIRM PositionMismatch": "Reponha as peças na última posição aceita.",
    "ERROR SavePending": "Use save antes de iniciar outra partida.",
    "ERROR Command": "Comando desconhecido. Digite ajuda para ver as opções.",
    "ERROR PlayerName": "Use até 24 letras, números, espaços, pontos, apóstrofos ou hífens.",
    "CONFIRM GameFinished": "A partida terminou. Use new para começar outra.",
    "CONFIRM ApplyFailed": "Falha ao aplicar o lance. Consulte o diagnóstico com tecnico.",
}

CYAN, GREEN, YELLOW, RED = 96, 92, 93, 91


@lru_cache(maxsize=4)
def console_colors(stream):
    if not stream.isatty() or os.environ.get("NO_COLOR") or os.environ.get("TERM") == "dumb":
        return False
    if os.name != "nt":
        return True
    import ctypes
    import msvcrt
    from ctypes import wintypes
    handle = wintypes.HANDLE(msvcrt.get_osfhandle(stream.fileno()))
    mode = wintypes.DWORD()
    kernel = ctypes.windll.kernel32
    if not kernel.GetConsoleMode(handle, ctypes.byref(mode)):
        return False
    return bool(kernel.SetConsoleMode(handle, mode.value | 0x0004))  # ENABLE_VIRTUAL_TERMINAL_PROCESSING


def colored(text, color, stream=None):
    if color and console_colors(stream if stream is not None else sys.stdout):
        return f"\033[{color}m{text}\033[0m"
    return text


def response_color(line):
    if line.startswith("ERROR ") or line.startswith(("CONFIRM NoLegalMatch", "CONFIRM ApplyFailed")):
        return RED
    if line.startswith("STATE ") and "fault=Ok" not in line:
        return RED
    if line.startswith(("OK ", "CONFIRM Accepted")):
        return GREEN
    if (line.startswith(("CONFIRM ", "STATE Awaiting")) or line.endswith(("offline", "available=0"))
            or " check=1 " in line or " reliable=0 " in line):
        return YELLOW
    if line.startswith(("STATE ", "OCC ")):
        return CYAN
    if line.startswith("ENDING ") and not line.startswith("ENDING None "):
        return GREEN
    if line.startswith(("OLED ", "OLED_TABULEIRO ")):
        return GREEN
    return None


def show_help():
    title, *commands = HELP.splitlines()
    print(colored(title, CYAN))
    for line in commands:
        command, gap, description = line.strip().partition("  ")
        print("  " + colored(command, CYAN) + gap + description)


def show_squares(occupancy, setup=False):
    fields = dict(item.split("=") for item in occupancy.split())
    if fields["available"] != "1":
        print(colored("Sensores indisponíveis. Confira o tabuleiro e consulte a posição novamente.", YELLOW))
        return
    expected, observed = (int(fields[key], 16) for key in ("official", "observed"))
    if setup:
        print(f"Montagem      {bin(expected & observed).count('1')} de {bin(expected).count('1')} casas na posição")
    removed = "Falta ocupar" if setup else "Casas liberadas"
    for label, mask in ((removed, expected & ~observed), ("Casas ocupadas a mais", observed & ~expected)):
        if mask:
            squares = " ".join(f"{'ABCDEFGH'[i % 8]}{1 + i // 8}" for i in range(64) if mask & (1 << i))
            print(colored(f"{label}: {squares}", CYAN))


def show_position(lines, notice=""):
    sections = dict(line.split(" ", 1) for line in lines if " " in line)
    state, _, details = sections["STATE"].partition(" ")
    flags = dict(item.split("=") for item in details.split())
    io = dict(item.split("=") for item in sections["IO"].split())
    position, turn, *_, number = sections["FEN"].split()
    cpu = sections.get("MODE") == "cpu"
    move = sections.get("CPU", "").upper()
    side = "Brancas" if turn == "w" else "Pretas"
    player = sections.get("WHITE_NAME" if turn == "w" else "BLACK_NAME", "")
    if turn == "b" and cpu:
        player = "Máquina"
    titles = {"Tracking": f"Vez das {side.lower()}", "AwaitingPosition": "Monte a posição",
              "AwaitingPromotion": "Escolha a promoção", "Finished": "Partida encerrada"}
    if console_colors(sys.stdout):
        print("\033[2J\033[H", end="")
    print(colored("XADREZ", CYAN) + "   /   " + ("Contra a máquina" if cpu else "Dois jogadores"))
    print()
    rail = [colored(titles.get(state, state) + (" · " + player if player and state == "Tracking" else ""),
                    YELLOW if io["check"] == "1" else CYAN),
            f"Jogada {number}   ·   Último lance: {io['last']}", "",
            "PRÓXIMA AÇÃO", "Jogue no tabuleiro."]
    if state == "Finished":
        reason, result = sections["ENDING"].split()
        endings = {"Manual": "Encerramento manual", "Checkmate": "Xeque-mate", "Stalemate": "Afogamento",
                   "InsufficientMaterial": "Material insuficiente", "Resignation": "Abandono"}
        rail[3:] = [endings.get(reason, reason), result.removeprefix("result=")]
    elif state == "AwaitingPosition":
        rail[4] = "Reponha as peças indicadas abaixo."
    elif state == "AwaitingPromotion":
        rail[4] = "Digite promote Q, R, B ou N."
    elif move:
        rail[3:] = ["RESPOSTA DA MÁQUINA", colored(move.replace(">", " → "), YELLOW),
                    f"[ ] Origem: {move[:2]}   ( ) Destino: {move[3:5]}", "Mova a peça indicada."]
    if "ADJUST" in sections:
        if sections["ADJUST"] == "Ambiguous":
            rail[4:] = ["Captura ambígua.", "Levante e recoloque a peça no destino."]
        elif move:
            rail[-1] = "Ajuste a peça até o destino indicado."
        else:
            rail[4] = "Ajuste as peças nas casas pretendidas."
    if io["check"] == "1" and state != "Finished":
        rail.append(colored("XEQUE", YELLOW))
    pieces = str.maketrans("KQRBNPkqrbnp", "RDTBCPrdtbcp")
    for y, row in enumerate(position.split("/")):
        squares = "".join("." * int(c) if c.isdigit() else c.translate(pieces) for c in row)
        cells = []
        for x, piece in enumerate(squares):
            coord = f"{'ABCDEFGH'[x]}{8-y}"
            if coord == move[:2]:
                cells.append(colored(f"[{piece}]", "97;48;5;25"))
            elif coord == move[3:5]:
                cells.append(colored(f"({piece if piece != '.' else '+'})", "30;48;5;221"))
            else:
                bg = 238 if (x + y) % 2 else 242
                fg = 37 if piece == "." else (97 if piece.isupper() else 93)
                cells.append(colored(f" {piece} ", f"{fg};48;5;{bg}"))
        print(f"{8-y}  " + "".join(cells) + "    " + (rail[y] if y < len(rail) else ""))
    print("    A  B  C  D  E  F  G  H")
    print("   Brancas em maiúsculas · Pretas em minúsculas")
    if notice:
        print(colored(notice, YELLOW if notice != HINTS["CONFIRM Accepted"] else GREEN))
    if flags["fault"] != "Ok":
        print(colored(f"Falha de registro: {flags['fault']}", RED))
    if flags["interrupted"] == "1" or flags["overflow"] == "1":
        print(colored("Leitura interrompida. Reponha a última posição aceita.", YELLOW))
    show_squares(sections["OCC"], setup=state == "AwaitingPosition")
    offline = [label for key, label in (("OLED", "partida"), ("OLED_TABULEIRO", "animação"))
               if sections.get(key, "").endswith("offline")]
    if offline:
        print(colored("Tela indisponível: " + ", ".join(offline), YELLOW))


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
        print(colored("Bloco PGN incompleto ou sessão alterada. Consulte status e tente save.", RED))
        return False
    data = ("\n".join(block[1:-1]) + "\n").encode("utf-8")
    if len(data) != int(length) or zlib.crc32(data) != int(checksum):
        print(colored("Tamanho ou CRC do PGN divergente. Use save para repetir a transferência.", RED))
        return False
    try:
        path = archive(directory, game_id, data)
    except OSError as exc:
        print(colored(f"Falha ao salvar: {exc}\nPGN pendente. Corrija a pasta e envie save.", RED))
        return False
    print(colored(f"PGN gravado em {path}", GREEN))
    if send("saved " + game_id) != ["OK saved"]:
        raise ValueError("O arquivo foi gravado, mas a placa não confirmou a sessão.")
    return True


def live_command(refresh):
    """Poll the board while the interactive console waits for a command."""
    prompt = colored("xadrez> ", CYAN)
    typed = ""
    print(prompt, end="", flush=True)
    next_refresh = time.monotonic() + 0.4
    while True:
        if os.name == "nt":
            import msvcrt
            if msvcrt.kbhit():
                key = msvcrt.getwch()
                if key in ("\x00", "\xe0"):
                    msvcrt.getwch()
                elif key == "\r":
                    print()
                    return typed
                elif key == "\x1a" and not typed:
                    raise EOFError
                elif key == "\x03":
                    raise KeyboardInterrupt
                elif key == "\b" and typed:
                    typed = typed[:-1]
                    print("\b \b", end="", flush=True)
                elif key.isprintable():
                    typed += key
                    print(key, end="", flush=True)
            else:
                time.sleep(0.03)
        else:
            import select
            if select.select([sys.stdin], [], [], 0.1)[0]:
                return input()
        if time.monotonic() >= next_refresh:
            if refresh():
                print(prompt + typed, end="", flush=True)
            next_refresh = time.monotonic() + 0.4


def interact(writer, reader, args):
    def send(command):
        return request(writer, reader, command)

    if "PROTOCOL 2" not in send("hello"):
        raise ValueError("O dispositivo não respondeu com o protocolo 2 do tabuleiro.")
    directory = args.destino.resolve()
    interactive = sys.stdin.isatty() and sys.stdout.isatty()
    print(colored("TABULEIRO DE XADREZ", CYAN), " · ", args.port or "Simulador", sep="")
    if args.sim:
        print("Simulação: sync monta a posição inicial. Experimente lift e2 e place e4.")
    print(f"Pasta dos PGNs: {directory}")
    pending = False
    last_panel = None

    def execute(command, raw=False, save_offered=True, notice="", changes_only=False):
        nonlocal pending, last_panel
        lines = send(command)
        panel = interactive and command == "status" and not raw and any(line.startswith("STATE ") for line in lines)
        if panel:
            # Ignore blinking indicators and intermediate lifts while a move is in flight.
            key = tuple(line.split(" frames=")[0] for line in lines
                        if line.startswith(("STATE ", "FEN ", "CPU ", "MODE ", "WHITE_NAME ", "BLACK_NAME ", "OLED", "ADJUST ")))
            if any(line.startswith(("STATE AwaitingPosition", "ADJUST ")) for line in lines):
                key += tuple(line for line in lines if line.startswith("OCC "))
            if changes_only and key == last_panel:
                return []
            last_panel = key
            show_position(lines, notice)
            print("[1] Posição   [3] PGN   [4] Máquina   [5] Dois jogadores   [6] Nomes   [?] Ajuda")
        for line in lines:
            if interactive and command == "pgn" and line in ("PGN_BEGIN", "PGN_END"):
                if line == "PGN_BEGIN":
                    print(colored("Registro da partida (PGN)", CYAN))
                continue
            if line.startswith("SAVE_PENDING "):
                if save_offered:
                    pending = not save_pending(send, line, directory)
            elif not panel:
                color = response_color(line)
                if not interactive or line not in HINTS:
                    print(colored(line, color))
                if line in HINTS:
                    print(colored(HINTS[line], color))
                if not interactive and line.startswith("OCC ") and line.endswith("available=1"):
                    show_squares(line[4:])
        return lines

    def ask_names():
        print("Quem vai jogar? Pressione Enter para usar Brancas ou Pretas.")
        for side, verb in (("Brancas", "white_name"), ("Pretas", "black_name")):
            while True:
                try:
                    name = input(f"{side}> ").strip()
                except EOFError:
                    return
                name = "".join(ch for ch in unicodedata.normalize("NFD", name) if not unicodedata.combining(ch))
                if re.fullmatch(r"[A-Za-z0-9 .'-]{0,24}", name):
                    reply = execute((verb + " " + name).rstrip())
                    if reply != ["OK name"]:
                        return
                    break
                print(colored(HINTS["ERROR PlayerName"], YELLOW))

    initial = execute("status")  # Também recupera PGNs pendentes.
    fresh = any(line.startswith("STATE ") and " plies=0 " in line and " record_finished=0 " in line for line in initial)
    if interactive and fresh and "MODE cpu" not in initial and not any(
            line.startswith(("WHITE_NAME ", "BLACK_NAME ")) for line in initial):
        try:
            mode = input("Começar: [Enter] Dois jogadores   [4] Máquina > ").strip()
            while mode not in ("", "5", "4"):
                mode = input("Use Enter ou 5 para dois jogadores, ou 4 para máquina > ").strip()
        except EOFError:
            return 0
        if mode == "4":
            execute("mode cpu")
        else:
            ask_names()
        execute("status")
    while True:
        try:
            if interactive and args.port:
                command = live_command(lambda: execute("status", changes_only=True, save_offered=not pending))
            else:
                command = input(colored("xadrez> ", CYAN))
            command = command.strip()
        except EOFError:
            break
        command = {"1": "status", "3": "pgn", "4": "mode cpu", "5": "mode local",
                   "maquina": "mode cpu", "máquina": "mode cpu", "local": "mode local",
                   "6": "nomes", "?": "ajuda"}.get(command, command)
        if command in ("sair", "exit", "quit"):
            break
        if command in ("ajuda", "help"):
            show_help()
            if args.sim or not interactive:
                execute("help")
        elif command == "nomes":
            ask_names()
            execute("status")
        elif command == "tecnico":
            execute("status", raw=True)
        elif command.startswith("pasta "):
            directory = Path(command[6:].strip()).expanduser().resolve()
            print(f"Pasta dos PGNs: {directory}. Use save para repetir.")
        elif command.split(" ", 1)[0] == "saved":
            print("A confirmação de gravação é enviada automaticamente.")
        elif command and len(command) <= 96 and command.isascii() and command.isprintable():
            reply = execute(command)
            sim_motion = args.sim and command.split()[0] in ("lift", "place", "observe", "promote")
            if sim_motion and reply and reply[0].startswith("OK"):
                reply += execute("wait 500")
            if interactive and reply and reply[0].startswith("OK") and (
                    command == "mode local" or (command == "new" and "MODE cpu" not in send("status"))):
                ask_names()
            if interactive and (sim_motion or command.startswith(("wait ", "mode ", "promote ")) or command == "new"):
                # Keep save receipts and failures visible when the game ends.
                saved_now = any(line.startswith("SAVE_PENDING ") for line in reply)
                if not saved_now or not pending:
                    note = HINTS.get(reply[0], reply[0]) if reply else ""
                    if saved_now:
                        game_id = next(line.split()[1] for line in reply if line.startswith("SAVE_PENDING "))
                        note = f"PGN gravado em {directory / ('partida-' + game_id + '.pgn')}"
                    elif reply and reply[0].startswith("OK"):
                        note = ""
                    execute("status", save_offered=not pending, notice=note)
        else:
            print(colored("Digite um comando ASCII com até 96 caracteres. Use ajuda.", YELLOW))

    if pending:
        message = ("PGN pendente de gravação. A sessão em RAM depende da alimentação do ESP32."
                   if args.port else "O simulador terminou com um PGN pendente.")
        print(colored(message, YELLOW, sys.stderr), file=sys.stderr)
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
        print(colored(f"Terminal encerrado: {exc}", RED, sys.stderr), file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    sys.exit(main())
