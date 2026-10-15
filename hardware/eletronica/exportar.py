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
connections={'L4':'BTN_END','L6':'BTN_CONFIRM','L7':'BTN_PROMOTE','L8':'LED_GREEN','L9':'LED_RED','L10':'BUZZER_EN','L11':'MCP_RESET','L13':'SD_CS','L14':'GND','R1':'SPI_MOSI','R2':'I2C_SCL','R5':'I2C_SDA','R6':'SPI_MISO','R7':'SPI_SCK','R14':'GND','R15':'3V3_IO'}
for p,n in connections.items():expected('A1',p,n)
for p in expected_header.keys()-connections.keys():
    assert nets[pins['A1',p]]=={('A1',p)} and 'no_connect' in pin_types['A1',p],('unused DevKit pin must be NC',p)
power_sources={p for p in nets['3V3_IO'] if 'power_out' in pin_types[p]}
assert power_sources=={('A1','R15')},('3V3 must come only from onboard regulator',power_sources)
for r,net in zip(range(11,16),['SD_CS','SPI_MOSI','SPI_MISO','SD_DAT1','SD_DAT2']):expected(f'R{r}',1,'3V3_IO');expected(f'R{r}',2,net)

# Compare actual GPIO/port functions in the netlist with the firmware constants.
cfg = (ROOT.parents[1] / "firmware/diagnostico/board_config.h").read_text(encoding="utf8")
constants = dict(re.findall(r"constexpr uint(?:8|16|32)_t (\w+) = (\d+);", cfg))
for constant, expected_net in {
    "SDA_PIN": "I2C_SDA", "SCL_PIN": "I2C_SCL", "MCP_RESET_PIN": "MCP_RESET",
    "SD_CS": "SD_CS", "SD_SCK": "SPI_SCK", "SD_MISO": "SPI_MISO", "SD_MOSI": "SPI_MOSI",
    "BUTTON_CONFIRM": "BTN_CONFIRM", "BUTTON_PROMOTE": "BTN_PROMOTE", "BUTTON_END": "BTN_END",
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
