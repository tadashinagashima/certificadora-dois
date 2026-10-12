"""Confere os contratos entre netlist KiCad, firmware e coordenadas CAD.

Python padrao; nao importa geradores nem modifica desenhos. Rodar apos exportar
a netlist e regenerar o CAD. Nao e simulacao eletrica ou teste no ESP32.
"""
from pathlib import Path
from datetime import datetime, timezone
import csv
import hashlib
import json
import re
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
ELEC = ROOT / "hardware/eletronica"
MECH = ROOT / "hardware/mecanica/exportacoes"
checks = []


def check(name, condition):
    checks.append({"check": name, "passed": bool(condition)})


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


netfile = ELEC / "exports/tabuleiro-sensorial.xml"
xml = ET.parse(netfile).getroot()
net_by_pin = {}
nodes = []
for net in xml.findall("./nets/net"):
    for node in net.findall("node"):
        attrs = dict(node.attrib)
        attrs["net"] = net.get("name")
        nodes.append(attrs)
        key = (node.get("ref"), node.get("pin"))
        check("Pino em uma unica rede: " + "/".join(key), key not in net_by_pin)
        net_by_pin[key] = attrs["net"]


def net(ref, pin):
    return net_by_pin.get((ref, str(pin)))


cfg = (ROOT / "firmware/diagnostico/board_config.h").read_text(encoding="utf8")
constants = dict(re.findall(r"constexpr uint(?:8|16|32)_t (\w+) = (\d+);", cfg))
for constant, expected_net in {
    "SDA_PIN": "I2C_SDA", "SCL_PIN": "I2C_SCL", "MCP_RESET_PIN": "MCP_RESET",
    "SD_CS": "SD_CS", "SD_SCK": "SPI_SCK", "SD_MISO": "SPI_MISO", "SD_MOSI": "SPI_MOSI",
    "BUTTON_CONFIRM": "BTN_CONFIRM", "BUTTON_PROMOTE": "BTN_PROMOTE", "BUTTON_END": "BTN_END",
    "LED_GREEN": "LED_GREEN", "LED_RED": "LED_RED", "BUZZER": "BUZZER_EN",
}.items():
    gpio = "GPIO" + constants[constant]
    found = [n for n in nodes if n["ref"] == "A1" and n.get("pinfunction", "").split("_")[0] == gpio]
    check("ESP32 firmware/esquema " + constant, len(found) == 1 and found[0]["net"] == expected_net)

colbits = [int(n) for n in re.search(r"COL_BITS\[8\] = \{([^}]+)\}", cfg)[1].split(",")]
rowbits = [int(n) for n in re.search(r"ROW_BITS\[8\] = \{([^}]+)\}", cfg)[1].split(",")]
for bits, prefix, labels in [(colbits, "COL_", "abcdefgh"), (rowbits, "ROW_", "12345678")]:
    for bit, label in zip(bits, labels):
        port = "GP" + ("A" if bit < 8 else "B") + str(bit % 8)
        found = [n for n in nodes if n["ref"] == "U1" and n.get("pinfunction", "").split("_")[0] == port]
        check("MCP firmware/esquema " + port, len(found) == 1 and found[0]["net"] == prefix + label)
check("MCP 16 portas distintas", sorted(colbits + rowbits) == list(range(16)))
check("GPA7/GPB7 reservados para colunas", 7 in colbits and 15 in colbits)
check("Acomodacao inicial da matriz direta: 300 us", constants.get("COLUMN_SETTLE_US") == "300")

coords_file = MECH / "coordenadas_sensores.csv"
coords = list(csv.DictReader(coords_file.open(encoding="utf-8-sig", newline="")))
check("64 coordenadas CAD", len(coords) == 64)
check("64 casas CAD distintas", len({r["casa"] for r in coords}) == 64)
for index, row in enumerate(coords):
    file, rank = index % 8, index // 8
    square = chr(97 + file) + str(rank + 1)
    check("CAD orientacao " + square, row["casa"] == square and int(row["coluna"]) == file and int(row["linha"]) == rank)
    check("CAD dimensional " + square, abs(float(row["x_mm"]) - (43.75 + 37.5 * file)) < 1e-6 and abs(float(row["y_mm"]) - (43.75 + 37.5 * rank)) < 1e-6 and row["eixo_reed"] == "Z")
    d, sw = "D" + str(index + 1), "SW" + str(index + 1)
    check("Matriz/CAD " + square, net(d, 2) == "ROW_" + str(rank + 1) and net(sw, 1) == "COL_" + chr(97 + file) and net(d, 1) is not None and net(d, 1) == net(sw, 2))

