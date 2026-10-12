"""Gera o CAD nativo e evidencias. Executar com o Python distribuido pelo FreeCAD.

As pecas usam primitivas/booleanos nativos com expressoes ligadas a Spreadsheet;
o arquivo FCStd continua parametrico sem instalar modulo Python adicional.
"""
from pathlib import Path
import csv
import json
import math
import sys
import FreeCAD as App
import Part

ROOT = Path(__file__).resolve().parent
OUT = ROOT / "exportacoes"
OUT.mkdir(exist_ok=True)
doc = App.newDocument("TabuleiroSensorial")
sheet = doc.addObject("Spreadsheet::Sheet", "Parametros")
sheet.Label = "00 | Parametros (mm) - hipoteses de prototipo"
PARAMS = [
    ("Outer", "350 mm", "Largura/comprimento externo (aproximado no requisito)"),
    ("Pitch", "37.5 mm", "Planejamento: area300 / oito casas por eixo"),
    ("Glass", "3 mm", "Espessura vidro definida na documentacao"),
    ("Height", "60 mm", "Hipotese: altura para reeds verticais e eletronica"),
    ("Base", "6 mm", "MDF fundo removivel"),
    ("Wall", "9 mm", "MDF paredes laterais"),
    ("Carrier", "6 mm", "Hipotese MDF superior com furos e canais"),
    ("GlassNominal", "350 mm", "Hipotese vidro existente: MEDIR antes de fabricar"),
    ("Gasket", "1 mm", "Junta compressivel periferica, espessura instalada"),
    ("GasketWidth", "10 mm", "Largura da junta periferica"),
    ("SensorGap", "3 mm", "Distancia topo corpo reed / face inferior vidro"),
    ("ReedDiameter", "2 mm", "Envelope reed; selecionar componente real"),
    ("ReedLength", "14 mm", "Envelope corpo reed; selecionar componente real"),
    ("ReedHole", "2.8 mm", "Furo provisório: medir reed e testar montagem"),
    ("LeadBelow", "5 mm", "Terminal inferior reto - envelope antes de dobrar"),
    ("LeadAbove", "1.5 mm", "Terminal superior rebaixado - ajustar apos teste"),
    ("ChannelWidth", "3 mm", "Canal superior para fios isolados"),
    ("ChannelDepth", "2 mm", "Profundidade canal, nao invadir apoio vidro"),
    ("MagnetDiameter", "12 mm", "Planejamento: ima ferrite em disco"),
    ("MagnetHeight", "3 mm", "Planejamento: altura ima ferrite"),
    ("Support", "12 mm", "Blocos de apoio em MDF empilhado ou madeira"),
    ("ButtonDiameter", "12 mm", "Furo botao de painel, confirmar modelo"),
    ("LedDiameter", "5 mm", "Furo LED, confirmar suporte"),
    ("BaseScrew", "3.4 mm", "Folga para parafuso M3 do fundo"),
    ("Pilot", "2.5 mm", "Piloto apoio, confirmar fixacao em MDF"),
    ("FootDiameter", "14 mm", "Pes elastomericos adesivos"),
    ("FootHeight", "3 mm", "Altura dos pes"),
    ("Game", "=8*Pitch", "Lado area de jogo"),
    ("Margin", "=(Outer-Game)/2", "Moldura aparente"),
    ("GlassSize", "=GlassNominal", "Lado nominal vidro; independente da caixa"),
    ("GlassZ", "=Height-Glass", "Cota inferior vidro"),
    ("CarrierTop", "=GlassZ-Gasket", "Plano apoio da junta"),
    ("CarrierZ", "=CarrierTop-Carrier", "Cota inferior suporte"),
    ("SensorTop", "=GlassZ-SensorGap", "Topo corpo reed vertical"),
    ("SensorZ", "=SensorTop-ReedLength/2", "Centro corpo reed vertical"),
    ("WallHeight", "=CarrierZ-Base", "Altura paredes"),
]
sheet.set("A1", "PARAMETRO")
sheet.set("B1", "VALOR / FORMULA")
sheet.set("C1", "JUSTIFICATIVA / HIPOTESE AJUSTAVEL")
rows = {}
for r, (name, value, desc) in enumerate(PARAMS, 2):
    sheet.set(f"A{r}", name)
    sheet.set(f"B{r}", value)
    sheet.setAlias(f"B{r}", name)
    sheet.set(f"C{r}", desc)
    rows[name] = r
