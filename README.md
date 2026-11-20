# Tabuleiro de Xadrez Sensorial

Projeto de Engenharia de Computação da UTFPR — Câmpus Cornélio Procópio, disciplina Certificadora da Competência 2. Um tabuleiro 8×8 com reeds e ímãs acompanha as peças, valida jogadas e registra partidas em PGN.

O firmware integra matriz MCP23017, três botões, dois LEDs, buzzer e microSD. O **simulador de terminal funciona sem montagem** e compartilha a aplicação, o filtro de sensores, os botões e a lógica de gravação com o ESP32. A integração foi verificada por software; alimentação, magnetismo, tempos e funcionamento no conjunto físico ainda precisam de ensaio.

## Experimentar sem hardware

Na raiz do repositório, com CMake 3.20+ e compilador C++17 (Visual Studio com ferramentas C++ no Windows):

```powershell
cmake -S firmware -B firmware/build
cmake --build firmware/build --config Release
.\firmware\build\Release\tabuleiro_sim.exe --sd firmware/build/sd
```

No Linux/macOS, o executável fica em `firmware/build/tabuleiro_sim`. Não são necessárias bibliotecas externas de xadrez nem Python para compilar o simulador.

Dentro do simulador, digitar uma linha de cada vez:

```text
new
sync
lift e2
place e4
press confirm
status
press end
pgn
status
```

O registro terá `1. e4 *`. Ao encerrar, o simulador grava um novo `GAME0001.PGN` (ou o próximo nome livre) em `firmware/build/sd/`; `status` informa o nome. `help` lista os comandos. Para sair, enviar EOF: Ctrl+Z e Enter no Windows, Ctrl+D no Linux/macOS.

`lift` e `place` simulam a retirada e a colocação de peças. `press` percorre o mesmo tratamento de botões usado pela placa. O simulador também permite leituras brutas, ruído e interrupções; exemplos no guia de firmware.

## Usar no ESP32

Compilar e gravar o projeto [PlatformIO](firmware/principal/platformio.ini) conforme o [guia de firmware](docs/firmware.md). A configuração `esp32` é a aplicação física: comandos de ocupação simulada são recusados. A alimentação prevista é USB do computador, com o regulador da DevKit V1 e matriz direta com pull-ups de 47 kΩ.

Posicionar as peças iniciais e aguardar o LED verde. Após mover, apertar e soltar **Confirmar**. Em promoção, **Promover** percorre dama/torre/bispo/cavalo, indicados por 1/2/3/4 pulsos verdes; confirmar novamente. **Encerrar** termina a partida; segurá-lo por pelo menos dois segundos antes de soltar registra abandono do lado que tem a vez.

O PGN é salvo automaticamente no final. Verde piscando indica gravação concluída; vermelho piscando pede verificar o SD e apertar Encerrar para tentar novamente. Depois de salvo, segurar Confirmar por dois segundos e soltar inicia outra partida. Os gestos completos e demais sinais estão no guia.

## Como ler o projeto

| Caminho | Conteúdo |
| --- | --- |
| `firmware/principal/src/BoardApp.*` | Aplicação compartilhada: aquisição filtrada, botões, confirmação, registro e sinais. |
| `firmware/principal/src/Hardware.*` | Acesso físico ao MCP23017, GPIO e cartão. |
| `firmware/principal/src/PgnStorage.h` | Gravação e verificação de um novo PGN. |
| `firmware/principal/src/main.cpp` | Inicialização e ciclo do ESP32. |
| `firmware/principal/src/SerialGame.*` | Console e entradas do simulador. |
| `firmware/simulador/` | Executável no computador; diretório local representa o cartão. |
| `firmware/board_config.h` | Pinagem e tempos ajustáveis, compartilhados com o diagnóstico. |
| `firmware/acompanhamento/`, `xadrez/`, `interpretacao/`, `controle/`, `registro/` | Cinco módulos da lógica de xadrez; THC local em `xadrez/src/thc/`. |
| `firmware/diagnostico/` | Programa independente para conferir a montagem. |
| `hardware/` | Projetos editáveis KiCad/FreeCAD e exportadores. |
| `docs/` | Três guias e fonte UML Astah. |

- [Arquitetura e requisitos](docs/arquitetura.md): responsabilidades, fluxo e critérios do produto.
- [Firmware](docs/firmware.md): compilação, operação, simulação e contratos.
- [Hardware](docs/hardware.md): componentes, pinagem, medidas, montagem e ensaios.

Builds, PGNs de simulação e exportações ficam em `build/`, ignorado pelo Git. A demonstração acima permite conferir a aplicação a partir de um clone limpo.

## Equipe

| Integrante | Responsabilidade |
| --- | --- |
| Bruno Ferreira de Lima | Testes, documentação e registro |
| Daniel Chien Chen Wang | Eletrônica e estrutura física |
| Gustavo Yudi Yamada | Algoritmos e biblioteca de xadrez |
| Pedro Paulo da Silva Oliveira | Firmware e interface com hardware |
| Tadashi Bello Nagashima | Coordenação e integração |

A [licença do projeto](LICENSE), a [licença THC](firmware/xadrez/src/thc/LICENSE) e a [licença dos símbolos KiCad](hardware/eletronica/LICENSE.KICAD) acompanham suas fontes.
