"""Confere e exporta o FCStd atual sem salva-lo. Usar Python do FreeCAD."""
from pathlib import Path
import csv
import FreeCAD as App
import Import

root = Path(__file__).resolve().parent
source = root / 'tabuleiro_sensorial.FCStd'
output = root / 'build'
output.mkdir(exist_ok=True)
doc = App.openDocument(str(source))
doc.recompute()
sheet = doc.getObject('Parametros')
physical = [o for o in doc.Objects if hasattr(o, 'Papel') and o.Papel != 'visual' and hasattr(o, 'Shape')]
assert physical and not any('Invalid' in o.State for o in doc.Objects), 'Documento invalido'
assert all(not o.Shape.isNull() and o.Shape.isValid() for o in physical), 'Solido invalido'

def value(name):
    item = getattr(sheet, name)
    return item.Value if hasattr(item, 'Value') else float(item)

assert sum(o.Name.startswith('Reed_') for o in physical) == 64
with (output / 'coordenadas_sensores.csv').open('w', newline='', encoding='utf-8-sig') as stream:
    writer = csv.writer(stream)
    writer.writerow(['casa','coluna','linha','x_mm','y_mm','z_eixo_mm','eixo_reed'])
    for row in range(8):
        for column in range(8):
            square = chr(97 + column) + str(row + 1)
            obj = doc.getObject('Reed_' + square)
            center, box = obj.Shape.CenterOfMass, obj.Shape.BoundBox
            assert abs(center.x - (value('Margin') + (column + .5) * value('Pitch'))) < 1e-6
            assert abs(center.y - (value('Margin') + (row + .5) * value('Pitch'))) < 1e-6
            assert abs(center.z - value('SensorZ')) < 1e-6
            assert abs(box.ZLength - value('ReedLength')) < 1e-6
            assert abs(box.XLength - value('ReedDiameter')) < 1e-6
            writer.writerow([square,column,row,center.x,center.y,center.z,'Z'])

# Bounding-box broad phase; allow touching faces, reject volumetric overlap.
for index, first in enumerate(physical):
    for second in physical[index+1:]:
        a, b = first.Shape.BoundBox, second.Shape.BoundBox
        overlap = [min(getattr(a, axis+'Max'), getattr(b, axis+'Max')) -
                   max(getattr(a, axis+'Min'), getattr(b, axis+'Min')) for axis in 'XYZ']
        if min(overlap) > 1e-7:
            assert first.Shape.common(second.Shape).Volume <= 1e-5, (first.Name, second.Name)

step = output / 'tabuleiro_sensorial.step'
Import.export(physical, str(step))
# Envelope dimensions from the edited solids; no duplicate geometry formulas.
parts = [('Fundo','MDF'), ('Frente','MDF'), ('Traseira','MDF'),
         ('Esquerda','MDF'), ('Direita','MDF'), ('SuporteSensores','MDF'),
         ('TravessaCentral','MDF'), *[(f'Apoio{i}','MDF/madeira') for i in range(4)],
         ('Vidro','Vidro'), ('Junta','Elastomero')]
with (output / 'lista_corte.csv').open('w', newline='', encoding='utf-8-sig') as stream:
    writer = csv.writer(stream)
    writer.writerow(['peca','quantidade','material','comprimento_mm','largura_mm','espessura_mm'])
    for name, material in parts:
        obj = doc.getObject(name)
        box = obj.Shape.BoundBox
        dimensions = sorted([box.XLength, box.YLength, box.ZLength], reverse=True)
        writer.writerow([obj.Label, 1, material, *[round(d, 6) for d in dimensions]])
App.closeDocument(doc.Name)
print(f'CAD: {len(physical)} objetos validos, 64 coordenadas, sem colisoes; STEP e CSV em {output}')