sheet.setColumnWidth("A", 145)
sheet.setColumnWidth("B", 155)
sheet.setColumnWidth("C", 510)
sheet.setStyle("A1:C1", "bold", "add")
doc.recompute()

groups = {}
for n, title in [("Estrutura", "01 | MDF e fixacoes"), ("VidroGrupo", "02 | Vidro e junta"),
                 ("Matriz", "03 | Matriz 8x8 (envelopes)"), ("Interface", "04 | Interface e acessos"),
                 ("Eletronica", "05 | Volumes reservados - validar modulos"),
                 ("Visual", "06 | Padrao visual ilustrativo")]:
    groups[n] = doc.addObject("App::DocumentObjectGroup", n)
    groups[n].Label = title

finals = []
visual = []
helpers = []
colors = {}
MDF = (0.66, 0.45, 0.25)
MDF_LIGHT = (0.78, 0.60, 0.38)
BLACK = (0.12, 0.15, 0.18)

def e(obj, prop, expr):
    # Parametro identifiers are prefixed automatically; mm constants stay intact.
    import re
    for name in sorted(rows, key=len, reverse=True):
        expr = re.sub(r"\b" + name + r"\b", "Parametros." + name, str(expr))
    obj.setExpression(prop, expr)

def box(name, L, W, H, x="0 mm", y="0 mm", z="0 mm"):
    o = doc.addObject("Part::Box", name)
    for p, v in [("Length", L), ("Width", W), ("Height", H),
                 ("Placement.Base.x", x), ("Placement.Base.y", y), ("Placement.Base.z", z)]:
        e(o, p, v)
    helpers.append(o)
    return o

def cylinder(name, rad, h, x, y, z, axis="Z"):
    o = doc.addObject("Part::Cylinder", name)
    for p, v in [("Radius", rad), ("Height", h), ("Placement.Base.x", x),
                 ("Placement.Base.y", y), ("Placement.Base.z", z)]:
        e(o, p, v)
    if axis == "X":
        o.Placement.Rotation = App.Rotation(App.Vector(0, 0, 1), App.Vector(1, 0, 0))
    if axis == "Y":
        o.Placement.Rotation = App.Rotation(App.Vector(0, 0, 1), App.Vector(0, 1, 0))
    helpers.append(o)
    return o

def compound(name, objs):
    o = doc.addObject("Part::Compound", name)
    o.Links = objs
    helpers.append(o)
    return o

def countersink(name, x, y, z, upside=False):
    o=doc.addObject("Part::Cone",name)
    for p,v in [("Radius1","BaseScrew/2" if not upside else "BaseScrew"),
                ("Radius2","BaseScrew" if not upside else "BaseScrew/2"),
                ("Height","BaseScrew/2"),("Placement.Base.x",x),
                ("Placement.Base.y",y),("Placement.Base.z",z)]:
        e(o,p,v)
    helpers.append(o)
    return o

def cut(name, base, tools):
    tool = tools[0] if len(tools) == 1 else compound(name + "Ferramentas", tools)
    o = doc.addObject("Part::Cut", name)
    o.Base, o.Tool = base, tool
    o.Refine = True
    helpers.append(o)
    return o

def finish(obj, label, group, color, transparency=0, role="fisico"):
    obj.Label = label
    obj.addProperty("App::PropertyString", "Papel", "Projeto").Papel = role
    groups[group].addObject(obj)
    colors[obj.Name] = {"rgb": color, "transparency": transparency}
    if role == "visual":
        visual.append(obj)
    else:
        finals.append(obj)
    if App.GuiUp:
        obj.ViewObject.ShapeColor = color
        obj.ViewObject.LineColor = (0.16, 0.18, 0.20)
        obj.ViewObject.Transparency = transparency
    return obj

