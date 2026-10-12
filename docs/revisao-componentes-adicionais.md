# Decisões elétricas do protótipo

A revisão B adota **matriz direta com MCP23017 e 1N4148, oito pull-ups de 47 kΩ e alimentação dos periféricos pelo 3V3 da DevKit V1**. A única entrada de energia é o USB do computador. TLV3704 e MCP1826S foram retirados do esquemático e da BOM; não há compra desses CIs prevista para a montagem.

## 1. Leitura direta

O comparador havia sido proposto para ampliar a margem entre a queda no diodo e o limite LOW do MCP. Para o protótipo acadêmico, a redução de corrente com 47 kΩ permite começar com os componentes existentes e validar a margem em bancada.

O [MCP23017, tabela 1-1](https://ww1.microchip.com/downloads/aemDocuments/documents/APID/ProductDocuments/DataSheets/MCP23017-Data-Sheet-DS20001952.pdf), exige LOW até `0,2×VDD` e HIGH a partir de `0,8×VDD`: **0,66 V e 2,64 V a 3,3 V**. A tensão ocupada é `VF do diodo + VOL da coluna + quedas nos contatos/fios`. O [1N4148](https://www.vishay.com/docs/81857/1n4148.pdf) não tem queda fixa de 0,7 V; ela varia com corrente e temperatura. O fabricante do lote disponível não foi identificado.

Com 47 kΩ, o limite calculado é **70 µA por ramo e 0,562 mA por coluna com oito ramos**, ignorando as quedas de tensão. Isso reduz VF e VOL em relação ao pull-up anterior de 10 kΩ. Os resistores de ¼ W disponíveis têm ampla sobra de potência.

A escolha é adequada para um protótipo caracterizado nas condições da demonstração, mas não constitui garantia analógica de pior caso: o VOL máximo de GPIO publicado pelo MCP é especificado a 4,5 V, não a 3,3 V. Medir ROW ocupada/vazia, incluindo os oito reeds da coluna. Aguardar inicialmente 300 µs; aumentar o tempo se a fiação exigir acomodação maior.

Se a medição mostrar LOW próximo ou acima do limite, testar primeiro **100 kΩ**, substituindo os resistores, e repetir leitura HIGH/LOW e acomodação. Se isso não resolver, avaliar diodos Schottky axiais, como [BAT85](https://assets.nexperia.com/documents/data-sheet/BAT85.pdf), em um piloto antes de trocar os 64. Eles reduzem VF, mas aumentam fugas. Condicionamento ativo só se justifica se essas opções falharem; não faz parte da arquitetura atual. Não remover os diodos nem alimentar o MCP a 5 V.

## 2. Alimentação pelo computador

O regulador auxiliar havia sido proposto por falta de informação sobre a placa. Ele não aumenta a capacidade do USB, do cabo ou das trilhas. Para a montagem atual, usar apenas o regulador onboard marcado `1117C 33 D405`; a marcação não identifica inequivocamente o fabricante ou a corrente disponível.

O [ESP-WROOM-32 opera de 3,0 a 3,6 V](https://www.espressif.com/sites/default/files/documentation/esp32-wroom-32_datasheet_en.pdf). Verificar o trilho 3V3 junto à placa e ao SD durante gravação e sinalização, com o mesmo computador/cabo da demonstração. Não presumir fonte de 1 A ou corrente extra por usar uma porta física USB 3. O diagnóstico não inicializa Wi-Fi/Bluetooth; o OLED permanece opcional e ausente da montagem mínima.

Se houver reinício, erro de SD ou aquecimento excessivo, verificar primeiro curtos, cabo, conexões, desacoplamento e consumo do buzzer. Se a limitação for térmica do regulador onboard, um regulador separado alimentado pelo mesmo USB pode ser avaliado, mantendo a restrição de corrente da porta; se o próprio USB estiver no limite, outro LDO não resolverá. A revisão só muda após identificar a causa. Não conectar fontes externas ou colocar saídas de reguladores em paralelo.

## 3. Montagem e compras

Os oito resistores de 47 kΩ foram confirmados pela equipe. Preservar os desacoplamentos, pull-ups das interfaces e o estágio NPN do buzzer indicados na [documentação elétrica](eletronica.md). Conferir esses itens no estoque/laboratório; nenhum CI adicional foi identificado como indispensável neste momento.

A [BOM atual](../hardware/eletronica/exports/bom.csv) corresponde à revisão B e não é cotação de compras. Testes magnéticos prévios não substituem as medições elétricas. O [plano de testes essenciais](plano-testes-fisicos.md) define a validação mínima antes de montar as 64 casas.
