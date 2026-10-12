"""Aplica cores/camera no arquivo nativo sem depender de renderizacao OpenGL."""
from pathlib import Path
import json
import FreeCAD as App
import FreeCADGui as Gui
from PySide import QtWidgets

ROOT=Path(__file__).resolve().parent
Gui.showMainWindow()
Gui.getMainWindow().hide()
doc=App.openDocument(str(ROOT/'tabuleiro_sensorial.FCStd'))
colors=json.loads((ROOT/'exportacoes/cores_modelo.json').read_text())
for obj in doc.Objects:
    if hasattr(obj,'Shape'):
        obj.Visibility=obj.Name in colors
        if obj.Name in colors:
            obj.ViewObject.ShapeColor=tuple(colors[obj.Name]['rgb'])
            obj.ViewObject.LineColor=(0.12,0.15,0.18)
            obj.ViewObject.Transparency=colors[obj.Name]['transparency']
Gui.activeDocument().activeView().viewAxonometric()
Gui.activeDocument().activeView().fitAll()
QtWidgets.QApplication.processEvents()
doc.recompute()
doc.save()
print('GUI presentation saved')