# Removable bottom; four corner fasteners are accessible from below.
mounts = [("Wall+Support/2", "Wall+Support/2"),
          ("Outer-Wall-Support/2", "Wall+Support/2"),
          ("Wall+Support/2", "Outer-Wall-Support/2"),
          ("Outer-Wall-Support/2", "Outer-Wall-Support/2")]
base_holes = [cylinder(f"FuroFundo{i}", "BaseScrew/2", "Base+2 mm", x,y,"-1 mm")
              for i,(x,y) in enumerate(mounts)]
base_holes += [countersink(f"EscareadoFundo{i}",x,y,"0 mm",True) for i,(x,y) in enumerate(mounts)]
base = finish(cut("Fundo", box("FundoBruto", "Outer", "Outer", "Base"), base_holes),
              "M01 | Fundo MDF 6 mm - removivel", "Estrutura", MDF_LIGHT)

# Side panels form butt joints. Front panel has controls accessible from white side.
front_holes = []
for i, frac in enumerate(["0.1857142857", "0.5", "0.8142857143"]):
    front_holes.append(cylinder(f"FuroBotao{i}", "ButtonDiameter/2", "Wall+2 mm",
                                f"Outer*{frac}", "-1 mm", "Base+WallHeight/2", "Y"))
for i, frac in enumerate(["0.3285714286", "0.6714285714"]):
    front_holes.append(cylinder(f"FuroLed{i}", "LedDiameter/2", "Wall+2 mm",
                                f"Outer*{frac}", "-1 mm", "Base+WallHeight/2", "Y"))
front = finish(cut("Frente", box("FrenteBruta", "Outer", "Wall", "WallHeight", z="Base"), front_holes),
               "M02 | Frente - 3 botoes e 2 LEDs", "Estrutura", MDF)
back = finish(box("Traseira", "Outer", "Wall", "WallHeight", y="Outer-Wall", z="Base"),
              "M03 | Traseira MDF 9 mm", "Estrutura", MDF)
left = finish(box("Esquerda", "Wall", "Outer-2*Wall", "WallHeight", y="Wall", z="Base"),
              "M04 | Lateral esquerda MDF 9 mm", "Estrutura", MDF)
# A broad connector service window avoids fixing undocumented module footprints.
access = box("AberturaConectores", "Wall+2 mm", "70 mm", "20 mm", "Outer-Wall-1 mm", "Outer/2-35 mm", "Base+3 mm")
right = finish(cut("Direita", box("DireitaBruta", "Wall", "Outer-2*Wall", "WallHeight", "Outer-Wall", "Wall", "Base"), [access]),
               "M05 | Lateral direita - acesso 70 x 20 USB / microSD", "Estrutura", MDF)

glass = finish(box("Vidro", "GlassSize", "GlassSize", "Glass", "(Outer-GlassSize)/2", "(Outer-GlassSize)/2", "GlassZ"),
               "V01 | Vidro 3 mm removivel - bordas lapidadas", "VidroGrupo", (0.67,0.85,0.89), 65)
gask_outer = box("JuntaExterna", "Outer-4 mm", "Outer-4 mm", "Gasket", "2 mm", "2 mm", "GlassZ-Gasket")
gask_inner = box("JuntaVazio", "Outer-4 mm-2*GasketWidth", "Outer-4 mm-2*GasketWidth", "Gasket+2 mm", "2 mm+GasketWidth", "2 mm+GasketWidth", "GlassZ-Gasket-1 mm")
gasket = finish(cut("Junta", gask_outer, [gask_inner]), "J01 | Junta periferica 10 mm - apoio vidro", "VidroGrupo", BLACK)

carrier_holes = [cylinder(f"FuroSuporte{i}", "BaseScrew/2", "Carrier+2 mm", x,y,"CarrierZ-1 mm")
                 for i,(x,y) in enumerate(mounts)]
