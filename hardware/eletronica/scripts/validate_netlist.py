"""Independent electrical connectivity checks against the KiCad-exported XML.

Does not import the generator. IC pins follow component datasheets; DevKit V1
headers follow the documented 30-pin assembly reference. Directed graph checks
diode ghost paths, not analog thresholds, magnetic behavior or timing.
"""
from pathlib import Path
import xml.etree.ElementTree as ET
import itertools, json, hashlib, random
ROOT=Path(__file__).resolve().parents[1]
path=ROOT/'exports'/'tabuleiro-sensorial.xml'
doc=ET.parse(path).getroot()
pins={}; nets={}; pin_types={}; count=0
for net in doc.find('nets'):
    name=net.attrib['name']; nets[name]=set()
    for node in net.findall('node'):
        key=(node.attrib['ref'],node.attrib['pin'])
        assert key not in pins, ('pin on two nets',key)
        pins[key]=name; nets[name].add(key)
        pin_types[key]=node.attrib.get('pintype','')
def expected(ref,pin,name):
    global count
    assert pins[(ref,str(pin))]==name,(ref,pin,pins[(ref,str(pin))],name)
    count+=1
components={x.attrib['ref']:x.findtext('value') for x in doc.find('components')}
assert sum(components.get(f'SW{i}')=='REED_NO' for i in range(1,65))==64
assert sum(components.get(f'D{i}')=='1N4148' for i in range(1,65))==64
for r,c in itertools.product(range(8),repeat=2):
    n=r*8+c+1
    expected(f'D{n}',2,f'ROW_{r+1}')
    expected(f'SW{n}',1,f'COL_{chr(97+c)}')
    inner=pins[(f'D{n}','1')]
    assert nets[inner]=={(f'D{n}','1'),(f'SW{n}','2')},('matrix junction',n,nets[inner])
    count+=1
for p,n in {21:'COL_a',22:'COL_b',23:'COL_c',24:'COL_d',25:'COL_e',26:'COL_f',28:'COL_g',8:'COL_h',27:'ROW_8',9:'3V3_IO',10:'GND',12:'I2C_SCL',13:'I2C_SDA',15:'GND',16:'GND',17:'GND',18:'MCP_RESET'}.items():expected('U1',p,n)
for p in range(1,8):expected('U1',p,f'ROW_{p}')
for rank in range(1,9):
    resistor=f'R{15+rank}'
    assert components[resistor]=='47k',('row pull-up value',resistor)
    expected(resistor,1,'3V3_IO');expected(resistor,2,f'ROW_{rank}')
    # Exactly eight diode anodes, one external pull-up and one MCP input per row.
    row_nodes={(f'D{8*(rank-1)+c}','2') for c in range(1,9)}
    row_nodes|={(resistor,'2'),('U1',str(rank if rank<8 else 27))}
    assert nets[f'ROW_{rank}']==row_nodes,('direct row topology',rank)
    count+=2
