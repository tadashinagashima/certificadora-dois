# Hardware

As fontes editáveis são o [projeto KiCad 10](../hardware/eletronica/tabuleiro-sensorial.kicad_pro) e o [modelo FreeCAD](../hardware/mecanica/tabuleiro_sensorial.FCStd). Manter folhas, biblioteca local e `sym-lib-table` juntos. Os esquemas têm três folhas: controle/alimentação, periféricos e matriz. A fonte da geometria é o FCStd paramétrico; editar no FreeCAD, sem regeneradores que sobrescrevam alterações manuais.

## Eletrônica adotada

A montagem usa uma DevKit V1 com ESP-WROOM-32, módulo CJMCU-2317 com MCP23017-E/SS, 64 reeds normalmente abertos, 64 diodos 1N4148 PTH, dois LEDs, buzzer e OLED I²C opcional. O computador alimenta o protótipo, controla a partida por USB serial e recebe o PGN.

O pino **3V3/R15** alimenta a rede `3V3_IO`; GND é comum. **VIN/L15 fica desconectado.** Não usar fonte externa nem aplicar 5 V ao MCP, OLED ou sinais. Wi-Fi e Bluetooth não são necessários e não são inicializados pelo diagnóstico. O regulador marcado `1117C 33 D405` pertence à placa; sua capacidade útil será conferida no teste com o computador e cabo da demonstração.

Não duplicar passivos já existentes nos módulos. A BOM pode ser exportada do projeto KiCad conforme instruções ao final deste guia.

### Pinagem da DevKit V1

