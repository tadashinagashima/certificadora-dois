"""Prancha vetorial dimensionada derivada dos parametros exportados do FreeCAD."""
from pathlib import Path
import json
import math
from reportlab.graphics.shapes import Drawing, Rect, Line, String, Circle, Polygon
from reportlab.graphics import renderPDF, renderSVG
from reportlab.lib.colors import HexColor, Color
ROOT=Path(__file__).resolve().parent
OUT=ROOT/'exportacoes'
p=json.loads((OUT/'verificacao_geometrica.json').read_text())['parameters']
d=Drawing(1190,842)
INK=HexColor('#19333c');GRAY=HexColor('#60757a');TEAL=HexColor('#187d70');PALE=HexColor('#e7efea');WOOD=HexColor('#d7bd96')
def text(x,y,s,size=10,color=INK,bold=False,anchor='start'):
    d.add(String(x,y,str(s),fontName='Helvetica-Bold' if bold else 'Helvetica',fontSize=size,fillColor=color,textAnchor=anchor))
def line(x1,y1,x2,y2,color=INK,width=.6,dash=None):
    d.add(Line(x1,y1,x2,y2,strokeColor=color,strokeWidth=width,strokeDashArray=dash))
def rect(x,y,w,h,fill=None,stroke=INK,width=.6):d.add(Rect(x,y,w,h,fillColor=fill,strokeColor=stroke,strokeWidth=width))
def circle(x,y,r,fill=None,stroke=INK):d.add(Circle(x,y,r,fillColor=fill,strokeColor=stroke,strokeWidth=.5))
def dim(x1,y1,x2,y2,label,vertical=False):
    line(x1,y1,x2,y2,GRAY,.5)
    if vertical:
        for y,sgn in [(y1,1),(y2,-1)]:
            line(x1,y,x1-2,y+sgn*5,GRAY);line(x1,y,x1+2,y+sgn*5,GRAY)
        text(x1-7,(y1+y2)/2,label,9,GRAY,anchor='end')
    else:
        for x,sgn in [(x1,1),(x2,-1)]:
            line(x,y1,x+sgn*5,y1-2,GRAY);line(x,y1,x+sgn*5,y1+2,GRAY)
        text((x1+x2)/2,y1+5,label,9,GRAY,anchor='middle')
def f(n):return ('%g'%n).replace('.',',')

rect(0,0,1190,842,HexColor('#ffffff'),None)
text(36,803,'TABULEIRO DE XADREZ SENSORIAL',23,bold=True)
text(36,779,'MEC-01 / Estudo dimensional paramétrico - unidades em mm - configuração inicial de protótipo',10,GRAY)
line(36,763,1154,763,TEAL,2)
text(76,730,'A. MDF SUPERIOR SEM VIDRO / PLANTA',13,bold=True)
x0,y0=87,330
outer=p['Outer'];pitch=p['Pitch'];margin=p['Margin'];game=p['Game']
rect(x0,y0,outer,outer,HexColor('#f8f5ef'))
for edge,direction in [(x0,1),(x0+outer,-1)]:
    circle(edge,y0+outer/2,10,HexColor('#ffffff'),None)
    points=[(edge+direction*10*math.cos(-math.pi/2+i*math.pi/32),y0+outer/2+10*math.sin(-math.pi/2+i*math.pi/32)) for i in range(33)]
    for a,b in zip(points,points[1:]):line(a[0],a[1],b[0],b[1])
for i in range(8):
    pos=margin+(i+.5)*pitch
    rect(x0+pos-p['ChannelWidth']/2,y0+margin,p['ChannelWidth'],game,PALE,None)
    rect(x0+margin,y0+pos-p['ChannelWidth']/2,outer-20+p['ChannelWidth']-margin,p['ChannelWidth'],PALE,None)
rect(x0+outer-20,y0+margin,p['ChannelWidth'],game,PALE,None)
rect(x0+margin,y0+margin+game-p['ChannelWidth'],outer-20+p['ChannelWidth']-margin,p['ChannelWidth'],PALE,None)
rect(x0+outer-23,y0+outer/2-14,8,28,HexColor('#ffffff'))
for i in range(9):
    a=margin+i*pitch
    line(x0+a,y0+margin,x0+a,y0+margin+game,Color(.5,.6,.6),.3,[2,3])
    line(x0+margin,y0+a,x0+margin+game,y0+a,Color(.5,.6,.6),.3,[2,3])
