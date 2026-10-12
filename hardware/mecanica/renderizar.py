"""Render CPU da geometria BRep tessellada pelo FreeCAD (nao e imagem gerada por IA)."""
from pathlib import Path
import json
import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT=Path(__file__).resolve().parent
OUT=ROOT/'exportacoes'
data=json.loads((ROOT/'build/malha_apresentacao.json').read_text())
FONT=Path('C:/Windows/Fonts/segoeui.ttf')
BOLD=Path('C:/Windows/Fonts/segoeuib.ttf')
def font(n,bold=False):return ImageFont.truetype(str(BOLD if bold else FONT),n)

def render(exploded=False,underside=False):
    width,height=1450,1300
    eye=np.array([1.,-1.,-1.5 if underside else (.9 if exploded else 1.5)]);eye/=np.linalg.norm(eye)
    right=np.cross(np.array([0.,0.,1.]),eye);right/=np.linalg.norm(right)
    up=np.cross(eye,right)
    matrix=np.stack([right,-up,eye],axis=1)
    entries=[]
    for obj in data:
        name=obj['name']
        if name=='ImaReferencia':continue
        if underside and not (name=='SuporteSensores' or name.startswith(('Reed_','ReedLeads_','Diode_','Cola_'))):continue
        v=np.array(obj['vertices'],dtype=float)
        shift=0
        if exploded:
            if name=='Vidro' or name.startswith('Casa_'):shift=360
            elif name=='Junta':shift=270
            elif name=='SuporteSensores' or name.startswith(('Reed_','ReedLeads_','Diode_','Cola_')):shift=160
            elif name=='Fundo' or name.startswith('Pe'):shift=-65
        v[:,2]+=shift
        proj=v@matrix
        entries.append((obj,v,proj))
    allpoints=np.concatenate([p for o,v,p in entries])
    lo,hi=allpoints[:,:2].min(axis=0),allpoints[:,:2].max(axis=0)
    scale=min((width-100)/(hi[0]-lo[0]),(height-90)/(hi[1]-lo[1]))
    offset=np.array([(width-(hi[0]-lo[0])*scale)/2,(height-(hi[1]-lo[1])*scale)/2])-lo*scale
    canvas=np.empty((height,width,3),dtype=np.uint8);canvas[:]=[247,249,248]
    depth=np.full((height,width),-np.inf,dtype=np.float32)
    light=np.array([-0.4,-.6,1.]);light/=np.linalg.norm(light)
    for obj,v,proj in entries:
        pts=proj.copy();pts[:,:2]=pts[:,:2]*scale+offset
        base=np.array(obj['color'])*255
        for tri in obj['triangles']:
            ids=np.array(tri)
            xyz=v[ids];normal=np.cross(xyz[1]-xyz[0],xyz[2]-xyz[0]);norm=np.linalg.norm(normal)
            if norm<1e-10:continue
            normal/=norm
            # BRep tessellation winding can differ per face; choose visible side by raster depth.
            shade=.66+.34*abs(np.dot(normal,light))
            color=np.clip(base*shade,0,255).astype(np.uint8)
            a,b,c=pts[ids]
            xmin=max(0,int(np.floor(min(a[0],b[0],c[0]))));xmax=min(width-1,int(np.ceil(max(a[0],b[0],c[0]))))
            ymin=max(0,int(np.floor(min(a[1],b[1],c[1]))));ymax=min(height-1,int(np.ceil(max(a[1],b[1],c[1]))))
            if xmin>xmax or ymin>ymax:continue
            denom=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
            if abs(denom)<1e-10:continue
            xx,yy=np.meshgrid(np.arange(xmin,xmax+1)+.5,np.arange(ymin,ymax+1)+.5)
            w1=((b[1]-c[1])*(xx-c[0])+(c[0]-b[0])*(yy-c[1]))/denom
            w2=((c[1]-a[1])*(xx-c[0])+(a[0]-c[0])*(yy-c[1]))/denom
            w3=1-w1-w2
            zz=w1*a[2]+w2*b[2]+w3*c[2]
            local=depth[ymin:ymax+1,xmin:xmax+1]
            mask=(w1>=-1e-7)&(w2>=-1e-7)&(w3>=-1e-7)&(zz>local)
            local[mask]=zz[mask]
            canvas[ymin:ymax+1,xmin:xmax+1][mask]=color
    page=Image.new('RGB',(2200,1600),'#f7f9f8')
    page.paste(Image.fromarray(canvas),(20,170))
    d=ImageDraw.Draw(page)
    d.text((70,48),'TABULEIRO DE XADREZ SENSORIAL',font=font(43,True),fill='#142b35')
    subtitle='Matriz vista por baixo - reeds verticais, cola e diodos' if underside else ('Vista explodida - geometria real do modelo FreeCAD' if exploded else 'Montagem paramétrica - proposta mecânica de protótipo')
    d.text((72,110),subtitle,font=font(26),fill='#527077')
    d.line((1520,210,1520,1440),fill='#cbd6d3',width=2)
    x=1570;y=230
    notes=[('01 / VIDRO','350 × 350 × 3 mm nominal','Removível; medir a peça existente.'),
           ('02 / ÁREA DE JOGO','300 × 300 mm','64 casas de 37,5 × 37,5 mm.'),
           ('03 / SENSORES','64 reeds em posição vertical','Furos Ø2,8; corpo Ø2 × 14 provisório.'),
           ('04 / MDF SUPERIOR','350 × 350 × 6 mm','Canais 3 × 2 mm e coletores rebaixados.'),
           ('05 / ESTRUTURA','Caixa 350 × 350 × 60 mm','Paredes 9 mm; fundo removível 6 mm.'),
           ('06 / MANUTENÇÃO','Vidro, suporte e fundo removíveis','Acesso lateral USB / microSD 70 × 20.')]
    for title,main,sub in notes:
        d.text((x,y),title,font=font(23,True),fill='#1d7766');y+=39
        d.text((x,y),main,font=font(24,True),fill='#18313c');y+=38
        d.text((x,y),sub,font=font(19),fill='#53656c');y+=78
    d.rounded_rectangle((1550,1260,2160,1440),radius=15,fill='#e8eeea')
    d.text((1575,1280),'VERIFICAÇÃO POR SOFTWARE',font=font(22,True),fill='#234c40')
    d.text((1575,1322),'Sólidos válidos • zero interseções',font=font(23),fill='#234c40')
    d.text((1575,1362),'Envelopes eletrônicos aproximados.',font=font(21),fill='#53656c')
    d.text((70,1490),'ESTUDO DIMENSIONAL • NÃO LIBERADO PARA FABRICAÇÃO',font=font(23,True),fill='#80552f')
    d.text((70,1530),'Acionamento magnético, rigidez, módulos reais e tolerâncias exigem ensaio físico. Unidades: mm.',font=font(21),fill='#53656c')
    target=OUT/('matriz_inferior.png' if underside else ('vista_explodida.png' if exploded else 'montagem.png'))
    page.save(target,optimize=True)
    print(target)

if __name__=='__main__':
    render(False)
    render(True)
    render(False,True)
