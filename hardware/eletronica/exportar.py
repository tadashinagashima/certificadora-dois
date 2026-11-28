"""Exporta o KiCad atual e confere a pinagem de montagem contra o firmware."""
from pathlib import Path
import argparse
import itertools
import re
import subprocess
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--kicad-cli', default='kicad-cli')
args = parser.parse_args()
ROOT = Path(__file__).resolve().parent
output = ROOT / 'build'
output.mkdir(exist_ok=True)
schematic = ROOT / 'tabuleiro-sensorial.kicad_sch'

def kicad(*options):
    subprocess.run([args.kicad_cli, 'sch', *map(str, options)], check=True)

kicad('erc', schematic, '--format', 'json', '--severity-all', '--exit-code-violations', '-o', output / 'erc.json')
kicad('export', 'netlist', schematic, '--format', 'kicadxml', '-o', output / 'tabuleiro-sensorial.xml')
path = output / 'tabuleiro-sensorial.xml'
doc=ET.parse(path).getroot()
pins={}; nets={}; pin_types={}; nodes=[]
for net in doc.find('nets'):
    name=net.attrib['name']; nets[name]=set()
    for node in net.findall('node'):
        nodes.append(dict(node.attrib, net=name))
        key=(node.attrib['ref'],node.attrib['pin'])
        assert key not in pins, ('pin on two nets',key)
        pins[key]=name; nets[name].add(key)
        pin_types[key]=node.attrib.get('pintype','')
def expected(ref,pin,name):
    assert pins[(ref,str(pin))]==name,(ref,pin,pins[(ref,str(pin))],name)
components={x.attrib['ref']:x.findtext('value') for x in doc.find('components')}
assert sum(components.get(f'SW{i}')=='REED_NO' for i in range(1,65))==64
assert sum(components.get(f'D{i}')=='1N4148' for i in range(1,65))==64
for r,c in itertools.product(range(8),repeat=2):
    n=r*8+c+1
    expected(f'D{n}',2,f'ROW_{r+1}')
    expected(f'SW{n}',1,f'COL_{chr(97+c)}')
    inner=pins[(f'D{n}','1')]
    assert nets[inner]=={(f'D{n}','1'),(f'SW{n}','2')},('matrix junction',n,nets[inner])
for p,n in {21:'COL_a',22:'COL_b',23:'COL_c',24:'COL_d',25:'COL_e',26:'COL_f',28:'COL_g',8:'COL_h',27:'ROW_8',9:'3V3_IO',10:'GND',12:'I2C_SCL',13:'I2C_SDA',15:'GND',16:'GND',17:'GND',18:'MCP_RESET'}.items():expected('U1',p,n)
for p in range(1,8):expected('U1',p,f'ROW_{p}')
for rank in range(1,9):
    resistor=f'R{15+rank}'
    assert components[resistor]=='47k',('row pull-up value',resistor)
    expected(resistor,1,'3V3_IO');expected(resistor,2,f'ROW_{rank}')
    row_nodes={(f'D{8*(rank-1)+c}','2') for c in range(1,9)}
    row_nodes|={(resistor,'2'),('U1',str(rank if rank<8 else 27))}
    assert nets[f'ROW_{rank}']==row_nodes,('direct row topology',rank)
for p,n in {1:'GND',2:'BUZZER_BASE',3:'BUZZER_LOW'}.items():expected('Q1',p,n)
for p,n in {1:'3V3_IO',2:'BUZZER_LOW'}.items():expected('D67',p,n)
for ref in ('J2', 'J3'):
    for p, n in enumerate(('GND', '3V3_IO', 'OLED_SCL', 'OLED_SDA'), 1):
        expected(ref, p, n)
for net, header, pin in [('OLED_SDA', 'L6', '4'), ('OLED_SCL', 'L7', '3')]:
    assert nets[net] == {('A1', header), ('J2', pin), ('J3', pin)}, (net, nets[net])
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
connections={'L6':'OLED_SDA','L7':'OLED_SCL','L8':'LED_GREEN','L9':'LED_RED','L10':'BUZZER_EN','L11':'MCP_RESET','L14':'GND','R2':'I2C_SCL','R5':'I2C_SDA','R14':'GND','R15':'3V3_IO'}
for p,n in connections.items():expected('A1',p,n)
for p in expected_header.keys()-connections.keys():
    assert nets[pins['A1',p]]=={('A1',p)} and 'no_connect' in pin_types['A1',p],('unused DevKit pin must be NC',p)
power_sources={p for p in nets['3V3_IO'] if 'power_out' in pin_types[p]}
assert power_sources=={('A1','R15')},('3V3 must come only from onboard regulator',power_sources)

# Compare actual GPIO/port functions in the netlist with the firmware constants.
cfg = (ROOT.parents[1] / "firmware/board_config.h").read_text(encoding="utf8")
constants = dict(re.findall(r"constexpr uint(?:8|16|32)_t (\w+) = (0x[0-9a-fA-F]+|\d+);", cfg))
for const, ref, side in [('OLED_LEFT_ADDRESS', 'J2', 'esquerdo'), ('OLED_RIGHT_ADDRESS', 'J3', 'direito')]:
    address = int(constants[const], 0)
    assert components[ref] == f'OLED {side} 0x{address:02X}', (ref, const, address)

for constant, expected_net in {
    "SDA_PIN": "I2C_SDA", "SCL_PIN": "I2C_SCL", "MCP_RESET_PIN": "MCP_RESET",
    "OLED_SDA_PIN": "OLED_SDA", "OLED_SCL_PIN": "OLED_SCL",
    "LED_GREEN": "LED_GREEN", "LED_RED": "LED_RED", "BUZZER": "BUZZER_EN",
}.items():
    gpio = "GPIO" + constants[constant]
    found = [n for n in nodes if n["ref"] == "A1" and n.get("pinfunction", "").split("_")[0] == gpio]
    assert len(found) == 1 and found[0]["net"] == expected_net, constant

colbits = [int(n) for n in re.search(r"COL_BITS\[8\] = \{([^}]+)\}", cfg)[1].split(",")]
rowbits = [int(n) for n in re.search(r"ROW_BITS\[8\] = \{([^}]+)\}", cfg)[1].split(",")]
for bits, prefix, labels in [(colbits, "COL_", "abcdefgh"), (rowbits, "ROW_", "12345678")]:
    for bit, label in zip(bits, labels):
        port = "GP" + ("A" if bit < 8 else "B") + str(bit % 8)
        found = [n for n in nodes if n["ref"] == "U1" and n.get("pinfunction", "").split("_")[0] == port]
        assert len(found) == 1 and found[0]["net"] == prefix + label, port


kicad('export', 'pdf', schematic, '-o', output / 'tabuleiro-sensorial.pdf')
kicad('export', 'bom', schematic, '-o', output / 'bom.csv')
print(f'ERC, matriz, alimentacao e pinagem firmware/esquema conferidos; exportacoes em {output}')
