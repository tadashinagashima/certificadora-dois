# Eletrônica — revisão B

O [projeto KiCad](../hardware/eletronica/tabuleiro-sensorial.kicad_pro) e o [PDF](../hardware/eletronica/exports/tabuleiro-sensorial.pdf) contêm três folhas: controle/alimentação, periféricos e matriz. A arquitetura adotada usa a **DevKit V1 de 30 pinos alimentada exclusivamente pelo USB do computador**, com os periféricos no seu pino 3V3 e leitura direta pelo MCP23017.

## 1. Componentes e alimentação

Mantêm-se os componentes disponíveis: ESP-WROOM-32, módulo CJMCU-2317 com MCP23017-E/SS, 64 reeds normalmente abertos, 64 diodos 1N4148 PTH, microSD de 8 GB em adaptador SD passivo, três botões, dois LEDs e buzzer. O OLED de 0,96" anunciado SSD1315 (`NFP1315-157BY`) é opcional e fica fora da montagem mínima.

O pino **3V3/R15** alimenta a rede `3V3_IO`; GND é comum. **VIN/L15 fica desconectado.** Não usar fonte externa nem aplicar 5 V ao MCP, SD ou sinais. Wi-Fi e Bluetooth não são necessários e não são inicializados pelo diagnóstico. O regulador marcado `1117C 33 D405` pertence à placa; sua capacidade útil será conferida no teste com o computador e cabo da demonstração.

TLV3704 e MCP1826S foram dispensados desta revisão. A [justificativa elétrica](revisao-componentes-adicionais.md) registra a decisão e os critérios para corrigi-la apenas se houver falha medida. A [BOM](../hardware/eletronica/exports/bom.csv) corresponde à arquitetura adotada; itens já existentes nos módulos não devem ser duplicados.

## 2. Pinagem da DevKit V1

A1 usa identificadores **L1…L15 e R1…R15**, definidos neste projeto de cima para baixo: componentes voltados para o observador, antena em cima e USB embaixo. Não são números do módulo WROOM. A equipe confirmou EN/L1, VIN/L15, GPIO23/R1 e 3V3/R15. O [esquema original DOIT de 30 pinos, preservado em espelho](https://embedded-systems-design.github.io/overview-of-the-esp32-devkit-doit-v1/SchematicsforESP32.pdf), fundamenta a sequência; conferir a serigrafia ao conectar.

| Função | GPIO | Posição em A1 | Configuração |
| --- | --- | --- | --- |
| SDA / SCL | 21 / 22 | R5 / R2 | I²C a 100 kHz, pull-ups a 3,3 V |
| SD SCK / MISO / MOSI / CS | 18 / 19 / 23 / 13 | R7 / R6 / R1 / L13 | SPI inicialmente a 1 MHz |
| Confirmar / promover / encerrar | 32 / 33 / 34 | L6 / L7 / L4 | Ativos LOW, pull-ups externos de 10 kΩ |
| LED verde / vermelho | 25 / 26 | L8 / L9 | Ativos HIGH, resistores de 1 kΩ |
| Buzzer | 27 | L10 | Ativo HIGH, estágio NPN |
| RESET MCP | 14 | L11 | Open-drain, pull-up externo de 10 kΩ |
| Alimentação dos periféricos | — | R15 | Saída 3V3 da placa |
| Terra | — | L14 / R14 | GND comum |

GPIO34 é somente entrada e requer o resistor externo. GPIOs de strapping, flash e UART0 não são usados pela aplicação. A referência no firmware está em [board_config.h](../firmware/diagnostico/board_config.h).

## 3. MCP23017 e matriz direta

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

## 4. Periféricos

### MicroSD

J1 representa os contatos externos do **adaptador SD convencional passivo**, sem regulador ou conversão de nível. O cartão permanece removível. Conferir a orientação antes de soldar.

| Contato SD | Sinal |
| --- | --- |
| 1 | CS / DAT3 |
| 2 | MOSI / CMD |
| 3 / 6 | GND |
| 4 | 3V3_IO |
| 5 | SCK |
| 7 | MISO / DAT0 |
| 8 / 9 | DAT1 / DAT2 |

CMD e DAT0–DAT3 recebem pull-ups de 10 kΩ, inclusive DAT1/2 sem uso, conforme a [Espressif](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/sd_pullup_requirements.html). Preservar C8=100 nF e C9=47 µF próximos ao adaptador, observando polaridade se eletrolítico. Começar com fios SPI curtos, até cerca de 10 cm, e 1 MHz.

### Interface

O buzzer previsto é ativo, 3,3 V, até 30 mA. Q1=2N3904 usa pino 1=emissor/GND, 2=base, 3=coletor; resistor de base de 680 Ω, resistor base-emissor de 100 kΩ e diodo flyback. Conferir tipo, consumo e pinagem dos componentes reaproveitados. Um NPN equivalente só pode ser usado com a ligação ajustada ao seu datasheet; não alimentar o buzzer diretamente pelo GPIO.

Os botões usam 10 kΩ e 100 nF, além do debounce descrito no [firmware](firmware-diagnostico.md). A indicação de promoção por pulsos do LED permanece parte da interface funcional futura.

J2 é a interface lógica do OLED, marcado DNP na configuração mínima. Seus números não garantem a ordem física do módulo. Se incluído, conferir controlador, pinagem, alimentação, endereço e pull-ups, e repetir o teste de alimentação com essa carga.

## 5. Reprodução e validação

As [instruções KiCad](../hardware/eletronica/README.md) descrevem geração, exportação e validação. Os relatórios [ERC](../hardware/eletronica/reports/erc.json) e [conectividade](../hardware/eletronica/reports/connectivity.json) conferem a revisão B, incluindo headers de 30 pinos, oito resistores de 47 kΩ, alimentação e grafo da matriz. Não simulam quedas de tensão, aquecimento ou magnetismo.

A montagem não requer comprar os dois CIs dispensados. Conferir apenas os passivos, NPN, conexões e buzzer da BOM no material disponível. Os [testes essenciais](plano-testes-fisicos.md) são alimentação USB sob gravação, leitura elétrica da matriz e seletividade magnética com vidro.
