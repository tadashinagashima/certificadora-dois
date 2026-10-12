# Projeto eletrônico KiCad

Abra [`tabuleiro-sensorial.kicad_pro`](tabuleiro-sensorial.kicad_pro) no KiCad 10. Mantenha as três folhas, `Sensory.kicad_sym` e `sym-lib-table` na mesma pasta. A biblioteca local permite editar o projeto sem depender dos símbolos globais.

A revisão B usa a DevKit V1 de 30 pinos alimentada pelo USB do computador, periféricos em seu pino 3V3 e matriz direta com oito pull-ups de 47 kΩ. Consulte a [especificação elétrica](../../docs/eletronica.md) para pinagem e montagem e as [decisões elétricas](../../docs/revisao-componentes-adicionais.md) para a justificativa.

## 1. Arquivos

| Local | Conteúdo |
| --- | --- |
| `*.kicad_sch` | Três folhas editáveis: controle/alimentação, periféricos e matriz |
| `exports/` | PDF, SVGs, BOM CSV, netlist XML e prévias PNG |
| `reports/erc.json` | Verificação de regras elétricas pelo KiCad CLI |
| `reports/connectivity.json` | Validação independente da netlist, com hash e limites |
| `reports/components.json` | Inventário produzido pelo gerador da revisão |
| `scripts/` | Geração, exportação e validação |

## 2. Exportar e validar

Requisitos: PowerShell, KiCad CLI 10 e Python 3 no PATH. PyMuPDF é opcional, necessário apenas para regenerar as prévias PNG. Na raiz do repositório:

```powershell
.\hardware\eletronica\scripts\export_validate.ps1
```

O script usa `C:\Program Files\KiCad\10.0\bin\kicad-cli.exe`; passe `-KiCadCli 'caminho\kicad-cli.exe'` para outra instalação. Ele executa ERC com todas as severidades e falha em violações, exporta PDF/SVG/XML/BOM e verifica a netlist. Não regenera as fontes nativas. Revisar também a legibilidade do PDF após alterações.

Os relatórios referem-se aos arquivos verificados; alterações na arquitetura exigem atualizar os critérios de `validate_netlist.py`. As pranchas contêm apenas informações elétricas, identificação e montagem; os procedimentos e critérios de bancada ficam no [plano de testes](../../docs/plano-testes-fisicos.md).

## 3. Regenerar as fontes

`generate_schematic.py` recria a revisão B e **sobrescreve os arquivos nativos**. Incorporar ao gerador quaisquer edições manuais que devam ser preservadas antes de executá-lo:

```powershell
python .\hardware\eletronica\scripts\generate_schematic.py
```

O gerador usa a biblioteca padrão do Python e as bibliotecas de símbolos do KiCad 10. Ajustar `LIBDIR` no script se a instalação estiver em outro caminho. Depois, repetir a exportação e validação.

## 4. Licença dos símbolos

`Sensory.kicad_sym` inclui símbolos adaptados das bibliotecas KiCad 10 e símbolos de DevKit, SD, OLED e buzzer criados para este projeto. A biblioteca é distribuída sob [CC BY-SA 4.0 com exceção para projetos eletrônicos](https://www.kicad.org/libraries/license/), conforme [`LICENSE.KICAD`](LICENSE.KICAD). A licença dos desenhos que a utilizam segue a exceção upstream.
