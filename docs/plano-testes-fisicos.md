# Testes físicos essenciais

Validar primeiro um piloto e depois a matriz completa, usando a [eletrônica B](eletronica.md) e o [diagnóstico](firmware-diagnostico.md). Os ensaios abaixo ainda estão pendentes; ERC, compilação e verificações geométricas não os substituem.

## 1. Evidências de sensoriamento

A equipe relatou acionamento repetível com reed vertical e ímã alinhado ao eixo até aproximadamente **40 mm do centro do sensor**. Com deslocamento lateral de aproximadamente **30 mm**, não houve acionamento na posição ensaiada. Os testes foram feitos **sem o vidro de 3 mm** e não identificam separadamente o desempenho dos ímãs de 10 e 12 mm.

O alcance axial não define um raio de detecção nem comprova rejeição dos vizinhos. Antes de furar o MDF definitivo, montar um piloto **3×3, com passo de 37,5 mm**, vidro e bases reais das peças. Comparar os dois ímãs no centro e nos limites de posicionamento aceitos, inclusive com peças vizinhas. Ajustar altura e orientação dos reeds até cada peça acionar somente sua casa e liberar ao ser retirada. Registrar altura, ímã, posições, repetições e falhas. Evitar esforço no corpo de vidro ao dobrar ou fixar terminais.

## 2. Leitura elétrica

Sem energia, conferir continuidade, ausência de curtos, faixa dos diodos, headers, terras e alimentação dos módulos. Verificar os pull-ups I²C existentes para não duplicá-los desnecessariamente.

Com os oito resistores de **47 kΩ** instalados e alimentação pelo USB do computador:

1. Enviar `v` para manter COL_a LOW. Medir VDD, a coluna e cada ROW com os reeds da coluna abertos, um fechado e os oito fechados. Com multímetro, usar esse modo estático; a média da varredura normal não representa o nível LOW.
2. Conferir **ROW ocupada < 0,2×VDD** e **ROW vazia > 0,8×VDD** — respectivamente 0,66 V e 2,64 V a 3,3 V. As medidas precisam ficar claramente do lado válido, considerando resolução e variação observada. Se a margem for insuficiente, seguir as [alternativas elétricas](revisao-componentes-adicionais.md) antes de replicar o circuito.
3. Enviar `v` novamente ou `r`. Na varredura, testar cada casa, os quatro cantos, oito casas de cada coluna, posição inicial e retângulos com três/quatro contatos fechados. A leitura deve coincidir com a ocupação, sem casas fantasma ou instabilidade, inclusive nas colunas g/h.

Usar a fiação prevista para a montagem. Se houver instabilidade, observar a acomodação de ROW com osciloscópio, quando disponível, e ajustar os 300 µs iniciais. Medição estática não valida esse tempo. Comparações com outro pull-up exigem **substituir** o resistor, sem ligação em paralelo.

## 3. Alimentação, SD e interface

Usar o mesmo computador e cabo USB previstos para a demonstração, sem fonte externa e com VIN desconectado. Medir 3V3 junto à DevKit e ao adaptador SD, em repouso e durante os comandos `w` e `t`. O trilho deve permanecer entre **3,0 e 3,6 V**, sem reinícios, desconexões USB ou erros de leitura/escrita. Osciloscópio, se disponível, permite observar quedas rápidas que o multímetro não registra.

O comando `w` cria e verifica um arquivo de 16 KiB com varreduras intercaladas, LEDs acesos e buzzer no primeiro bloco. Conferir também a ocupação lida durante o teste. Repetir o uso representativo durante a duração prevista da demonstração, verificando estabilidade e aquecimento do regulador. Se houver falha, verificar cabo, conexões, desacoplamento e cargas antes de propor outro regulador.

Acionar os três botões e conferir os eventos no serial; usar `t` para os LEDs e o buzzer. Confirmar tensão, consumo e pinagem do buzzer/NPN reaproveitados. O OLED fica fora da montagem mínima; sua inclusão exige repetir o ensaio de alimentação.

## 4. Montagem e demonstração

Conferir apoio e remoção do vidro, isolamento das soldas, fixação sem esforço nos reeds, passagem de cabos e acesso ao USB, microSD e fundo. Medir módulos e vidro antes de fabricar suportes; as reservas do [CAD](mecanica.md) não são modelos de fabricante. Validar retenção do vidro para o transporte previsto.

Registrar resultado, configuração usada e falhas dos três ensaios principais: seletividade magnética, leitura elétrica e alimentação durante gravação. A demonstração completa ainda depende do firmware de xadrez: jogadas comuns e especiais, rejeição de jogadas inválidas e conferência do PGN deverão atender aos [requisitos](requisitos.md).
