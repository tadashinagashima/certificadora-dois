"""Rebuild native KiCad assembly schematics; Python 3 standard library only.

Symbol geometry is vendored in Sensory.kicad_sym from the installed KiCad 10.
No ERC settings or exclusions are changed. Coordinates below are in mil-grid units
(one unit = 2.54 mm); global labels are intentional inter-sheet connections.
"""
from pathlib import Path
import re, uuid, json, math, argparse

ROOT = Path(__file__).resolve().parents[1]
LIBDIR = Path(r'C:\Program Files\KiCad\10.0\share\kicad\symbols')
PROJECT = 'tabuleiro-sensorial'
NAMESPACE = uuid.UUID('79c8b434-ab0c-4e96-9c37-a3cf5643349a')
def uid(key): return str(uuid.uuid5(NAMESPACE, key))
def q(s): return json.dumps(str(s), ensure_ascii=False)
def mm(v): return round(v * 2.54, 5)
def fx(size=1, hide=False): return f'(effects (font (size {size} {size}))' + (' (hide yes)' if hide else '') + ')'
def block(text, start):
    level=0; string=False; esc=False
    for i in range(start,len(text)):
        c=text[i]
        if esc: esc=False; continue
        if c=='\\' and string: esc=True; continue
        if c=='"': string=not string
        if not string:
            if c=='(': level+=1
            if c==')':
                level-=1
                if not level:return text[start:i+1]
    raise ValueError('unclosed symbol')

LIB={}; PINS={}; META={}
def standard(lib,name,alias=None):
    alias=alias or name
    src=(LIBDIR/(lib+'.kicad_sym')).read_text(encoding='utf8')
    marker='(symbol '+q(name); start=src.find(marker+'\n')
    if start<0: start=src.find(marker+'\r')
    if start<0: raise KeyError(marker)
    body=block(src,start)
    ext=re.search(r'\(extends "([^"]+)"\)',body)
    if ext:
        parent=ext.group(1); pstart=src.find('(symbol '+q(parent)+'\n')
        pbody=block(src,pstart)
        # Child symbols inherit geometry and pins; use base and override properties.
        body=pbody.replace(q(parent),q(name)).replace('"'+parent+'_','"'+name+'_')
    body=body.replace(q(name),q(alias)).replace('"'+name+'_','"'+alias+'_')
    LIB[alias]=body
    pinlist=[]
    for m in re.finditer(r'\(pin (?:input|output|bidirectional|tri_state|passive|free|unspecified|power_in|power_out|open_collector|open_emitter|no_connect) ',body):
        pb=block(body,m.start()); at=re.search(r'\(at ([\d.-]+) ([\d.-]+) ([\d.-]+)\)',pb)
        number=re.search(r'\(number "([^"]+)"',pb).group(1)
        pinlist.append((number,float(at[1])/2.54,-float(at[2])/2.54,float(at[3])))
    PINS[alias]=pinlist
    return alias

def custom(name,ref,pins,width,height,description):
    # Pins: number, label, type, x, y, angle. y grows downward as on sheet.
    shapes=f'(rectangle (start {-mm(width)} {mm(height)}) (end {mm(width)} {-mm(height)}) (stroke (width 0.254) (type default)) (fill (type background)))'
    ps=[]
    for num,label,typ,x,y,ang in pins:
        ps.append(f'(pin {typ} line (at {mm(x)} {-mm(y)} {ang}) (length 2.54) (name {q(label)} {fx(1.27)}) (number {q(num)} {fx(1.0)}))')
    LIB[name]=f'''(symbol {q(name)} (pin_names (offset 0.5)) (in_bom yes) (on_board yes)
      (property "Reference" {q(ref)} (at 0 {mm(height+2)} 0) {fx()})
      (property "Value" {q(name)} (at 0 {mm(height+1)} 0) {fx()})
      (property "Footprint" "" (at 0 0 0) {fx(hide=True)})
      (property "Datasheet" "" (at 0 0 0) {fx(hide=True)})
      (property "Description" {q(description)} (at 0 0 0) {fx(hide=True)})
      (symbol "{name}_0_1" {shapes}) (symbol "{name}_1_1" {' '.join(ps)}))'''
    PINS[name]=[(str(n),x,y,a) for n,l,t,x,y,a in pins]

