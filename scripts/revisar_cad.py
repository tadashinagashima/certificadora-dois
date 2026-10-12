"""Reabre FCStd/STEP e confere a entrega salva sem modificar os modelos.

Executar com o Python incluido no FreeCAD, que fornece FreeCAD e Part.
"""
from pathlib import Path
from datetime import datetime, timezone
import hashlib
import json
import FreeCAD as App
import Part

ROOT = Path(__file__).resolve().parents[1]
native_path = ROOT / "hardware/mecanica/tabuleiro_sensorial.FCStd"
step_path = ROOT / "hardware/mecanica/exportacoes/tabuleiro_sensorial.step"
checks = []


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check(name, ok):
    checks.append({"check": name, "passed": bool(ok)})


before = {p.name: sha(p) for p in [native_path, step_path]}
doc = App.openDocument(str(native_path))
doc.recompute()
sheet = doc.getObject("Parametros")
parameters = {n: float(getattr(sheet, n).Value) for n in ["Outer", "Game", "Pitch", "Glass", "Height", "SensorGap", "SensorZ"]}
check("Dimensoes conforme planejamento e revisao A", parameters == {"Outer": 350.0, "Game": 300.0, "Pitch": 37.5, "Glass": 3.0, "Height": 60.0, "SensorGap": 3.0, "SensorZ": 47.0})
physical = [o for o in doc.Objects if hasattr(o, "Papel") and o.Papel != "visual" and hasattr(o, "Shape")]
reeds = [o for o in physical if o.Name.startswith("Reed_")]
invalid = [o.Name for o in physical if o.Shape.isNull() or not o.Shape.isValid()]
errors = [o.Name for o in doc.Objects if "Invalid" in o.State]
check("Documento reaberto e recomputado sem erros", not errors)
check("283 objetos fisicos/envelopes salvos", len(physical) == 283)
check("Todos os solidos nativos validos", not invalid)
check("64 reeds nativos", len(reeds) == 64)
for reed in reeds:
    square = reed.Name.removeprefix("Reed_")
    file, rank = ord(square[0]) - 97, int(square[1]) - 1
    center = reed.Shape.CenterOfMass
    bb = reed.Shape.BoundBox
    check("Posicao/eixo do reed " + square, abs(center.x - (43.75 + file * 37.5)) < 1e-6 and abs(center.y - (43.75 + rank * 37.5)) < 1e-6 and abs(center.z - 47) < 1e-6 and abs(bb.ZLength - 14) < 1e-6 and abs(bb.XLength - 2) < 1e-6 and abs(bb.YLength - 2) < 1e-6)
expression_count = sum(len(o.ExpressionEngine) for o in doc.Objects if hasattr(o, "ExpressionEngine"))
check("Expressoes parametricas preservadas no arquivo", expression_count > 100)

shape = Part.read(str(step_path))
native_solids = sum(len(o.Shape.Solids) for o in physical)
native_volume = sum(o.Shape.Volume for o in physical)
step_volume = shape.Volume
check("STEP reimportado valido", shape.isValid() and all(s.isValid() for s in shape.Solids))
check("STEP preserva contagem dos solidos fisicos", len(shape.Solids) == native_solids)
check("STEP preserva volume fisico", abs(native_volume - step_volume) < max(0.001, abs(native_volume) * 1e-7))
App.closeDocument(doc.Name)
check("Arquivos originais permaneceram inalterados", before == {p.name: sha(p) for p in [native_path, step_path]})

report = {
    "checked_at_utc": datetime.now(timezone.utc).isoformat(),
    "tool": "FreeCAD " + ".".join(App.Version()[:3]),
    "parameters_mm": parameters,
    "native_objects": len(physical), "native_solids": native_solids,
    "native_volume_mm3": native_volume, "step_solids": len(shape.Solids), "step_volume_mm3": step_volume,
    "expressions": expression_count, "checks": checks,
    "success": all(c["passed"] for c in checks),
    "source_sha256": before,
    "scope": "Reabertura de arquivos e geometria. Nao valida materiais, campo magnetico, rigidez ou fabricacao.",
}
(ROOT / "validacao/revisao-cad.json").write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf8")
print(json.dumps({"success": report["success"], "objects": len(physical), "native_solids": native_solids, "step_solids": len(shape.Solids), "expressions": expression_count, "failed": [c for c in checks if not c["passed"]]}))
raise SystemExit(0 if report["success"] else 1)