components = {c.get("ref"): c.findtext("value") for c in xml.findall("./components/comp")}
check("64 Reed Switches + 3 botoes", len([r for r in components if re.fullmatch(r"SW\d+", r)]) == 67)
check("64 diodos da matriz", all(components.get("D" + str(i)) == "1N4148" for i in range(1, 65)))
check("Q1 emissor/base/coletor", net("Q1", 1) == "GND" and net("Q1", 2) == "BUZZER_BASE" and net("Q1", 3) == "BUZZER_LOW")
check("SD adaptador tamanho normal pinos", all(net("J1", p) == n for p, n in {1: "SD_CS", 2: "SPI_MOSI", 3: "GND", 4: "3V3_IO", 5: "SPI_SCK", 6: "GND", 7: "SPI_MISO"}.items()))
for rank in range(1, 9):
    ref = "R" + str(15 + rank)
    check("Pull-up direto linha " + str(rank), components.get(ref) == "47k" and net(ref, 1) == "3V3_IO" and net(ref, 2) == "ROW_" + str(rank))
check("Sem comparadores ou regulador auxiliar", not any(v.startswith(("TLV3704", "MCP1826S")) for v in components.values()) and not {"U2", "U3", "U4"} & components.keys())
check("Sem redes obsoletas", not any(n.startswith(("SENSE_", "VREF")) or n == "USB_5V" for n in net_by_pin.values()))
check("3V3 do regulador onboard", net("A1", "R15") == "3V3_IO")
check("Dois GND da DevKit conectados", net("A1", "L14") == "GND" and net("A1", "R14") == "GND")
vin = [n for n in nodes if n["ref"] == "A1" and n["pin"] == "L15"]
check("VIN sem conexao externa", len(vin) == 1 and "no_connect" in vin[0].get("pintype", "") and sum(n["net"] == vin[0]["net"] for n in nodes) == 1)
header = xml.find("./components/comp[@ref='A1']/units/unit/pins")
expected_header_ids = {side + str(i) for side in "LR" for i in range(1, 16)}
check("DevKit V1: 30 pinos L1..L15/R1..R15", header is not None and len(header) == 30 and {p.get("num") for p in header} == expected_header_ids)

bom_file = ELEC / "exports/bom.csv"
bom = list(csv.DictReader(bom_file.open(encoding="utf-8-sig", newline="")))
bom_components = {}
for row in bom:
    refs = [r.strip() for r in row["Refs"].split(",")]
    check("Quantidade BOM " + row["Refs"], len(refs) == int(row["Qty"]))
    for ref in refs:
        check("Referencia BOM unica " + ref, ref not in bom_components)
        bom_components[ref] = row["Value"]
check("BOM corresponde integralmente a netlist", bom_components == components)

compiled_file = ROOT / "firmware/validation/resultado.json"
compiled = json.loads(compiled_file.read_text(encoding="utf-8-sig"))
check("Compilacao bem sucedida", compiled["success"] and compiled["exitCode"] == 0)
for file, digest in compiled["sourceSha256"].items():
    check("Fonte compilada atual " + file, sha(ROOT / "firmware/diagnostico" / file) == digest)

erc_file = ELEC / "reports/erc.json"
erc = json.loads(erc_file.read_text(encoding="utf-8-sig"))
check("ERC sem violacoes reportadas", all(not s["violations"] for s in erc["sheets"]))
geo_file = MECH / "verificacao_geometrica.json"
geo = json.loads(geo_file.read_text(encoding="utf-8-sig"))
check("CAD solidos validos", geo["objects_valid"] and not geo["document_errors"])
check("CAD sem intersecoes volumetricas reportadas", not geo["collisions"])
check("CAD teste de parametro", geo["parameter_change_test"]["Outer_plus_10_mm"] and geo["parameter_change_test"]["restored"])

failed = [c for c in checks if not c["passed"]]
report = {
    "checked_at_utc": datetime.now(timezone.utc).isoformat(),
    "scope": "Revisao B: contratos entre exportacoes CAD, netlist, BOM e firmware. Baseline 350/300/37.5 mm. Nao simula eletricidade, magnetismo ou montagem.",
    "checks": len(checks), "passed": len(checks) - len(failed), "failures": failed,
    "source_sha256": {str(p.relative_to(ROOT)).replace("\\", "/"): sha(p) for p in [netfile, bom_file, coords_file, compiled_file, erc_file, geo_file]},
    "physical_tests_performed": False,
}
(ROOT / "validacao/integracao.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf8")
print(json.dumps({"checks": len(checks), "passed": len(checks) - len(failed), "failures": failed}, ensure_ascii=False))
raise SystemExit(1 if failed else 0)
