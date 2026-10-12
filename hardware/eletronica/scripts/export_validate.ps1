param([string]$KiCadCli = 'C:\Program Files\KiCad\10.0\bin\kicad-cli.exe')
$ErrorActionPreference = 'Stop'
$electronicRoot = Split-Path $PSScriptRoot -Parent
$schematic = Join-Path $electronicRoot 'tabuleiro-sensorial.kicad_sch'
function Invoke-KiCad([string[]]$Arguments) {
    & $KiCadCli @Arguments
    if ($LASTEXITCODE -ne 0) { throw "KiCad returned $LASTEXITCODE : $Arguments" }
}
# Does not regenerate or overwrite manual native edits.
Invoke-KiCad @('sch','erc',$schematic,'--format','json','--severity-all','--exit-code-violations','-o',(Join-Path $electronicRoot 'reports/erc.json'))
Invoke-KiCad @('sch','export','pdf',$schematic,'-o',(Join-Path $electronicRoot 'exports/tabuleiro-sensorial.pdf'))
Invoke-KiCad @('sch','export','svg',$schematic,'-o',(Join-Path $electronicRoot 'exports/'))
Invoke-KiCad @('sch','export','netlist',$schematic,'--format','kicadxml','-o',(Join-Path $electronicRoot 'exports/tabuleiro-sensorial.xml'))
Invoke-KiCad @('sch','export','bom',$schematic,'-o',(Join-Path $electronicRoot 'exports/bom.csv'))
python (Join-Path $PSScriptRoot 'validate_netlist.py')
if ($LASTEXITCODE -ne 0) { throw 'Connectivity checks failed' }
# Preview rendering optional dependency; native PDF/SVG do not depend on Python packages.
python -c "import fitz,pathlib; p=pathlib.Path(r'$electronicRoot'); d=fitz.open(p/'exports/tabuleiro-sensorial.pdf'); [s.get_pixmap(matrix=fitz.Matrix(1.6,1.6)).save(p/('exports/preview-'+str(i+1)+'.png')) for i,s in enumerate(d)]"
if ($LASTEXITCODE -ne 0) { Write-Warning 'Install PyMuPDF only if PNG previews are needed; PDF and SVG exported.' }
