# Arquitetura do Sistema

## 1. Arquitetura eletrônica

```mermaid
flowchart LR
    P["Peças com ímãs"] -->|Acionamento magnético| M["Matriz 8×8<br/>64 Reed Switches + 64 diodos 1N4148"]
    M <-->|8 colunas e 8 linhas| X["MCP23017"]
    X <-->|I²C| E["ESP32"]
    B["Botões"] -->|Comandos| E
    E -->|Sinalização| S["LEDs e buzzer"]
    E -->|SPI| SD["Adaptador SD + cartão microSD"]
    E -.->|I²C — opcional| O["OLED SSD1315"]
    U["Alimentação USB"] --> E
```

O MCP23017 seleciona sequencialmente as colunas e lê as linhas da matriz. O OLED, caso utilizado, compartilhará o barramento I²C. A pinagem definitiva será estabelecida durante a implementação.

## 2. Organização do firmware

O firmware será desenvolvido em C/C++ no ecossistema Arduino. Seus módulos e as relações entre eles são representados no diagrama de componentes:

![Diagrama de componentes do firmware](diagramas/componentes.svg)

Os diagramas também estão disponíveis no
[projeto Astah editável](diagramas/tabuleiro-xadrez-sensorial.asta).

## 3. Estados e processamento das jogadas

A interpretação das jogadas utiliza três informações:

- **Posição lógica:** estado oficial da partida, mantido pela biblioteca de xadrez.
- **Ocupação física:** indicação instantânea de casa ocupada ou vazia.
- **Alterações da jogada:** sequência de transições detectadas até a confirmação.

Os Reed Switches não identificam as peças. Em uma captura, por exemplo, a casa de destino pode apresentar a sequência **ocupada → vazia → ocupada**, tornando necessário considerar as alterações intermediárias.

![Diagrama de atividades do processamento das jogadas](diagramas/atividades.svg)