def main():
    for lib,name,alias in [('Device','R','R'),('Device','C','C'),('Device','D','D'),('Device','LED','LED'),('Switch','SW_Reed','Reed'),('Switch','SW_Push','Button'),('Transistor_BJT','2N3904','NPN'),('power','PWR_FLAG','PWR_FLAG'),('Interface_Expansion','MCP23017x-x-SO','MCP23017')]:
        standard(lib,name,alias)
    
    left=['EN','GPIO36','GPIO39','GPIO34','GPIO35','GPIO32','GPIO33','GPIO25','GPIO26','GPIO27','GPIO14','GPIO12','GPIO13','GND','VIN']
    right=['GPIO23','GPIO22','GPIO1','GPIO3','GPIO21','GPIO19','GPIO18','GPIO5','GPIO17','GPIO16','GPIO4','GPIO2','GPIO15','GND','3V3']
    usedout={'GPIO25','GPIO26','GPIO27','GPIO14','GPIO13','GPIO23','GPIO18','GPIO22'}
    usedin={'EN','GPIO36','GPIO39','GPIO34','GPIO35','GPIO32','GPIO33','GPIO19'}
    dev=[]
    for side,ls in [('L',left),('R',right)]:
        for i,n in enumerate(ls):
            typ='power_out' if n=='3V3' else 'power_in' if n in ('GND','VIN') else 'output' if n in usedout else 'input' if n in usedin else 'bidirectional'
            dev.append((f'{side}{i+1}',n,typ,-10 if side=='L' else 10,-14+2*i,0 if side=='L' else 180))
    custom('ESP32_DevKit_V1_30pin','A',dev,9,15,'DevKit V1 / ESP-WROOM-32, 30 pinos. Vista dos componentes, antena acima e USB abaixo. L/R: posições do projeto, de cima para baixo; não são números do módulo WROOM.')
    sd=[(1,'DAT3/CS','input',-8,-8,0),(2,'CMD/MOSI','input',-8,-5,0),(3,'VSS1','power_in',-8,-2,0),(4,'VDD_3V3','power_in',-8,1,0),(5,'CLK','input',-8,4,0),(6,'VSS2','power_in',-8,7,0),(7,'DAT0/MISO','tri_state',8,-8,180),(8,'DAT1','bidirectional',8,-5,180),(9,'DAT2','bidirectional',8,-2,180)]
    custom('SD_adapter_passive','J',sd,7,10,'Adaptador SD passivo: numeração dos contatos SD 1 a 9, não dos contatos microSD.')
    custom('OLED_optional','J',[(1,'GND','power_in',-6,-3,0),(2,'3V3','power_in',-6,-1,0),(3,'SCL','input',-6,1,0),(4,'SDA','bidirectional',-6,3,0)],5,5,'Interface lógica do OLED SSD1315; ordem física do conector conforme o módulo utilizado.')
    custom('Buzzer','BZ',[(1,'+','passive',-4,0,0),(2,'-','passive',4,0,180)],3,2,'Buzzer ativo, 3,3 V, corrente máxima de 30 mA.')
    
    rootid=uid('root'); COMPONENTS=[]
    class Sheet:
        def __init__(self,name,title,page,child=None):
            self.name=name; self.title=title; self.page=page; self.id=rootid if page==1 else uid(name); self.path='/'+rootid+('/'+uid(child) if child else ''); self.items=[]; self.syms=set(); self.n=0
        def add(self,s):self.items.append(s)
        def ident(self,k):self.n+=1; return uid(self.name+':'+k+':'+str(self.n))
        def text(self,s,x,y,size=1.8):self.add(f'(text {q(s)} (at {mm(x)} {mm(y)} 0) (effects (font (size {size} {size})) (justify left top)) (uuid {self.ident("text")}))')
        def wire(self,a,b):self.add(f'(wire (pts (xy {mm(a[0])} {mm(a[1])}) (xy {mm(b[0])} {mm(b[1])})) (stroke (width 0) (type default)) (uuid {self.ident("wire")}))')
        def label(self,n,p,ang=0):
            self.add(f'(global_label {q(n)} (shape bidirectional) (at {mm(p[0])} {mm(p[1])} {ang}) (effects (font (size 1.27 1.27)) (justify {"right" if ang==0 else "left"})) (uuid {self.ident("label")}))')
        def nc(self,p):self.add(f'(no_connect (at {mm(p[0])} {mm(p[1])}) (uuid {self.ident("nc")}))')
        def symbol(self,lib,ref,val,x,y,ang=0,nets=None,foot='',note='',props_at=None,dnp=False):
            self.syms.add(lib); a=math.radians(ang); pins={}
            for num,px,py,pa in PINS[lib]:
                pins[num]=(round(x+px*math.cos(a)+py*math.sin(a),5),round(y-px*math.sin(a)+py*math.cos(a),5), (pa+ang)%360)
            prop=props_at or (x+3,y-2)
            inst=f'''(symbol (lib_id "Sensory:{lib}") (at {mm(x)} {mm(y)} {ang}) (unit 1) (in_bom yes) (on_board yes) (dnp {"yes" if dnp else "no"}) (uuid {uid(ref)})
              (property "Reference" {q(ref)} (at {mm(prop[0])} {mm(prop[1])} {ang}) {fx(1.27)})
              (property "Value" {q(val)} (at {mm(prop[0])} {mm(prop[1]+1.3)} {ang}) {fx(1.27)})
              (property "Footprint" {q(foot)} (at {mm(x)} {mm(y)} 0) {fx(hide=True)})
              (property "Assembly_note" {q(note)} (at {mm(x)} {mm(y)} 0) {fx(hide=True)})
              (instances (project {q(PROJECT)} (path {q(self.path)} (reference {q(ref)}) (unit 1)))))'''
            self.add(inst); COMPONENTS.append({'reference':ref,'value':val,'symbol':lib,'footprint':foot,'note':note,'dnp':dnp,'sheet':self.name})
            if nets is not None:
                for num,(px,py,pa) in pins.items():
                    n=nets.get(num)
                    if n is None:self.nc((px,py));continue
                    dx=-3 if pa==0 else 3 if pa==180 else 0;dy=3 if pa==90 else -3 if pa==270 else 0
                    p=(px+dx,py+dy); self.wire((px,py),p);self.label(n,p,180 if dx>0 else 0)
            return pins
        def child(self,name,title,x,y,page):
            sid=uid(name)
            self.add(f'''(sheet (at {mm(x)} {mm(y)}) (size 38.1 15.24) (stroke (width 0.254) (type default)) (fill (color 0 0 0 0)) (uuid {sid})
              (property "Sheetname" {q(title)} (at {mm(x)} {mm(y-1)} 0) (effects (font (size 1.27 1.27)) (justify left bottom)))
              (property "Sheetfile" {q(name+'.kicad_sch')} (at {mm(x)} {mm(y+7)} 0) (effects (font (size 1 1)) (justify left top)))
              (instances (project {q(PROJECT)} (path {q('/'+rootid)} (page {q(page)})))))''')
        def save(self):
            libs='\n'.join(LIB[k].replace('(symbol '+q(k),'(symbol '+q('Sensory:'+k),1) for k in sorted(self.syms))
            content=f'''(kicad_sch (version 20250114) (generator "eeschema") (uuid {self.id}) (paper "A3")
              (title_block (title {q(self.title)}) (date "2026-10-11") (rev "B") (company "UTFPR - Engenharia de Computação")
                (comment 1 "Tabuleiro de Xadrez Sensorial"))
              (lib_symbols {libs}) {' '.join(self.items)} (embedded_fonts no))'''
            (ROOT/(self.name+'.kicad_sch')).write_text(content,encoding='utf8')
    
    main=Sheet(PROJECT,'Controle e alimentação USB',1)
    main.text('CONTROLE E ALIMENTAÇÃO',8,6,2.5)
    main.text('Alimentação pelo USB do computador; periféricos pelo 3V3 da DevKit.',8,11)
    main.text('CONTROLADOR',8,21,2.0)
    main.text('ENTRADAS DA MATRIZ / PULL-UPS',66,18,2.0)
    devnets={'GPIO21':'I2C_SDA','GPIO22':'I2C_SCL','GPIO18':'SPI_SCK','GPIO19':'SPI_MISO','GPIO23':'SPI_MOSI','GPIO13':'SD_CS','GPIO32':'BTN_CONFIRM','GPIO33':'BTN_PROMOTE','GPIO34':'BTN_END','GPIO25':'LED_GREEN','GPIO26':'LED_RED','GPIO27':'BUZZER_EN','GPIO14':'MCP_RESET','GND':'GND','3V3':'3V3_IO'}
    main.symbol('ESP32_DevKit_V1_30pin','A1','ESP32 DEVKIT V1 / 30 pinos',33,45,nets={p:devnets.get(n) for p,n,t,x,y,a in dev},props_at=(33,27),note='Alimentação pelo USB do computador; 3V3 da placa alimenta os periféricos. L/R: posições do projeto, de cima para baixo, na vista dos componentes com antena acima e USB abaixo.')
    main.text('A1: vista dos componentes, antena acima e USB abaixo.\nL1-L15 e R1-R15: posições do projeto, de cima para baixo.\nGPIO34: entrada sem pull-up interno.',8,64)
    for i in range(8):
        main.symbol('R',f'R{16+i}','47k',75+23*(i%4),28+16*(i//4),nets={'1':'3V3_IO','2':f'ROW_{i+1}'},note='Pull-up externo da entrada de matriz. Pull-ups internos do MCP desabilitados.')
    main.text('ROW_1-ROW_8: entradas ativas LOW.\nUma coluna LOW por vez; demais em alta impedância.',66,51)
    main.text('EXPANSOR I²C',66,56,2.0)
    main.symbol('PWR_FLAG','#FLG01','PWR_FLAG',75,61,nets={'1':'GND'},props_at=(75,58))
    mcpnets={'9':'3V3_IO','10':'GND','12':'I2C_SCL','13':'I2C_SDA','15':'GND','16':'GND','17':'GND','18':'MCP_RESET'}
    for i in range(6):mcpnets[str(21+i)]=f'COL_{chr(97+i)}'
    mcpnets.update({'28':'COL_g','8':'COL_h','27':'ROW_8'})
    for i in range(7):mcpnets[str(i+1)]=f'ROW_{i+1}'
    main.symbol('MCP23017','U1','MCP23017-E/SS / CJMCU-2317',102,79,nets=mcpnets,props_at=(102,60),note='Módulo CJMCU-2317. Numeração do CI SSOP28, não dos headers do módulo.')
    for i,(net,val,x) in enumerate([('I2C_SDA','4.7k',64),('I2C_SCL','4.7k',77),('MCP_RESET','10k',134)]):main.symbol('R',f'R{i+1}',val,x,72,nets={'1':'3V3_IO','2':net},note='Não duplicar pull-up equivalente já presente no módulo.')
    main.symbol('C','C4','100nF',137,88,nets={'1':'3V3_IO','2':'GND'},note='Desacoplamento junto a VDD/VSS do MCP; pode estar incorporado ao módulo.')
    main.text('U1 / I²C: endereço 0x20.\nGPA7 e GPB7 usados somente como saídas.\nOLAT=0; seleção de coluna por IODIR; GPPU=0.',8,79)
    main.text('U1: numeração do CI SSOP28, não dos headers do módulo.\nNão duplicar pull-ups e desacoplamento já presentes nos módulos.\nC4 junto a VDD/VSS.',65,97)
    for i,(name,title) in enumerate([('perifericos','02 / Interface e SD'),('matriz','03 / Matriz 8x8')]):main.child(name,title,8+i*43,103,i+2)
    
    io=Sheet('perifericos','Interface e armazenamento',2,'perifericos')
    io.text('INTERFACE E ARMAZENAMENTO / 3,3 V',8,6,2.5)
    io.text('COMANDOS',8,11,2.0)
    io.text('CARTÃO SD / SPI',118,10,2.0)
    for i,(n,title) in enumerate([('BTN_CONFIRM','CONFIRMAR'),('BTN_PROMOTE','PROMOVER'),('BTN_END','ENCERRAR')]):
        x=22+i*34
        io.symbol('R',f'R{4+i}','10k',x,19,nets={'1':'3V3_IO','2':n})
        io.symbol('Button',f'SW{65+i}',title,x,32,nets={'1':n,'2':'GND'},props_at=(x,27))
        io.symbol('C',f'C{5+i}','100nF',x,43,nets={'1':n,'2':'GND'})
    io.text('SINALIZAÇÃO VISUAL',8,60,2.0)
    io.text('ACIONAMENTO DO BUZZER',68,55,2.0)
    for i,(n,val) in enumerate([('LED_GREEN','VERDE'),('LED_RED','VERMELHO')]):
        y=68+i*16
        io.symbol('R',f'R{7+i}','1k',23,y,ang=90,nets={'1':n,'2':f'LED_{i}_A'},props_at=(23,y-4))
        io.symbol('LED',f'D{65+i}',val,46,y,nets={'1':'GND','2':f'LED_{i}_A'},props_at=(46,y-4))
    io.symbol('NPN','Q1','2N3904',82,74,nets={'1':'GND','2':'BUZZER_BASE','3':'BUZZER_LOW'},foot='Package_TO_SOT_THT:TO-92_Inline',props_at=(91,73))
    io.symbol('R','R9','680R',70,66,ang=90,nets={'1':'BUZZER_EN','2':'BUZZER_BASE'},props_at=(70,61))
    io.symbol('R','R10','100k',70,85,nets={'1':'BUZZER_BASE','2':'GND'})
    io.symbol('Buzzer','BZ1','3.3V ativo <=30mA',108,65,nets={'1':'3V3_IO','2':'BUZZER_LOW'},props_at=(108,59))
    io.symbol('D','D67','1N4148',109,83,nets={'1':'3V3_IO','2':'BUZZER_LOW'},props_at=(109,78))
    io.text('Q1: 1=emissor, 2=base, 3=coletor.',68,94)
    io.symbol('SD_adapter_passive','J1','SD PASSIVO / microSD 8GB',132,28,nets={'1':'SD_CS','2':'SPI_MOSI','3':'GND','4':'3V3_IO','5':'SPI_SCK','6':'GND','7':'SPI_MISO','8':'SD_DAT1','9':'SD_DAT2'},props_at=(132,16),note='Adaptador SD convencional passivo; numeração dos contatos SD 1 a 9; alimentação de 3,3 V.')
    for i,(net,x,y) in enumerate([('SD_CS',122,48),('SPI_MOSI',137,48),('SPI_MISO',152,48),('SD_DAT1',135,63),('SD_DAT2',150,63)]):io.symbol('R',f'R{11+i}','10k',x,y,nets={'1':'3V3_IO','2':net})
    io.symbol('C','C8','100nF',137,80,nets={'1':'3V3_IO','2':'GND'},props_at=(137,72))
    io.symbol('C','C9','47uF / 6.3V',152,80,nets={'1':'3V3_IO','2':'GND'},props_at=(152,72),note='Se polarizado: positivo em 3V3_IO e negativo em GND. Instalar junto ao adaptador SD.')
    io.text('J1: numeração dos contatos do adaptador SD.\nC8/C9 junto ao adaptador. Se C9 polarizado: + em 3V3_IO.',119,91,1.6)
    io.text('OLED / OPCIONAL',8,97,2.0)
    io.symbol('OLED_optional','J2','OLED SSD1315 opcional',30,103,nets={'1':'GND','2':'3V3_IO','3':'I2C_SCL','4':'I2C_SDA'},props_at=(46,100),dnp=True,note='Interface lógica; ordem física do conector conforme módulo. DNP: não montado.')
    io.text('J2: interface lógica; ordem física conforme módulo.',44,105,1.6)
    
    ma=Sheet('matriz','Matriz de sensores 8x8',3,'matriz')
    ma.text('MATRIZ 8x8 / SENSORES DE OCUPAÇÃO',8,6,2.5)
    ma.text('Faixa dos diodos voltada ao reed/coluna. Reeds montados na vertical.',8,11)
    for r in range(8):
        for c in range(8):
            n=r*8+c+1;x=15+c*19;y=25+r*10; house=f'{chr(97+c)}{r+1}'
            ma.text(house,x-5,y-5,1.6)
            # D cathode at left, anode at right; reed to left and ROW to right.
            d=ma.symbol('D',f'D{n}','1N4148',x+4,y,props_at=(x+4,y-3),note=f'Casa {house}: cátodo voltado ao reed/coluna.')
            s=ma.symbol('Reed',f'SW{n}','REED_NO',x-3,y,props_at=(x-3,y-3),note=f'Casa {house}: reed vertical, normalmente aberto.')
            # Native standard symbols have D1=K,D2=A and reed1/2 passive.
            dp={k:v[:2] for k,v in d.items()};sp={k:v[:2] for k,v in s.items()}
            ma.wire(dp['1'],sp['2']);ma.wire(sp['1'],(x-6,y));ma.wire((x-6,y),(x-6,y+3));ma.label(f'COL_{chr(97+c)}',(x-6,y+3),180)
            ma.wire(dp['2'],(x+6,y));ma.wire((x+6,y),(x+6,y+3));ma.label(f'ROW_{r+1}',(x+6,y+3),0)
    
    for sheet in [main,io,ma]:sheet.save()
    (ROOT/'Sensory.kicad_sym').write_text('(kicad_symbol_lib (version 20241209) (generator "kicad_symbol_editor")\n'+'\n'.join(LIB.values())+'\n)',encoding='utf8')
    (ROOT/'sym-lib-table').write_text('(sym_lib_table (version 7) (lib (name "Sensory")(type "KiCad")(uri "${KIPRJMOD}/Sensory.kicad_sym")(options "")(descr "Versioned project assembly symbols")))',encoding='utf8')
    (ROOT/(PROJECT+'.kicad_pro')).write_text(json.dumps({'meta':{'filename':PROJECT+'.kicad_pro','version':1},'erc':{'erc_exclusions':[]},'text_variables':{}},indent=2),encoding='utf8')
    (ROOT/'reports'/'components.json').write_text(json.dumps(COMPONENTS,ensure_ascii=False,indent=2),encoding='utf8')
    print(f'Generated 3 KiCad sheets, {len(COMPONENTS)} symbol instances, local library and project; no ERC exclusions.')
    
    
    

if __name__ == '__main__':
    main()

