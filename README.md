# Tabuleiro de Xadrez Sensorial

Projeto de Engenharia de Computação da UTFPR, Câmpus Cornélio Procópio, para a Certificadora da Competência 2. Mova as peças no tabuleiro, pressione **Enter no terminal USB** e acompanhe o lance nos dois OLEDs. O ESP32 interpreta as leituras dos 64 reeds com as regras da THC. O computador grava a partida em PGN.

![Os dois OLEDs durante uma partida, da vez das brancas à captura em D5](docs/preview/partida.gif)

## Jogar no protótipo

1. Conecte a DevKit V1 de 30 pinos por USB de dados. [Compile e grave o firmware](docs/firmware.md#compilar-e-gravar-o-esp32).
2. Feche o monitor serial e abra `python firmware/terminal.py --port COM7` com a porta da placa. O ambiente PlatformIO inclui pySerial. Em outro Python, instale `pyserial==3.5`.
3. Na abertura, Enter seleciona **dois jogadores**, 4 seleciona **máquina** e 2 abre os **tutoriais**. Monte as peças conforme o desenho, com A1 à esquerda das brancas. O LED verde indica que a matriz está pronta.
4. Faça o gesto completo e pressione Enter em uma linha vazia. No modo máquina, execute também a resposta indicada das pretas. Na promoção, escolha `promote Q`, `promote R`, `promote B` ou `promote N` após o primeiro Enter e confirme a escolha com outro Enter.
5. Use `finish` para encerrar ou `resign white` / `resign black` para registrar abandono. Aguarde o caminho do PGN antes de desligar. Se a gravação falhar, mantenha a placa ligada e use `pasta CAMINHO` e `save`.

**Confira a captura** pede que você levante e recoloque apenas a peça que capturou e pressione Enter. **Reponha a posição** pede que você monte a última posição aceita. O terminal desenha essa posição e indica quais casas corrigir.

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

O [procedimento de bancada](docs/hardware.md#conferência-antes-de-fabricar-e-usar) cobre acionamento dos reeds, capturas rápidas, recuperação dos OLEDs e medição da fluidez nas duas frequências.

## Equipe

| Integrante | Responsabilidade |
| --- | --- |
| Bruno Ferreira de Lima | Testes, documentação e registro |
| Daniel Chien Chen Wang | Eletrônica e estrutura física |
| Gustavo Yudi Yamada | Algoritmos e biblioteca de xadrez |
| Pedro Paulo da Silva Oliveira | Firmware e interface com hardware |
| Tadashi Bello Nagashima | Coordenação e integração |

A [licença do projeto](LICENSE), a [licença THC](firmware/xadrez/src/thc/LICENSE) e a [licença dos símbolos KiCad](hardware/eletronica/LICENSE.KICAD) acompanham suas fontes.
