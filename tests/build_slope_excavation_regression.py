"""Create an isolated interface regression project from an existing solved slope.

The source is read only. Its factor-1.0 stress state is reused for integration
testing, not as an independent excavation accuracy benchmark.
"""
from pathlib import Path
import argparse
import copy
import json
import shutil
import uuid

parser = argparse.ArgumentParser()
parser.add_argument("source", type=Path)
parser.add_argument("destination", type=Path)
args = parser.parse_args()
assert args.source.resolve() != args.destination.resolve()
assert not args.destination.exists(), "Use a fresh destination; existing projects are never overwritten"
data = args.destination / "工程数据"
shutil.copytree(args.source / "工程数据", data)
def load(name):
    return json.loads((data / name).read_text(encoding="utf-8-sig"))
def save(name, value):
    (data / name).write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")
mesh = load("mesh.json")
mesh["binary_cache_file"] = ""
mesh["surface_cache_file"] = ""
save("mesh.json", mesh)
snapshot = data / "mesh_snapshot.iiin.dat"
rows = snapshot.read_text().splitlines()
head = rows[0].split()
start, width = 2 + int(head[0]), int(head[3])
for index in range(int(head[2])):
    row = rows[start+index].split()
    row[width+3] = str(10 if index < 4 else 20 if index < 8 else 1)
    rows[start+index] = " ".join(row)
snapshot.write_text("\n".join(rows)+"\n", encoding="ascii")
solver = load("solver.json")
case = next(x for x in solver["items"] if x["type"] == "SlopeStabilityAnalysis")
case["title"] = "开挖不加固"
case["custom_title"] = case["title"]
case["uuid"] = "{"+str(uuid.uuid4())+"}"
case["children"] = []
state = case["state"]
state["base_values"][2] = "0"
state["base_values"][5] = "30"
state["base_values"][25] = "0"
state["strength_reserve"] = [{"factor":"1.0000", "ifpr":"1.0000"}]
state["excavation"] = {"enabled": True, "support": 0, "stages": [
    {"excavation_layer": -i-1,"selected_groups":[group],"selected_group_category":"LAYER", "selected_group_dim":int(head[1]),"selected_domains":[{"category":"LAYER","dim":int(head[1]),"id":group}],"enable_view_pick":False}
    for i, group in enumerate((10,20))], "reinforcement": {}}
reinforced = copy.deepcopy(case)
reinforced["title"] = "开挖及加固"
reinforced["custom_title"] = reinforced["title"]
reinforced["uuid"] = "{"+str(uuid.uuid4())+"}"
reinforced["state"]["excavation"]["support"] = 1
reinforced["state"]["excavation"]["reinforcement"] = {
    "mode": 2, "bar_length": 3.0, "bar_delta_c_mpa": 0.1,
    "cable_length": 6.0, "cable_delta_c_mpa": 0.15, "concept_stiffness": 1.2,
}
solver["items"] = [case, reinforced]
save("solver.json", solver)
original = args.source / "边坡分析" / "CASE01"
for item in solver["items"]:
    stress = args.destination / item["title"] / "GEOSTR"
    stress.mkdir(parents=True)
    for name in ("MODSTR", "ISGMAR"):
        shutil.copy2(original / (name+"_CZ100CB100.TXT"), stress / (name+".TXT"))
template = next(args.source.glob("*.xml")).read_text(encoding="utf-8-sig")
(args.destination / (args.destination.name+".xml")).write_text(template.replace(args.source.name,args.destination.name),encoding="utf-8")
(args.destination / "说明.txt").write_text("接口回归工程：初始应力复用原边坡系数1.0的静力状态，验证分级开挖设置和数据传递，不作为独立精度验算。\n",encoding="utf-8")
for item in solver["items"]:
    print(item["title"], item["uuid"])