assert not any(v.startswith(('TLV3704','MCP1826S')) for v in components.values()),'Removed optional IC still in BOM'
assert not any(n.startswith(('SENSE_','VREF')) or n=='USB_5V' for n in nets),'Obsolete supply or conditioning net'
assert not {'U2','U3','U4'} & components.keys(),'Auxiliary conditioning/regulator remains'
count+=3
for p,n in {1:'GND',2:'BUZZER_BASE',3:'BUZZER_LOW'}.items():expected('Q1',p,n)
for p,n in {1:'3V3_IO',2:'BUZZER_LOW'}.items():expected('D67',p,n)
for p,n in {1:'SD_CS',2:'SPI_MOSI',3:'GND',4:'3V3_IO',5:'SPI_SCK',6:'GND',7:'SPI_MISO',8:'SD_DAT1',9:'SD_DAT2'}.items():expected('J1',p,n)
# Front/component view, USB down. L/R are assembly coordinates, not GPIO numbers.
headers={
    'L':['EN','GPIO36','GPIO39','GPIO34','GPIO35','GPIO32','GPIO33','GPIO25','GPIO26','GPIO27','GPIO14','GPIO12','GPIO13','GND','VIN'],
    'R':['GPIO23','GPIO22','GPIO1','GPIO3','GPIO21','GPIO19','GPIO18','GPIO5','GPIO17','GPIO16','GPIO4','GPIO2','GPIO15','GND','3V3'],
}
expected_header={f'{side}{i}':name for side,names in headers.items() for i,name in enumerate(names,1)}
source=doc.find("./components/comp[@ref='A1']/libsource")
part=doc.find(f"./libparts/libpart[@lib='{source.get('lib')}'][@part='{source.get('part')}']")
actual_header={p.get('num'):p.get('name') for p in part.findall('./pins/pin')}
assert len(part.findall('./pins/pin'))==30 and actual_header==expected_header,('DevKit 30-pin header',actual_header)
count+=30
connections={'L4':'BTN_END','L6':'BTN_CONFIRM','L7':'BTN_PROMOTE','L8':'LED_GREEN','L9':'LED_RED','L10':'BUZZER_EN','L11':'MCP_RESET','L13':'SD_CS','L14':'GND','R1':'SPI_MOSI','R2':'I2C_SCL','R5':'I2C_SDA','R6':'SPI_MISO','R7':'SPI_SCK','R14':'GND','R15':'3V3_IO'}
for p,n in connections.items():expected('A1',p,n)
for p in expected_header.keys()-connections.keys():
    assert nets[pins['A1',p]]=={('A1',p)} and 'no_connect' in pin_types['A1',p],('unused DevKit pin must be NC',p)
    count+=1
power_sources={p for p in nets['3V3_IO'] if 'power_out' in pin_types[p]}
assert power_sources=={('A1','R15')},('3V3 must come only from onboard regulator',power_sources)
count+=1
for r,net in zip(range(11,16),['SD_CS','SPI_MOSI','SPI_MISO','SD_DAT1','SD_DAT2']):expected(f'R{r}',1,'3V3_IO');expected(f'R{r}',2,net)
# Model actual XML diode direction A(pin2) -> K(pin1), reeds closed bidirectional.
def scan(occupied,column):
    reverse={}
    def edge(a,b):reverse.setdefault(b,set()).add(a)
    for i in occupied:
        d=f'D{i+1}';s=f'SW{i+1}'
        edge(pins[d,'2'],pins[d,'1'])
        edge(pins[s,'1'],pins[s,'2']);edge(pins[s,'2'],pins[s,'1'])
    reached={f'COL_{chr(97+column)}'};todo=list(reached)
    while todo:
        for n in reverse.get(todo.pop(),set())-reached:reached.add(n);todo.append(n)
    return {r for r in range(8) if f'ROW_{r+1}' in reached}
patterns=0
for rows in itertools.combinations(range(8),2):
    for cols in itertools.combinations(range(8),2):
        cells=[r*8+c for r in rows for c in cols]
        for mask in range(16):
            occupied={n for i,n in enumerate(cells) if mask&(1<<i)}
            for c in cols:assert scan(occupied,c)=={n//8 for n in occupied if n%8==c}
            patterns+=1
rng=random.Random(20261011)
for _ in range(256):
    occupied={i for i in range(64) if rng.getrandbits(1)}
    for c in range(8):assert scan(occupied,c)=={n//8 for n in occupied if n%8==c}
    patterns+=1
erc=json.loads((ROOT/'reports'/'erc.json').read_text())
violations=sum(len(s.get('violations',[])) for s in erc.get('sheets',[]))
assert violations==0,erc
report={'date':'2026-10-11','revision':'B','status':'PASS','pin_or_junction_checks':count,'reed_switches':64,'matrix_diodes':64,'row_pullups_ohms':47000,'supply':'USB computer -> DevKit V1 onboard regulator -> 3V3_IO','directed_graph_occupancy_patterns':patterns,'erc_violations':violations,'erc_builtin_ignored_checks':erc.get('ignored_checks',[]),'netlist_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'limitations':['Ideal diode graph, no SPICE or guaranteed analog margin','No assembled-matrix electrical, power, SD or buzzer tests performed','DevKit header reference requires board markings check; MCP/OLED headers represented by logical interfaces']}
(ROOT/'reports'/'connectivity.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print(json.dumps(report,indent=2))