A1 usa identificadores **L1…L15 e R1…R15**, definidos neste projeto de cima para baixo: componentes voltados para o observador, antena em cima e USB embaixo. Não são números do módulo WROOM. A equipe confirmou EN/L1, VIN/L15, GPIO23/R1 e 3V3/R15. O [esquema original DOIT de 30 pinos, preservado em espelho](https://embedded-systems-design.github.io/overview-of-the-esp32-devkit-doit-v1/SchematicsforESP32.pdf), fundamenta a sequência; conferir a serigrafia ao conectar.

| Função | GPIO | Posição em A1 | Configuração |
| --- | --- | --- | --- |
| SDA / SCL | 21 / 22 | R5 / R2 | I²C a 100 kHz, pull-ups a 3,3 V |
| LED verde / vermelho | 25 / 26 | L8 / L9 | Ativos HIGH, resistores de 1 kΩ |
| Buzzer | 27 | L10 | Ativo HIGH, estágio NPN |
| RESET MCP | 14 | L11 | Open-drain, pull-up externo de 10 kΩ |
| Alimentação dos periféricos | — | R15 | Saída 3V3 da placa |
| Terra | — | L14 / R14 | GND comum |

A UART0 atende a comunicação pelo conversor USB da DevKit. A referência no firmware está em [board_config.h](../firmware/board_config.h).

### MCP23017 e matriz direta

U1 representa o **CI SSOP28**, não a ordem dos headers do CJMCU-2317. Conferir VCC, GND, SDA, SCL, RESET, A0–A2 e portas por serigrafia/continuidade. Alimentar em 3,3 V; A0/A1/A2 em GND definem `0x20`. Verificar os pull-ups I²C já presentes e sua ligação a 3,3 V; R1/R2 de 4,7 kΩ representam a necessidade elétrica, não resistores extras obrigatórios. C4 de 100 nF fica próximo ao MCP, se não houver desacoplamento equivalente no módulo.

| Barramento | Porta MCP | Pino do CI |
| --- | --- | --- |
| COL_a…COL_f | GPA0…GPA5 | 21…26 |
| COL_g / COL_h | GPA7 / GPB7 | 28 / 8 |
| ROW_1…ROW_7 | GPB0…GPB6 | 1…7 |
| ROW_8 | GPA6 | 27 |

Cada casa conecta `ROW → anodo 1N4148 → catodo → reed → COL`. A faixa do diodo aponta para o reed/coluna. SW1/D1=a1, SW8/D8=h1, SW57/D57=a8 e SW64/D64=h8.

**R16…R23 são oito pull-ups de 47 kΩ**, um entre cada ROW e 3V3_IO; a equipe confirmou seu estoque. As linhas chegam diretamente ao MCP e ficam LOW quando a casa da coluna selecionada está ocupada. Não adicionar pull-ups de 10 kΩ em paralelo.

A varredura mantém `OLATA=OLATB=0` e `GPPU=0`, libera a coluna anterior e seleciona apenas uma saída LOW; as demais ficam em alta impedância. GPA7/GPB7 são usados somente como colunas, conforme o [datasheet MCP23017, revisão D](https://ww1.microchip.com/downloads/aemDocuments/documents/APID/ProductDocuments/DataSheets/MCP23017-Data-Sheet-DS20001952.pdf). O diagnóstico espera inicialmente **300 µs** antes da leitura, a conferir com a fiação real.

### Periféricos

#### OLED de 0,96"

O módulo disponível foi anunciado como SSD1315 (`NFP1315-157BY`). O firmware adota 128×64 pixels e o perfil SSD1315 NONAME da biblioteca U8g2 2.36.18 em modo U8x8. Confira controlador e resolução no módulo adquirido. Um ACK indica que há um dispositivo no endereço; letras legíveis na tela confirmam a configuração do painel. [Driver SSD1315 da U8g2](https://github.com/olikraus/u8g2/wiki/u8x8setupcpp)

| Serigrafia do OLED | Ligação em J2 |
| --- | --- |
| GND | 1 — GND comum |
| VCC | 2 — 3V3_IO |
| SCL | 3 — GPIO22/R2, mesma rede SCL do MCP |
| SDA | 4 — GPIO21/R5, mesma rede SDA do MCP |

Confira os nomes dos quatro pinos antes de ligar. O endereço de 7 bits padrão é `0x3C`; para módulo configurado em `0x3D`, altere `OLED_ADDRESS` em [board_config.h](../firmware/board_config.h) e recompile. O MCP usa `0x20`. Mantenha o barramento a 100 kHz e verifique a resistência equivalente dos pull-ups dos dois módulos e sua ligação a 3,3 V. A biblioteca usa o Wire já inicializado, sem pino de reset dedicado. [Datasheet SSD1315](https://files.waveshare.com/upload/f/f0/SSD1315_1.1.pdf)

O firmware verifica o endereço no boot. Um display desconectado ou que passe a responder com erro sai do ciclo de atualização. Para ensaiar novamente, desligue, confira a fiação e religue após salvar a partida. Um curto que prenda SDA ou SCL afeta fisicamente ambos os dispositivos; a matriz sinaliza perda de aquisição e tenta se recuperar.

#### LEDs e buzzer

O buzzer previsto é ativo, 3,3 V, até 30 mA. Q1=2N3904 usa pino 1=emissor/GND, 2=base, 3=coletor; resistor de base de 680 Ω, resistor base-emissor de 100 kΩ e diodo flyback. Conferir tipo, consumo e pinagem dos componentes reaproveitados. Um NPN equivalente só pode ser usado com a ligação ajustada ao seu datasheet; não alimentar o buzzer diretamente pelo GPIO.

## Mecânica e montagem

A planilha `Parametros`, coluna B, controla o modelo; preservar aliases e fórmulas ao recomputar. Usa operações nativas de Part, sem complementos. Os valores abaixo são os parâmetros atuais do protótipo, sujeitos às medidas reais antes de fabricar:

| Parâmetro / elemento | Valor |
| --- | --- |
| `Outer` / `Game` / `Pitch` / `Margin` | 350 / 300 / 37,5 / 25 mm |
| Vidro removível (`GlassNominal` / `Glass`) | 350 × 350 × 3 mm; medir o vidro disponível |
| Altura / pés | 60 / 3 mm |
| MDF fundo / paredes / suporte | 6 / 9 / 6 mm |
| Junta (`Gasket` / `GasketWidth`) | 1 mm comprimida / 10 mm de largura |
| Reed vertical (`ReedDiameter` / `ReedLength`) | Envelope Ø2 × 14 mm |
| Furo / folga ao vidro (`ReedHole` / `SensorGap`) | Ø2,8 mm / 3 mm do topo do corpo |
| Terminais acima / abaixo | 1,5 / 5 mm retos ilustrativos |
| Canais de fios | 3 mm de largura × 2 mm de profundidade |
| Ímã de referência | Ferrite Ø12 × 3 mm |
| Apoios / passagem de parafuso / piloto | 12 × 12 mm / Ø3,4 mm / Ø2,5 mm |
| LEDs | Dois furos Ø5 mm na frente |

A frente (brancas) fica no menor Y. Centros: `X=Margin+(coluna+0,5)×Pitch`, `Y=Margin+(linha+0,5)×Pitch`; a1=(43,75;43,75), h8=(306,25;306,25) mm. Não espelhar casas ao montar pela face inferior.

O fundo ocupa Z=0…6; suporte Z=50…56; junta Z=56…57; vidro Z=57…60. O reed ocupa Z=40…54, centro Z=47: 10 mm abaixo da face inferior do vidro. O ímã ilustrado ocupa Z=60,2…63,2; seu centro fica 14,7 mm acima do centro do reed. Essas cotas não representam alcance magnético validado; feltro e embutimento mudam a distância real.

Fundo e suporte medem 350×350×6 mm, com quatro escareados de 90°, Ø6,8×1,7 mm para furos Ø3,4. Paredes frontal/traseira: 350×44×9 mm; laterais: 332×44×9 mm. Travessa central: 332×12×9 mm entre d/e. Apoios de canto possuem pilotos de 10 mm; selecionar fixação e comprimento dos parafusos conforme o MDF.

O suporte tem 64 furos, canais, coletores e passagem de chicote 8×28 mm. Vidro apoiado em junta periférica externa 346×346 mm, com duas pegas laterais R10. A frente recebe os dois LEDs. A abertura lateral de 70×20 mm dá acesso ao USB. Conferir bordas lapidadas, apoio, remoção e retenção no transporte. Fios, soldas, cruzamentos e terminais dobrados não estão modelados; manter redes distintas isoladas e abaixo do apoio do vidro.

Reservas `REF` são envelopes, não desenhos de fabricante: ESP32/USB 58×30×15 mm, placa MCP/interfaces 85×65×18 mm, buzzer 16×16×12 mm. Há 7 mm nominais entre a placa e os terminais inferiores dos reeds, sem fios/conectores. Medir módulos, USB e furação antes de fabricar suportes. Para a demonstração, posicionar o OLED externamente com os quatro fios acessíveis. O modelo não prevê recorte para esse módulo.

## Editar, exportar e conferir

KiCad: editar as folhas nativas, manter a biblioteca `Sensory.kicad_sym` e executar na raiz (PowerShell, Python 3 e KiCad CLI 10):

```powershell
python hardware/eletronica/exportar.py --kicad-cli 'C:\Program Files\KiCad\10.0\bin\kicad-cli.exe'
```

Executa ERC, confere alimentação, matriz e pinagem contra `board_config.h`; exporta PDF, BOM CSV e netlist XML em `hardware/eletronica/build/`. Não modifica o esquema. Revisar o PDF antes de montar. Os símbolos locais seguem [LICENSE.KICAD](../hardware/eletronica/LICENSE.KICAD), com a exceção upstream para projetos eletrônicos.

FreeCAD: editar a planilha e as operações, recomputar e salvar o FCStd. Para conferir sólidos/coordenadas, exportar STEP e lista de corte a partir do documento atual (FreeCAD 1.1+):

```powershell
& 'C:\Program Files\FreeCAD 1.1\bin\python.exe' hardware/mecanica/exportar.py
```

A exportação não salva nem substitui o FCStd; os resultados ficam em `hardware/mecanica/build/`. STEP não conserva a planilha/expressões. Para imagens ou pranchas adicionais, usar a exportação do editor quando necessária. Alterações de dimensões/pinagem exigem revisar a documentação, `board_config.h` e as verificações correspondentes. O exportador FreeCAD confere sólidos, colisões e orientação das 64 casas. A lista de corte usa as dimensões externas dos sólidos atuais; furos, recortes e materiais devem ser conferidos no modelo antes de fabricar.

## Conferência antes de fabricar e usar

1. Sem energia, verificar continuidade, curtos, faixa dos diodos, terras, pinagem real dos módulos e pull-ups I²C existentes.
2. Montar piloto 3×3 com passo 37,5 mm, vidro e bases reais. Cada peça deve acionar só sua casa e liberar ao ser retirada, inclusive perto das fronteiras e com vizinhos. Ajustar altura/orientação antes dos 64 furos; não forçar o vidro dos reeds.
3. Com os 47 kΩ, usar `v` do diagnóstico para medir VDD, coluna e ROW com zero, um e oito contatos fechados. Exigir ROW ocupada <0,2×VDD e vazia >0,8×VDD (0,66/2,64 V a 3,3 V), com margem. Não usar média da varredura no multímetro como nível LOW.
4. Retomar a varredura e testar todas as casas, cantos, colunas g/h, posição inicial e retângulos de três/quatro contatos. Não devem existir casas fantasma ou instabilidade. Conferir acomodação dos 300 µs com a fiação real.
5. Com o USB e cabo da demonstração, medir o 3V3 na placa e nos módulos durante varredura, atualização do OLED e teste `t`. Conferir 3,0…3,6 V, aquecimento e ausência de resets ou desconexões.
6. Testar a mesma sequência de comandos primeiro sem OLED e depois com ele, desligando antes de mudar a ligação. Conferir a resposta a `o` no diagnóstico, a legibilidade do SAN, indicação de xeque e gravação do PGN pelo terminal. Verificar isolamento, apoio do vidro e acesso ao fundo/USB.

A aquisição física já está integrada ao firmware principal, com filtro por casa e sinalização de perdas. A montagem completa e os tempos de aquisição ainda precisam dessa validação. Compilação, ERC e geometria não demonstram comportamento elétrico, magnético, térmico ou resistência mecânica.
