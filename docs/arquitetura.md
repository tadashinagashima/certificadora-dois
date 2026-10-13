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
    U["USB do computador"] --> E
```

O MCP23017 seleciona uma coluna LOW por vez, mantendo as demais em alta impedância, e lê diretamente as linhas com pull-ups de 47 kΩ. A DevKit V1 de 30 pinos fornece 3,3 V aos periféricos pelo regulador da própria placa. O OLED é opcional e compartilha o I²C quando instalado. A [documentação elétrica](eletronica.md) define a pinagem e as conexões adotadas; [board_config.h](../firmware/diagnostico/board_config.h) mantém a referência no firmware.

## 2. Organização do firmware

O firmware será desenvolvido em C/C++ no ecossistema Arduino. Seus módulos e as relações entre eles são representados no diagrama de componentes:

![Diagrama de componentes do firmware](diagramas/componentes.svg)

Os diagramas também estão disponíveis no
[projeto Astah editável](diagramas/tabuleiro-xadrez-sensorial.asta).

O [acompanhamento da jogada](firmware-acompanhamento.md) está implementado como módulo C++ independente. Recebe observações do Controle e fornece uma cópia da tentativa, mantendo as dependências representadas no diagrama. O guia do módulo define o contrato, os limites do histórico e as verificações automatizadas; sua integração com o sensoriamento permanece pendente.

O [adaptador THC](firmware-xadrez.md) implementa a responsabilidade da biblioteca de xadrez: posição lógica, consulta de movimentos legais, aplicação validada, SAN e condições de encerramento. Também é C++ independente, sem ligação com o acompanhamento ou periféricos. Interpretação, confirmação e política de encerramento permanecem atribuídas aos módulos futuros; os diagramas continuam aplicáveis.

## 3. Estados e processamento das jogadas

A interpretação das jogadas utiliza três informações:

- **Posição lógica:** estado oficial da partida, mantido pela biblioteca de xadrez.
- **Ocupação física:** indicação instantânea de casa ocupada ou vazia.
- **Alterações da jogada:** sequência de transições detectadas até a confirmação.

Os Reed Switches não identificam as peças. Em uma captura, por exemplo, a casa de destino pode apresentar a sequência **ocupada → vazia → ocupada**, tornando necessário considerar as alterações intermediárias.

![Diagrama de atividades do processamento das jogadas](diagramas/atividades.svg)
