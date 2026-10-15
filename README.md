# Tabuleiro de Xadrez Sensorial

Projeto de Engenharia de Computação da UTFPR — Câmpus Cornélio Procópio, disciplina Certificadora da Competência 2. O tabuleiro 8×8 usa reeds e ímãs para acompanhar peças, validar jogadas e produzir PGN.

O firmware atual executa partidas completas por entradas simuladas na Serial do ESP32. Há também um diagnóstico de matriz, botões, indicadores e SD. Aquisição física integrada, interface de partida e persistência PGN em SD ainda estão pendentes; os projetos de montagem precisam ser conferidos com os componentes reais.

## Documentação

- [Arquitetura e requisitos](docs/arquitetura.md): responsabilidades, fluxo e critérios do produto.
- [Firmware](docs/firmware.md): compilação, protocolo Serial, contratos e testes.
- [Hardware](docs/hardware.md): componentes adotados, pinagem, dimensões, edição e montagem.

## Estrutura

```text
docs/                   Arquitetura, firmware, hardware e fonte UML Astah
firmware/CMakeLists.txt Build nativo e testes dos cinco módulos
firmware/principal/     Aplicação PlatformIO e testes de integração
firmware/diagnostico/   Diagnóstico Arduino e pinagem
firmware/acompanhamento/ MoveTracker
firmware/xadrez/        ChessAdapter e única cópia da THC (licença MIT)
firmware/interpretacao/ MoveInterpreter
firmware/controle/      GameController
firmware/registro/      GameRecord
hardware/eletronica/    Projeto KiCad, biblioteca e exportação/verificação
hardware/mecanica/      Modelo FreeCAD e exportação/verificação
```

## Começar

Usar ESP32 clássico, Arduino-ESP32 **3.3.12**, PlatformIO **6.2.0** e a plataforma fixada no [platformio.ini](firmware/principal/platformio.ini). Seguir a preparação do [guia de firmware](docs/firmware.md); na Serial, `help` lista os comandos. `new`, `sync`, `lift e2`, `place e4`, `confirm` simulam o primeiro lance. Cada comando deve aguardar a resposta `DONE`. `pgn` exporta a partida em RAM.

Para manutenção, executar os testes de contratos e integração pelo CMake/CTest, conforme o guia de firmware. Para montar, abrir KiCad/FreeCAD e seguir as verificações do guia de hardware. Builds, caches e saídas geradas ficam em `build/` e são ignorados; não se armazenam relatórios históricos ou cópias de sondas.

## Equipe

| Integrante | Responsabilidade |
| --- | --- |
| Bruno Ferreira de Lima | Testes, documentação e registro |
| Daniel Chien Chen Wang | Eletrônica e estrutura física |
| Gustavo Yudi Yamada | Algoritmos e biblioteca de xadrez |
| Pedro Paulo da Silva Oliveira | Firmware e interface com hardware |
| Tadashi Bello Nagashima | Coordenação e integração |

A [licença do projeto](LICENSE), a [licença THC](firmware/xadrez/src/thc/LICENSE) e a [licença dos símbolos KiCad](hardware/eletronica/LICENSE.KICAD) acompanham suas fontes.