carrier_holes += [countersink(f"EscareadoSuporte{i}",x,y,"CarrierTop-BaseScrew/2") for i,(x,y) in enumerate(mounts)]
carrier_holes += [box("PassagemChicote", "8 mm", "28 mm", "Carrier+2 mm", "Outer-23 mm", "Outer/2-14 mm", "CarrierZ-1 mm")]
for row in range(8):
    for col in range(8):
        sq = chr(97+col)+str(row+1)
        carrier_holes.append(cylinder("FuroReed_"+sq,"ReedHole/2","Carrier+2 mm",
                                     f"Margin+({col}+0.5)*Pitch",f"Margin+({row}+0.5)*Pitch","CarrierZ-1 mm"))
for i in range(8):
    carrier_holes.append(box(f"CanalColuna{i}","ChannelWidth","Game","ChannelDepth+1 mm",
                             f"Margin+({i}+0.5)*Pitch-ChannelWidth/2","Margin","CarrierTop-ChannelDepth"))
    carrier_holes.append(box(f"CanalLinha{i}","Outer-20 mm+ChannelWidth-Margin","ChannelWidth","ChannelDepth+1 mm",
                             "Margin",f"Margin+({i}+0.5)*Pitch-ChannelWidth/2","CarrierTop-ChannelDepth"))
carrier_holes.append(box("ColetorVertical","ChannelWidth","Game","ChannelDepth+1 mm",
                         "Outer-20 mm","Margin","CarrierTop-ChannelDepth"))
carrier_holes.append(box("ColetorSuperior","Outer-20 mm+ChannelWidth-Margin","ChannelWidth","ChannelDepth+1 mm",
                         "Margin","Margin+Game-ChannelWidth","CarrierTop-ChannelDepth"))
# Grip recesses outside the gasket footprint permit lifting the unglued glass.
for side in [0,1]:
    carrier_holes.append(cylinder(f"PegaVidro{side}","10 mm","Carrier+2 mm", "0 mm" if side==0 else "Outer","Outer/2","CarrierZ-1 mm"))
carrier = finish(cut("SuporteSensores", box("SuporteBruto", "Outer", "Outer", "Carrier", z="CarrierZ"), carrier_holes),
                 "M06 | MDF 6 mm - 64 furos, 16 canais e pegas", "Estrutura", (0.58,0.49,0.34))
finish(box("TravessaCentral","Wall","Outer-2*Wall","12 mm","Outer/2-Wall/2","Wall","CarrierZ-12 mm"),
       "M07 | Travessa central de apoio - entre colunas d/e", "Estrutura", MDF)

for i,(x,y) in enumerate(mounts):
    support_raw = box(f"ApoioBruto{i}", "Support", "Support", "CarrierZ-Base", f"({x})-Support/2", f"({y})-Support/2", "Base")
    # Pilot holes from both faces stay separate (avoid incompatible screw meeting).
    pilot_bottom = cylinder(f"PilotoInferior{i}", "Pilot/2", "10 mm", x,y,"Base")
    pilot_top = cylinder(f"PilotoSuperior{i}", "Pilot/2", "10 mm", x,y,"CarrierZ-10 mm")
    finish(cut(f"Apoio{i}", support_raw, [pilot_bottom,pilot_top]), f"A0{i+1} | Apoio 12 x 12 - furos piloto", "Estrutura", MDF)
    # Screw hardware omitted: no false thread geometry / screw-length decision.
    px = "30 mm" if i in [0,2] else "Outer-30 mm"
    py = "30 mm" if i in [0,1] else "Outer-30 mm"
    finish(cylinder(f"Pe{i}", "FootDiameter/2", "FootHeight", px,py,"-FootHeight"), f"P0{i+1} | Pe elastomerico", "Estrutura", BLACK)

# Simplified reference envelopes; no vendor dimensions have been specified.
for i,(label,frac) in enumerate(zip(["CONFIRMAR", "PROMOCAO", "ENCERRAR"], ["0.1857142857","0.5","0.8142857143"])):
    finish(cylinder(f"Botao{i}", "ButtonDiameter/2-0.2 mm", "Wall", f"Outer*{frac}", "0 mm", "Base+WallHeight/2", "Y"),
           "REF | Botao " + label, "Interface", (0.16,0.24,0.31), role="envelope")
