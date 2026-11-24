"""Write Manifest (F:<md5>:<size>:<path>) for every file under apps/ -- the
format the M5StickC Plus2 App Store and "Update SD" read."""
import hashlib, os, time

lines = ["V:0", f"T:{int(time.time())}"]
for root, dirs, files in os.walk("apps"):
    dirs.sort()
    lines.append("D:" + root.replace(os.sep, "/"))
    for f in sorted(files):
        p = os.path.join(root, f)
        data = open(p, "rb").read()
        lines.append(f"F:{hashlib.md5(data).hexdigest()}:{len(data)}:{p.replace(os.sep, '/')}")
open("Manifest", "w", newline="\n").write("\n".join(lines) + "\n")
fap = [l for l in lines if l.startswith("F:") and l.endswith(".fap")]
assert fap, "no .fap files under apps/ -- build step produced nothing"
print(f"Manifest: {len(fap)} apps")
