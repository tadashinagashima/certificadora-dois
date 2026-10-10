# Tabuleiro de Xadrez Sensorial

Projeto da disciplina **Certificadora da Competência 2**, do curso de **Engenharia de Computação da UTFPR — Câmpus Cornélio Procópio**.

## Objetivo

Desenvolver um tabuleiro de xadrez 8×8 capaz de detectar movimentações físicas, validar jogadas e registrar automaticamente partidas em formato **PGN**, permitindo sua reprodução e análise posterior pelos participantes do Clube de Xadrez Estudantil.

## Funcionamento

As peças possuem ímãs na base, e cada casa do tabuleiro conta com um **Reed Switch** para detectar sua ocupação. O **ESP32**, por meio do expansor **MCP23017**, acompanha as alterações dos sensores até o jogador confirmar a jogada.

O firmware interpreta essas alterações em conjunto com o estado lógico da partida e utiliza uma biblioteca de xadrez para validar o movimento. Jogadas inválidas são sinalizadas; jogadas válidas atualizam a partida e são registradas em **PGN** no cartão microSD.

## Tecnologias e componentes

- **ESP32:** controle principal, com firmware em C/C++ (Arduino).
- **64 Reed Switches e 64 diodos 1N4148:** matriz de sensoriamento 8×8.
- **MCP23017 (I²C):** leitura da matriz.
- **Ímãs de ferrite:** instalados na base das peças.
- **MicroSD (SPI):** armazenamento das partidas.
- **Botões, LEDs e buzzer:** confirmação de jogadas, promoção, encerramento e sinalização.
- **OLED SSD1315 (opcional):** exibição de informações da partida.

A estrutura física utilizará uma base de **MDF** e um **tabuleiro de vidro removível de 3 mm**, com dimensões externas aproximadas de 35 × 35 cm.

## Equipe

| Integrante | Responsabilidade |
| --- | --- |
| Bruno Ferreira de Lima | Testes, documentação e registro da partida |
| Daniel Chien Chen Wang | Eletrônica e estrutura física |
| Gustavo Yudi Yamada | Algoritmos e biblioteca de xadrez |
| Pedro Paulo da Silva Oliveira | Firmware e interface com hardware |
| Tadashi Bello Nagashima | Coordenação e integração |