for i,(frac,col) in enumerate(zip(["0.3285714286","0.6714285714"],[(0.05,0.75,0.43),(0.96,0.20,0.18)])):
    finish(cylinder(f"Led{i}", "LedDiameter/2-0.1 mm", "Wall", f"Outer*{frac}", "0 mm", "Base+WallHeight/2", "Y"),
           f"REF | LED {i+1}", "Interface", col, role="envelope")

for name,label,L,W,H,x,y,z,col in [
    ("VolumeESP32","ESP32 / USB - envelope generico","58 mm","30 mm","15 mm","Outer-Wall-65 mm","Outer/2-33 mm","Base+4 mm",(0.10,0.22,0.32)),
    ("VolumeSD","adaptador SD passivo / microSD - envelope","32 mm","24 mm","2.1 mm","Outer-Wall-32 mm","Outer/2+4 mm","Base+8 mm",(0.12,0.33,0.47)),
    ("VolumeControle","placa MCP23017 / interfaces - reserva","85 mm","65 mm","18 mm","Outer/2-42.5 mm","Outer/2-32.5 mm","Base+4 mm",(0.08,0.42,0.31)),
    ("VolumeBuzzer","buzzer - reserva","16 mm","16 mm","12 mm","Outer/2-8 mm","45 mm","Base+4 mm",(0.12,0.15,0.19)),
]:
    finish(box(name,L,W,H,x,y,z), "REF | "+label, "Eletronica", col, role="envelope")

magnet = finish(cylinder("ImaReferencia","MagnetDiameter/2","MagnetHeight","Margin+Pitch/2","Margin+Pitch/2","Height+0.2 mm"),
                "REF | Ima ferrite 12 x 3 - exemplo a1, folga feltro0.2 provisoria", "Matriz", (0.27,0.28,0.29),role="envelope")

# 64 cell assemblies, each with one glass reed envelope and one diode envelope.
for row in range(8):
    for col in range(8):
        sq = chr(97+col)+str(row+1)
        x = f"Margin+({col}+0.5)*Pitch"
        y = f"Margin+({row}+0.5)*Pitch"
        reed = cylinder("Reed_"+sq, "ReedDiameter/2", "ReedLength", x, y, "SensorTop-ReedLength")
        finish(reed, "S_"+sq+" | Reed - envelope", "Matriz", (0.31,0.66,0.65), role="envelope")
        leads = [cylinder("ReedLeadL_"+sq,"0.25 mm","LeadBelow",x,y,"SensorTop-ReedLength-LeadBelow"),
                 cylinder("ReedLeadR_"+sq,"0.25 mm","LeadAbove",x,y,"SensorTop")]
        finish(compound("ReedLeads_"+sq,leads),"S_"+sq+" | Terminais retos - ilustrativos", "Matriz", (0.72,0.74,0.77), role="envelope")
        diode = cylinder("Diode_"+sq,"0.9 mm","4 mm",f"({x})+6 mm",y,"SensorTop-ReedLength-2 mm","X")
        finish(diode,"D_"+sq+" | 1N4148 - envelope", "Matriz", (0.86,0.39,0.13), role="envelope")
        glue = cut("Cola_"+sq,cylinder("ColaExt_"+sq,"3 mm","2 mm",x,y,"CarrierZ-2 mm"),
                   [cylinder("ColaInt_"+sq,"ReedDiameter/2","4 mm",x,y,"CarrierZ-3 mm")])
        finish(glue,"REF | Cola quente sob MDF "+sq,"Matriz",(0.81,0.76,0.52),role="envelope")
        # Visual cells are thin overlays above the glass, excluded from physical STEP/checks.
        tile = box("Casa_"+sq,"Pitch","Pitch","0.01 mm",f"Margin+{col}*Pitch",f"Margin+{row}*Pitch","Height+0.02 mm")
        color = (0.14,0.25,0.29) if (row+col)%2 == 0 else (0.87,0.89,0.81)
        finish(tile, "VISUAL | "+sq+" - pintura ilustrativa", "Visual", color, role="visual")

doc.recompute()
for obj in helpers:
    obj.Visibility = obj in finals or obj in visual
for obj in finals + visual:
    obj.Visibility = True
sheet.Visibility = False
doc.recompute()

