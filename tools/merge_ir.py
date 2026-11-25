"""Usage: python tools/merge_ir.py infrared/assets <Flipper-IRDB git tree json>
Build <remote>_plus.ir packs: current m5resources file + Flipper-IRDB (CC0) remotes,
button names normalised to the ones each universal remote screen uses. Entries the
firmware would reject are dropped (it stops reading a file at the first bad signal)."""
import json, os, re, sys, urllib.request, urllib.parse, concurrent.futures as cf

IRDB = "https://raw.githubusercontent.com/Lucaslhm/Flipper-IRDB/main/"
M5 = "https://raw.githubusercontent.com/trollmaster419/m5resources/main/infrared/assets/"

PWR = r"(power|pwr|on_?off|onoff|standby|power_?toggle|power_?on|power_?off|turn_?on|turn_?off)"
VUP = r"(vol_?up|volume_?up|vol\+|volume\+|vol_?plus|v\+)"
VDN = r"(vol_?dn|vol_?down|volume_?down|vol-|volume-|vol_?minus|v-)"
MUTE = r"(mute|av_?mute|a/v_?mute|mute_?a/v)"
PLAY, PAUSE = r"(play|play_?pause|playpause)", r"(pause|play_?pause|playpause)"
REMOTES = {
    "tv": (["TVs"], [("Power", PWR), ("Mute", MUTE), ("Vol_up", VUP), ("Vol_dn", VDN),
                     ("Ch_next", r"(ch_?next|ch_?up|channel_?up|ch\+|chan_?up|p\+)"),
                     ("Ch_prev", r"(ch_?prev|ch_?dn|ch_?down|channel_?down|ch-|chan_?down|p-)")]),
    "audio": (["SoundBars", "Audio_and_Video_Receivers", "Speakers"],
              [("Power", PWR), ("Mute", MUTE), ("Vol_up", VUP), ("Vol_dn", VDN), ("Play", PLAY),
               ("Pause", PAUSE), ("Next", r"(next|skip|skip_?fwd|track_?next|next_?track)"),
               ("Prev", r"(prev|previous|skip_?back|skip_?rev|track_?prev|prev_?track)")]),
    "fans": (["Fans"], [("Power", PWR), ("Mode", r"(mode|wind_?mode|wind)"),
                        ("Speed_up", r"(speed_?up|speed\+|fan_?up|speed)"),
                        ("Speed_dn", r"(speed_?dn|speed_?down|speed-|fan_?down)"),
                        ("Rotate", r"(rotate|oscillat\w*|osc|swing)"), ("Timer", r"(timer|time)")]),
    "leds": (["LED_Lighting"], [("Power_on", r"(power_?on|on|turn_?on)"),
                                ("Power_off", r"(power_?off|off|turn_?off)"),
                                ("Brightness_up", r"(brightness_?up|bright_?up|bright\+|brighter)"),
                                ("Brightness_dn", r"(brightness_?dn|brightness_?down|bright_?down|bright-|dimmer|dim)"),
                                ("Red", r"(red|r)"), ("Green", r"(green|g)"), ("Blue", r"(blue|b)"),
                                ("White", r"(white|w)")]),
    "projectors": (["Projectors"], [("Power", PWR), ("Mute", MUTE), ("Vol_up", VUP), ("Vol_dn", VDN),
                                    ("Play", PLAY), ("Pause", PAUSE), ("Freeze", r"(freeze|frz)")]),
    # AC "Power" frames carry a whole state (mode/temp) - only plain Off is safe to merge
    "ac": (["ACs"], [("Off", r"(off|power_?off|turn_?off)")]),
}
BITS = {"Kaseikyo": (26, 10), "NEC": (8, 8), "NECext": (16, 16), "NEC42": (13, 8),
        "NEC42ext": (26, 16), "Samsung32": (8, 8), "RC5": (5, 6), "RC5X": (5, 7), "RC6": (8, 8),
        "SIRC": (5, 7), "SIRC15": (8, 7), "SIRC20": (13, 7), "RCA": (4, 8), "Pioneer": (8, 8)}


def fetch(url):
    try:
        with urllib.request.urlopen(url, timeout=30) as r:
            return r.read().decode("utf-8", "replace").replace("\r", "")
    except Exception:
        return ""


def signals(text):
    for block in re.split(r"\n#[^\n]*\n|\n(?=name:)", "\n" + text):
        lines = [l for l in block.strip().split("\n") if l and not l.startswith("#")]
        if lines and lines[0].startswith("name:"):
            yield lines[0][5:].strip(), [l for l in lines[1:] if l.split(":")[0] in
                                         ("type", "protocol", "address", "command", "frequency", "duty_cycle", "data")]


def valid(body):
    try:
        d = {l.split(":", 1)[0]: l.split(":", 1)[1].strip() for l in body}
        if d["type"] == "parsed":
            al, cl = BITS[d["protocol"]]
            a = int.from_bytes(bytes.fromhex(d["address"].replace(" ", "")), "little")
            c = int.from_bytes(bytes.fromhex(d["command"].replace(" ", "")), "little")
            return not (a >> al or c >> cl)
        n = len(d["data"].split())
        return d["type"] == "raw" and 0 < n <= 1024 and 10000 <= int(d["frequency"]) <= 1000000 \
            and 0 < float(d["duty_cycle"]) <= 1
    except (KeyError, ValueError):
        return False


def main():
    out_dir, tree_json = sys.argv[1], sys.argv[2]
    tree = [x["path"] for x in json.load(open(tree_json))["tree"] if x["path"].endswith(".ir")]
    for remote, (folders, names) in REMOTES.items():
        paths = [p for p in tree if p.split("/")[0] in folders]
        seen, entries, per, before, dropped = set(), [], {}, {}, 0

        def add(n, b):
            nonlocal dropped
            if (n, tuple(b)) in seen:
                return
            if not valid(b):
                dropped += 1
                return
            seen.add((n, tuple(b)))
            entries.append((n, b))
            per[n] = per.get(n, 0) + 1

        base_name = "fans" if remote == "fans" else remote
        for n, b in signals(fetch(M5 + base_name + ".ir")):
            add(n, b)
        before = dict(per)
        with cf.ThreadPoolExecutor(16) as ex:
            texts = list(ex.map(lambda p: fetch(IRDB + urllib.parse.quote(p)), paths))
        for text in texts:
            for n, b in signals(text):
                key = n.strip().lower().replace(" ", "_")
                for target, rx in names:
                    if re.fullmatch(rx, key):
                        add(target, b)
        with open(os.path.join(out_dir, remote + "_plus.ir"), "w", newline="\n") as f:
            f.write("Filetype: IR library file\nVersion: 1\n")
            f.write(f"# {remote}: trollmaster419/m5resources + Lucaslhm/Flipper-IRDB (CC0), merged\n")
            for n, b in entries:
                f.write("#\nname: " + n + "\n" + "\n".join(b) + "\n")
        summary = ", ".join(f"{t} {before.get(t, 0)}->{per.get(t, 0)}" for t, _ in names)
        print(f"{remote:10} {len(texts):3} files, dropped {dropped:3}: {summary}")
        assert all(per.get(t, 0) >= before.get(t, 0) for t, _ in names), remote


if __name__ == "__main__":
    main()
