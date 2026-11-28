# Tabuleiro de Xadrez Sensorial

Projeto de Engenharia de Computação da UTFPR — Câmpus Cornélio Procópio, disciplina Certificadora da Competência 2. Um tabuleiro 8×8 com reeds e ímãs acompanha as peças, valida jogadas e registra partidas em PGN.

O ESP32 acompanha a ocupação das casas. Você conclui o movimento no tabuleiro e pressiona **Enter no terminal USB**. A biblioteca THC valida o lance e o computador salva a partida em PGN. Se o lance for inválido ou ambíguo, o terminal mostra a posição a restaurar e as casas divergentes. Dois OLEDs lado a lado acompanham a partida com instruções e animações.

## Jogar no protótipo

1. Conecte a DevKit V1 de 30 pinos por USB de dados. [Compile e grave o firmware](docs/firmware.md#compilar-e-gravar-o-esp32).
2. Feche o monitor serial e abra `python firmware/terminal.py --port COM7` com a porta da placa. O ambiente PlatformIO inclui pySerial. Em outro Python, instale `pyserial==3.5`.
3. Na abertura, Enter seleciona **dois jogadores**, 4 seleciona **máquina** e 2 abre os **tutoriais**. Monte as peças conforme o desenho, com A1 à esquerda das brancas. O LED verde indica que a matriz está pronta.
4. Faça o gesto completo e pressione Enter em uma linha vazia. Em captura, deixe a casa da peça capturada vazia por um instante antes de pousar a sua. Em promoção, troque o peão pela peça escolhida, pressione Enter, digite `promote Q`, `promote R`, `promote B` ou `promote N`, e confirme com outro Enter. No modo máquina, execute também a resposta indicada das pretas.
5. Use `finish` para encerrar ou `resign white` / `resign black` para registrar abandono. Aguarde o caminho do arquivo antes de desligar. Se a gravação falhar, use `pasta CAMINHO` e `save` com a placa alimentada.

Em **Tutorial**, cada lição tem montagem própria, movimentos guiados e um fim. O roque exercita os dois lados do tabuleiro. A promoção pede uma torre. O en passant começa com o avanço duplo preto que cria a oportunidade de captura. Use `repetir` para remontar a mesma lição. [Roteiro de apresentação com peças e lances](docs/firmware.md#roteiro-de-apresentação).

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

`confirm` equivale ao Enter em branco. O terminal informa o caminho de um novo arquivo em `build/partidas/`, com o corpo `1. e4 *`. Use `pgn` para consultar, `new` para começar outra partida e `sair` para fechar o terminal. A opção `--destino CAMINHO` escolhe outra pasta.

## Guias do projeto

- [Arquitetura e requisitos](docs/arquitetura.md) descreve os cinco módulos de xadrez, a aplicação compartilhada e a divisão de responsabilidades entre placa e computador.
- [Firmware](docs/firmware.md) reúne os comandos, o protocolo, os cenários de simulação e o diagnóstico.
- [Hardware](docs/hardware.md) descreve a matriz de 64 reeds com pull-ups de 47 kΩ, os dois barramentos I²C, os projetos nativos e os ensaios de bancada.

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