def check_shape(obj):
    return {"name":obj.Name,"label":obj.Label,"role":obj.Papel,
            "valid":obj.Shape.isValid(),"solids":len(obj.Shape.Solids),
            "volume_mm3":round(obj.Shape.Volume,6)}

checks = [check_shape(o) for o in finals]
errors = [o.Name for o in doc.Objects if "Invalid" in o.State]
# AABB broad phase then exact BRep common. Contacts (zero volume) are allowed.
collisions = []
pairs = 0
for i,a in enumerate(finals):
    for b in finals[i+1:]:
        aa,bb = a.Shape.BoundBox,b.Shape.BoundBox
        overlap = (min(aa.XMax,bb.XMax)-max(aa.XMin,bb.XMin),
                   min(aa.YMax,bb.YMax)-max(aa.YMin,bb.YMin),
                   min(aa.ZMax,bb.ZMax)-max(aa.ZMin,bb.ZMin))
        if min(overlap) > 1e-7:
            pairs += 1
            volume = a.Shape.common(b.Shape).Volume
            if volume > 1e-5:
                collisions.append({"a":a.Name,"b":b.Name,"intersection_mm3":round(volume,6)})

def val(name):
    p = getattr(sheet,name)
    return p.Value if hasattr(p,"Value") else float(p)

params = {name:round(val(name),6) for name,_,_ in PARAMS}

# Demonstrate expressions actually resize geometry, then restore baseline.
baseline = (base.Shape.BoundBox.XLength, doc.getObject("Reed_h8").Shape.CenterOfMass.x)
original_outer = sheet.getContents(f"B{rows['Outer']}")
sheet.set(f"B{rows['Outer']}",str(params["Outer"]+10)+" mm")
doc.recompute()
changed = (base.Shape.BoundBox.XLength,doc.getObject("Reed_h8").Shape.CenterOfMass.x)
param_ok = abs(changed[0]-baseline[0]-10)<1e-6 and abs(changed[1]-baseline[1]-5)<1e-6
sheet.set(f"B{rows['Outer']}",original_outer)
doc.recompute()

report = {"tool":"FreeCAD "+".".join(App.Version()[:3]),"units":"mm",
          "scope":"Verificacao geometrica por software; nao valida resistencia, campo magnetico ou fabricacao.",
          "parameters":params,"final_objects":len(finals),"objects_valid":all(x["valid"] for x in checks),
          "document_errors":errors,"exact_intersection_pairs":pairs,"collisions":collisions,
          "parameter_change_test":{"Outer_plus_10_mm":param_ok,"baseline":baseline,"changed":changed,"restored":True},
          "geometry":checks,
          "limitations":["Envelopes eletronicos genericos, nao footprints de fornecedor.",
                         "Roteamento de fios, cabos, soldas, terminais dobrados e ferragens nao modelados.",
                         "Rigidez da moldura e suporte, campo magnetico e ativacao de reeds exigem prototipo.",
                         "Suporte apoiado no perimetro e travessa central; avaliar flecha e rigidez por prototipo.",
                         "Vidro requer bordas lapidadas e junta compativel, sem aperto pontual.",
                         "SensorGap mede vidro ate ponta do corpo reed, nao ate contato magnetico; centro do corpo fica10mm abaixo da face inferior do vidro neste envelope. Contato real e campo nao simulados.",
                         "Prototipo de uma casa com reed vertical e ima ferrite12x3 obrigatorio antes de replicar64; envelope pode nao acionar.",
                         "Furos escareados exigem parafusos de cabeca embutida e comprimento confirmado, nao modelados."]}
report["clearances_mm"]={"control_board_to_reed_lower_leads":params["SensorTop"]-params["ReedLength"]-params["LeadBelow"]-(params["Base"]+4+18),"glass_to_body_top":params["SensorGap"],"glass_to_body_center":params["SensorGap"]+params["ReedLength"]/2}
report["limitations"].append("Folga placa/terminais calculada para envelopes retos sem solda/cabos; confirmar ocupacao real e isolacao antes da montagem. Altura60mm deixa7mm nesta referencia.")
(OUT/"verificacao_geometrica.json").write_text(json.dumps(report,indent=2,ensure_ascii=False),encoding="utf-8")
(OUT/"cores_modelo.json").write_text(json.dumps(colors,indent=2),encoding="utf-8")

