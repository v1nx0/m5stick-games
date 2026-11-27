"""Full brand remotes for Infrared > Saved Remotes (installed by WiFi > Update SD).

Usage: python tools/ir_remotes.py <Flipper-IRDB git tree json> [TVs|Projectors ...]
Downloads Flipper-IRDB (CC0) remotes, keeps only signals the firmware accepts (it
rejects a whole file at the first bad one) and writes ONE file per brand,
infrared/<Folder>/<Brand>.ir: the stick's LittleFS spends a 4 KB block per file, so
per-model files (523) did not fit next to the base files and IR packs. Same-named
buttons with different codes become "Power 2", "Power 3"... Safe names because the
stick's downloader puts paths straight into URLs and the Manifest splits on ':'.
"""
import json, os, re, sys, urllib.parse, urllib.request, concurrent.futures as cf

sys.path.insert(0, os.path.dirname(__file__))
from merge_ir import signals, valid  # same parser + firmware validity rules as the packs

IRDB = "https://raw.githubusercontent.com/Lucaslhm/Flipper-IRDB/main/"
# folder -> brands to keep (None = all). TVs: major brands only, storage is limited.
FOLDERS = {
    "TVs": {"Samsung", "LG", "Sony", "Philips", "Panasonic", "Sharp", "Hisense", "TCL", "Toshiba",
            "Xiaomi", "Grundig", "JVC", "Hitachi", "Vizio", "Haier", "Akai", "Amazon", "Blaupunkt",
            "Bush", "Daewoo", "Dynex", "Element", "Emerson", "Funai", "Insignia", "Magnavox", "NEC",
            "Onn", "Pioneer", "RCA", "Roku", "Sanyo", "Sceptre", "Telefunken", "Thomson", "Westinghouse",
            "Zenith"},
    "Projectors": None,
}
OUT = "infrared"


def fetch(path):
    try:
        with urllib.request.urlopen(IRDB + urllib.parse.quote(path), timeout=30) as r:
            return r.read().decode("utf-8", "replace").replace("\r", "")
    except Exception:
        return ""


def safe(name):
    return re.sub(r"[^A-Za-z0-9._-]+", "_", name).strip("_") or "remote"


tree = [x["path"] for x in json.load(open(sys.argv[1]))["tree"] if x["path"].endswith(".ir")]
selected = set(sys.argv[2:]) or set(FOLDERS)
for folder, keep in FOLDERS.items():
    if folder not in selected:
        continue
    paths = [p for p in tree if p.split("/")[0] == folder and len(p.split("/")) > 2
             and (keep is None or p.split("/")[1] in keep)]
    with cf.ThreadPoolExecutor(16) as ex:
        texts = list(ex.map(fetch, paths))
    brands, dropped_sig = {}, 0
    for path, text in zip(paths, texts):
        b = brands.setdefault(safe(path.split("/")[1]), {"seen": set(), "names": {}, "out": []})
        for name, body in signals(text):
            if not (valid(body) and name and ":" not in name):
                dropped_sig += 1
                continue
            if tuple(body) in b["seen"]:
                continue  # same code already in this brand's remote
            b["seen"].add(tuple(body))
            n = b["names"].get(name, 0) + 1
            b["names"][name] = n
            b["out"].append((name if n == 1 else f"{name} {n}", body))
    os.makedirs(f"{OUT}/{folder}", exist_ok=True)
    for brand, b in sorted(brands.items()):
        if not b["out"]:
            continue
        with open(f"{OUT}/{folder}/{brand}.ir", "w", newline="\n") as f:
            f.write("Filetype: IR signals file\nVersion: 1\n")
            f.write(f"# {folder}/{brand} - Lucaslhm/Flipper-IRDB (CC0), all models merged\n")
            for name, body in b["out"]:
                f.write("#\nname: " + name + "\n" + "\n".join(body) + "\n")
    files = [os.path.join(f"{OUT}/{folder}", f) for f in os.listdir(f"{OUT}/{folder}")]
    size = sum(os.path.getsize(f) for f in files)
    print(f"{folder}: {len(files)} brand remotes ({size / 1048576:.2f} MB), {dropped_sig} bad signals dropped")
