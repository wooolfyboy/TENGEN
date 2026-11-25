#!/usr/bin/env python3
"""Runs the decoders on captures from the rtl_433_tests set
(https://github.com/merbanan/rtl_433_tests — .cu8 with the expected .json next to it).

    python3 run_tests.py <path to rtl_433_tests/tests> [dir ...]

For each capture: demod.py -> test_ook -> compare model/id/channel/temperature/humidity
with rtl_433's output. With no dir arguments, uses the default set.
"""
import sys, os, glob, json, subprocess
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
BIN = os.path.join(HERE, ".test_ook")

MODELS = {   # rtl_433 model -> our protocol
    "Nexus-TH": "Nexus", "Nexus-T": "Nexus", "Rubicson-Temperature": "Rubicson", "Prologue-TH": "Prologue",
    "LaCrosse-TX141THBv2": "LaCrosse-TX141TH", "LaCrosse-TX141Bv2": "LaCrosse-TX141", "LaCrosse-TX141Bv3": "LaCrosse-TX141",
    "LaCrosse-TX141B": "LaCrosse-TX141",
    "Bresser-3CH": "Bresser-3CH", "Acurite-Tower": "Acurite-Tower", "Acurite-609TXC": "Acurite-609",
    "Fineoffset-WH2": "FineOffset-WH2", "Fineoffset-WH2A": "FineOffset-WH2A", "Fineoffset-WH5": "FineOffset-WH5",
}
DEFAULT = ["nexus", "rubicson/thermometer", "rubicson/02", "prologue", "lacrosse/tx141th-bv2", "lacrosse/tx141-bv2",
           "lacrosse/tx141-bv3", "lacrosse/04", "lacrosse/tx019t-bv0", "bresser_3ch", "acurite/Acurite_592TXR", "acurite/Acurite_609A1TX", "fineoffset/fineoffset_wh2", "fineoffset/fineoffset_wh2A", "fineoffset/fineoffset_wh5"]

def build():
    subprocess.check_call(["g++", "-O1", "-std=gnu++17", "-I" + os.path.join(ROOT, "src"), os.path.join(HERE, "test_ook.cpp"),
                           os.path.join(ROOT, "src/subghz/ook.cpp"), "-o", BIN])

def expected(path):
    out = []
    for line in open(path, encoding="utf-8", errors="replace"):
        line = line.strip()
        if not line.startswith("{"): continue
        try: d = json.loads(line)
        except Exception: continue
        if d.get("model") in MODELS: out.append(d)
    return out

def decode(cu8):
    pulses = subprocess.run([sys.executable, os.path.join(HERE, "demod.py"), cu8], capture_output=True, text=True, check=True).stdout
    res = subprocess.run([BIN], input=pulses, capture_output=True, text=True, check=True).stdout
    msgs = []
    for line in res.splitlines():
        if line.startswith("MSG "):
            msgs.append(dict(kv.split("=", 1) for kv in line[4:].split()))
    return msgs

def main():
    base = sys.argv[1]
    dirs = sys.argv[2:] or DEFAULT
    build()
    ok = bad = skip = 0
    for d in dirs:
        for cu8 in sorted(glob.glob(os.path.join(base, d, "**", "*.cu8"), recursive=True)):
            js = cu8[:-4] + ".json"
            if not os.path.exists(js): skip += 1; continue
            exp = expected(js)
            if not exp: skip += 1; continue
            got = decode(cu8)
            e = exp[0]
            want = MODELS[e["model"]]
            match = None
            for g in got:
                if g["proto"] != want: continue
                t_ok = abs(float(g["t"]) - (e.get("temperature_C") if "temperature_C" in e else (e["temperature_F"] - 32) * 5 / 9)) < 0.15
                h_ok = "humidity" not in e or int(g["h"]) == int(e["humidity"])
                if t_ok and h_ok: match = g; break
            name = os.path.relpath(cu8, base)
            if match: ok += 1; print("OK  ", name, want, match["t"], match["h"])
            else:
                bad += 1
                print("FAIL", name, "want", want, e.get("temperature_C", e.get("temperature_F")), e.get("humidity"), "got", [(g["proto"], g["t"], g["h"]) for g in got])
    print(f"\n{ok} ok, {bad} fail, {skip} without reference")
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main())