with (OUT/"coordenadas_sensores.csv").open("w",newline="",encoding="utf-8-sig") as f:
    writer=csv.writer(f); writer.writerow(["casa","coluna","linha","x_mm","y_mm","z_eixo_mm","eixo_reed"])
    for row in range(8):
        for col in range(8):
            writer.writerow([chr(97+col)+str(row+1),col,row,params["Margin"]+(col+.5)*params["Pitch"],params["Margin"]+(row+.5)*params["Pitch"],params["SensorZ"],"Z"])

p=params
cutlist = [
    ["M01",1,"MDF",p['Outer'],p['Outer'],p['Base'],f"4 furos{p['BaseScrew']} escareados inferiores; conferir modelo"],
    ["M02",1,"MDF",p['Outer'],p['WallHeight'],p['Wall'],f"Frente: 3 furos{p['ButtonDiameter']} e 2 furos{p['LedDiameter']}"],
    ["M03",1,"MDF",p['Outer'],p['WallHeight'],p['Wall'],"Traseira"],
    ["M04",1,"MDF",p['Outer']-2*p['Wall'],p['WallHeight'],p['Wall'],"Lateral esquerda"],
    ["M05",1,"MDF",p['Outer']-2*p['Wall'],p['WallHeight'],p['Wall'],"Lateral direita: acesso70x20"],
    ["M06",1,"MDF",p['Outer'],p['Outer'],p['Carrier'],f"64 furos{p['ReedHole']}; canais{p['ChannelWidth']}x{p['ChannelDepth']} com coletores; pegasR10; passagem8x28;4escareados"],
    ["M07",1,"MDF",p['Outer']-2*p['Wall'],12,p['Wall'],"Travessa central entre colunas d/e sob suporte"],
    ["A01-A04",4,"MDF empilhado / madeira",p['Support'],p['Support'],p['CarrierZ']-p['Base'],f"Pilotos{p['Pilot']}x10 em faces superior/inferior"],
    ["V01",1,"Vidro existente",p['GlassSize'],p['GlassSize'],p['Glass'],"MEDIR tamanho real; bordas lapidadas; sem corte presumido"],
    ["J01",1,"Elastomero",p['Outer']-4,p['Outer']-4,p['Gasket'],f"Anel largura{p['GasketWidth']}; espessura instalada"],
]
with (OUT/"lista_corte.csv").open("w",newline="",encoding="utf-8-sig") as f:
    writer=csv.writer(f);writer.writerow(["id","quantidade","material","comprimento_mm","largura_mm","espessura_mm","observacoes"]);writer.writerows(cutlist)

# STEP excludes cosmetic overlays; includes explicitly labelled envelopes.
import Import
Import.export(finals,str(OUT/"tabuleiro_sensorial.step"))
doc.recompute()
doc.saveAs(str(ROOT/"tabuleiro_sensorial.FCStd"))

# Export triangulated actual BReps for a reproducible renderer independent of GPU.
import MeshPart
build=ROOT/"build"
build.mkdir(exist_ok=True)
meshdata=[]
for obj in finals+visual:
    mesh=MeshPart.meshFromShape(Shape=obj.Shape,LinearDeflection=0.3,AngularDeflection=0.5,Relative=False)
    pts,tris=mesh.Topology
    meshdata.append({"name":obj.Name,"label":obj.Label,"role":obj.Papel,
                     "color":colors[obj.Name]["rgb"],
                     "vertices":[[v.x,v.y,v.z] for v in pts],"triangles":tris})
(build/"malha_apresentacao.json").write_text(json.dumps(meshdata,separators=(",",":")),encoding="utf-8")
print(json.dumps({"saved":str(ROOT/"tabuleiro_sensorial.FCStd"),"valid":report["objects_valid"],"collisions":collisions,"parametric_test":param_ok,"final_objects":len(finals)},ensure_ascii=False))
if not report["objects_valid"] or errors or collisions or not param_ok:
    raise RuntimeError("Verificacoes geometricas falharam; revisar relatorio")
