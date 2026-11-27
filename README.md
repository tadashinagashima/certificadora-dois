# Tabuleiro de Xadrez Sensorial

Projeto de Engenharia de Computação da UTFPR — Câmpus Cornélio Procópio, disciplina Certificadora da Competência 2. Um tabuleiro 8×8 com reeds e ímãs acompanha as peças, valida jogadas e registra partidas em PGN.

O ESP32 lê a matriz pelo MCP23017 e recebe os comandos de partida pelo USB do computador. O terminal mostra as respostas da placa e grava o PGN ao encerrar, inclusive em caso de mate. Um OLED I²C opcional mostra a vez, o último lance e o estado da partida. Dois LEDs e o buzzer complementam a indicação na montagem.

## Demonstração sem placa

Na raiz do repositório, com Python 3.9+, CMake 3.20+ e compilador C++14 (Visual Studio com ferramentas C++ no Windows):

```powershell
cmake -S firmware -B firmware/build
cmake --build firmware/build --config Release
python firmware/terminal.py --sim firmware/build/Release/tabuleiro_sim.exe
```

Em Linux/macOS com gerador de configuração única, use `python3 firmware/terminal.py --sim firmware/build/tabuleiro_sim`. A simulação usa a biblioteca padrão de Python. Dentro do terminal, digite uma linha de cada vez:

```text
sync
lift e2
place e4
confirm
finish
```

O terminal informa o caminho de um novo arquivo em `build/partidas/`, com o corpo `1. e4 *`. Use `pgn` para consultar o registro, `new` para iniciar outra partida e `sair` para fechar o terminal. A opção `--destino CAMINHO` escolhe outra pasta.

## Jogar no protótipo

1. Conecte a DevKit V1 de 30 pinos ao computador com um cabo USB de dados. Compile e grave o firmware seguindo o [guia de firmware](docs/firmware.md#compilar-e-gravar-o-esp32).
2. Feche o monitor serial. Com o Python do ambiente PlatformIO, execute `python firmware/terminal.py --port COM7`, substituindo a porta pela da placa. Em outro ambiente Python, instale `pyserial==3.5`.
3. Monte a posição inicial e use `status`. O estado `Tracking` indica que o tabuleiro reconheceu a ocupação. Mova a peça e envie `confirm`. Em captura, levante a peça capturada antes de ocupar o destino.
4. Se houver promoção, escolha a peça com `promote Q`, `promote R`, `promote B` ou `promote N` e confirme novamente. Use `finish` para encerrar ou `resign white` / `resign black` para registrar abandono. Aguarde a mensagem com o caminho do PGN antes de desligar.
5. Em falha de gravação, o registro permanece em RAM. Corrija o destino com `pasta CAMINHO` e envie `save`. A placa protege a partida encerrada até o computador confirmar a gravação.

## Guias do projeto

- [Arquitetura e requisitos](docs/arquitetura.md) descreve os cinco módulos de xadrez, a aplicação compartilhada e a divisão de responsabilidades entre placa e computador.
- [Firmware](docs/firmware.md) reúne os comandos, o protocolo, os cenários de simulação e o diagnóstico.
- [Hardware](docs/hardware.md) descreve a matriz de 64 reeds com pull-ups de 47 kΩ, a ligação do OLED em paralelo com o MCP, os projetos nativos e os ensaios de bancada.

Compilação, simulação e conferência dos projetos nativos são verificações por software. O funcionamento do conjunto montado ainda exige ensaio físico.

## Equipe

| Integrante | Responsabilidade |
| --- | --- |
| Bruno Ferreira de Lima | Testes, documentação e registro |
| Daniel Chien Chen Wang | Eletrônica e estrutura física |
| Gustavo Yudi Yamada | Algoritmos e biblioteca de xadrez |
| Pedro Paulo da Silva Oliveira | Firmware e interface com hardware |
| Tadashi Bello Nagashima | Coordenação e integração |

A [licença do projeto](LICENSE), a [licença THC](firmware/xadrez/src/thc/LICENSE) e a [licença dos símbolos KiCad](hardware/eletronica/LICENSE.KICAD) acompanham suas fontes.