for row in range(8):
    for col in range(8):circle(x0+margin+(col+.5)*pitch,y0+margin+(row+.5)*pitch,p['ReedHole']/2,INK)
for x in [p['Wall']+p['Support']/2,outer-p['Wall']-p['Support']/2]:
    for y in [p['Wall']+p['Support']/2,outer-p['Wall']-p['Support']/2]:
        circle(x0+x,y0+y,p['BaseScrew']);circle(x0+x,y0+y,p['BaseScrew']/2)
for i in range(8):
    text(x0+margin+(i+.5)*pitch,y0-15,chr(97+i),9,GRAY,anchor='middle')
    text(x0-14,y0+margin+(i+.5)*pitch-3,str(i+1),9,GRAY,anchor='middle')
text(x0+outer/2,y0-37,'LADO DAS BRANCAS / Y MÍNIMO',10,TEAL,bold=True,anchor='middle')
dim(x0,y0+outer+25,x0+outer,y0+outer+25,f(outer))
line(x0,y0+outer,x0,y0+outer+32,GRAY);line(x0+outer,y0+outer,x0+outer,y0+outer+32,GRAY)
dim(x0-35,y0,x0-35,y0+outer,f(outer),True)
dim(x0+margin,y0+outer+7,x0+margin+game,y0+outer+7,f(game)+' área de jogo')
text(485,680,'64 × Ø'+f(p['ReedHole'])+' passantes',11,bold=True)
text(485,661,'Eixos: '+f(margin+pitch/2)+' + n × '+f(pitch),10)
text(485,642,'n = 0...7, nos eixos X e Y.',10)
text(485,614,'16 canais '+f(p['ChannelWidth'])+' × '+f(p['ChannelDepth'])+' + 2 coletores',11,bold=True)
text(485,595,'Fios isolados e soldas abaixo do apoio.',10)
text(485,576,'Cruzamentos: isolar redes distintas.',10)
text(485,548,'Saída do chicote: 8 × 28',11,bold=True)
text(485,529,'X = '+f(outer-23)+'...'+f(outer-15)+' / Y = '+f(outer/2-14)+'...'+f(outer/2+14),10)
text(485,501,'4 × Ø'+f(p['BaseScrew'])+' + escareado Ø'+f(2*p['BaseScrew']),11,bold=True)
text(485,482,'Escareado 90° / profundidade '+f(p['BaseScrew']/2),10)
text(485,463,'Centros 15 mm de cada borda.',10)
text(485,435,'2 pegas laterais R10 no suporte',11,bold=True)
text(485,416,'Centro Y = '+f(outer/2)+'; vidro continua inteiro.',10)
text(485,388,'A1 = ('+f(margin+pitch/2)+'; '+f(margin+pitch/2)+')',10,TEAL,bold=True)
text(485,369,'H8 = ('+f(margin+7.5*pitch)+'; '+f(margin+7.5*pitch)+')',10,TEAL,bold=True)

text(762,730,'B. DETALHE DE UMA CASA / CORTE',13,bold=True)
# Local section enlarged 4x; cut through reed axis, schematic lead placement.
sx=825;sy=405;scale=4.0
def z(v):return sy+(v-25)*scale
rect(780,z(p['GlassZ']),160,p['Glass']*scale,HexColor('#cfe4e7'))
rect(780,z(p['CarrierZ']),160,p['Carrier']*scale,WOOD)
rect(sx-p['ReedHole']/2*scale,z(p['CarrierZ'])-.1,p['ReedHole']*scale,p['Carrier']*scale+.2,HexColor('#ffffff'),None)
rect(780,z(p['CarrierTop']-p['ChannelDepth']),160,p['ChannelDepth']*scale,HexColor('#f8faf8'),None)
rect(sx-p['ReedDiameter']/2*scale,z(p['SensorTop']-p['ReedLength']),p['ReedDiameter']*scale,p['ReedLength']*scale,HexColor('#6cb5ae'))
line(sx,z(p['SensorTop']),sx,z(p['SensorTop']+p['LeadAbove']),GRAY,2)
line(sx,z(p['SensorTop']-p['ReedLength']),sx,z(p['SensorTop']-p['ReedLength']-p['LeadBelow']),GRAY,2)
rect(sx-12,z(p['CarrierZ']-2),24,8,HexColor('#cabd87'))
rect(sx-4,z(p['CarrierZ']-2),8,8,HexColor('#6cb5ae'),None)
rect(sx-p['MagnetDiameter']/2*scale,z(p['Height']+.2),p['MagnetDiameter']*scale,p['MagnetHeight']*scale,HexColor('#555b5c'))
for yy,label in [(z(p['Height']+p['MagnetHeight']+.2),'Ímã ferrite Ø12 × 3'),(z(p['GlassZ'])+5,'Vidro '+f(p['Glass'])),
                 (z(p['CarrierTop'])-9,'Canal '+f(p['ChannelWidth'])+' × '+f(p['ChannelDepth'])),(z(p['CarrierZ'])+3,'MDF '+f(p['Carrier'])),
                 (z(p['CarrierZ']-2)-7,'Cola quente inferior'),(z(p['SensorTop']-p['ReedLength'])+3,'Reed vertical Ø2 × 14 (hipótese)')]:
    line(943,yy,965,yy,GRAY);text(971,yy-3,label,9)
dim(761,z(p['SensorTop']),761,z(p['GlassZ']),f(p['SensorGap']),True)
text(755,375,'Distância vidro / topo do corpo = '+f(p['SensorGap'])+'.',10,bold=True)
text(755,356,'Centro do corpo '+f(p['SensorGap']+p['ReedLength']/2)+' abaixo do vidro.',10)
text(755,337,'Local do contato e campo magnético NÃO simulados.',10)
text(755,318,'Folga placa / terminal reto = 7; confirmar soldas/cabos.',10)
text(755,299,'Ensaio obrigatório de uma casa antes de repetir 64.',10,TEAL,bold=True)

text(76,253,'C. FRENTE E ACESSOS',13,bold=True)
fy=163
rect(x0,fy,outer,p['Base'],WOOD)
rect(x0,fy+p['Base'],outer,p['WallHeight'],WOOD)
rect(x0,fy+p['CarrierZ'],outer,p['Carrier'],WOOD)
rect(x0,fy+p['GlassZ'],outer,p['Glass'],HexColor('#cfe4e7'))
for x in [65,175,285]:circle(x0+x,fy+p['Base']+p['WallHeight']/2,p['ButtonDiameter']/2,HexColor('#234554'))
for x in [115,235]:circle(x0+x,fy+p['Base']+p['WallHeight']/2,p['LedDiameter']/2,HexColor('#46ac8d'))
dim(x0+outer+25,fy,x0+outer+25,fy+p['Height'],f(p['Height']),True)
text(x0,fy-20,'Botões Ø12: X=65 / 175 / 285; LEDs Ø5: X=115 / 235.',9)
text(x0,fy-36,'Centro Z='+f(p['Base']+p['WallHeight']/2)+'. Abertura lateral direita 70 × 20; origem Y=140, Z=9.',9)
text(555,228,'ESTRUTURA / COTAS VERTICAIS',11,bold=True)
text(555,208,'Fundo 6; paredes 9; suporte superior 6; junta instalada 1; vidro 3.',10)
text(555,188,'Altura da caixa '+f(p['Height'])+'; altura com pés '+f(p['Height']+p['FootHeight'])+'. Travessa central entre colunas d/e.',10)
text(555,168,'Vidro nominal350 ×350: confirmar a dimensão real da peça já disponível.',10)
text(555,148,'Fixação com cabeça embutida; confirmar parafuso, comprimento e resistência do MDF.',10)
rect(36,36,1118,61,HexColor('#eef3ef'),None)
text(51,76,'ESTUDO DE PROTÓTIPO - NÃO LIBERADO PARA FABRICAÇÃO',12,TEAL,bold=True)
text(51,56,'Validado por software: sólidos, interferências dos envelopes e recomputação paramétrica. Não valida rigidez, tolerâncias nem acionamento magnético.',9)
text(1138,76,'A3 / 01',10,GRAY,bold=True,anchor='end')
text(1138,56,'Sem escala de impressão garantida',9,GRAY,anchor='end')
renderSVG.drawToFile(d,str(OUT/'prancha_dimensional.svg'))
renderPDF.drawToFile(d,str(OUT/'prancha_dimensional.pdf'))
print('Prancha SVG/PDF gerada')
