#!/usr/bin/env python3
"""Draw the README performance chart from docs/performance.json.

Two honest numbers per run: game speed (emulation speed, 100% = normal) and FPS
(new pictures actually shown per second), each with frameskip off and Auto, for the
Old 3DS. The New 3DS gets one sentence (performance.json "new_note"): it runs the
tested scenes at full speed, so off/Auto columns would say nothing.

    tools/perfchart/perfchart.py                         # redraw SVGs + README table
    tools/perfchart/perfchart.py --results <fastbench dir> --console old
    tools/perfchart/perfchart.py --set "Silent Hill" old.auto_fps=11.2
    tools/perfchart/perfchart.py --version v0.5.0

--results reads fastbench results.tsv: speed_pct -> *_speed, min(drawn_count, top_count)
-> *_fps (new pictures, like the in-game counter),
split by the frameskip=off|auto request word.
"""
import argparse, csv, json, os, re, sys
from html import escape

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DATA = os.path.join(ROOT, "docs", "performance.json")
README = os.path.join(ROOT, "README.md")
THEMES = {
    "dark": dict(ink="#f0f6fc", ink2="#c9d1d9", muted="#8b949e", grid="#30363d", axis="#6e7681",
                 off="#3987e5", auto="#f0884f", auto_bar=0.72, tick="#f0f6fc"),
    "light": dict(ink="#0b0b0b", ink2="#52514e", muted="#7a7974", grid="#e4e3df", axis="#8a8984",
                  off="#2a78d6", auto="#eb6834", auto_bar=0.38, tick="#0b0b0b"),
}
FONT = "-apple-system,BlinkMacSystemFont,'Segoe UI',Helvetica,Arial,sans-serif"
W = 1010

def svg(d, c):
    out = []
    def t(x, y, s, size=13, fill=c["ink"], anchor="start", weight="normal"):
        out.append(f'<text x="{x:.1f}" y="{y:.1f}" font-size="{size}" fill="{fill}" text-anchor="{anchor}" font-weight="{weight}">{escape(str(s))}</text>')
    def bar(x, y, w, h, fill, opacity=1):
        if w <= 0: return
        r = min(4, h / 2, w / 2)
        out.append(f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h}" rx="{r:.1f}" fill="{fill}" fill-opacity="{opacity}"/>')
    def pair(x, y, off, auto, unit):
        # "88% · 98%": off in its colour, auto in its colour
        out.append(f'<text x="{x:.1f}" y="{y:.1f}" font-size="11.5" font-weight="600" xml:space="preserve">'
                   f'<tspan fill="{c["off"]}">{off:.0f}{unit}</tspan><tspan fill="{c["muted"]}"> · </tspan>'
                   f'<tspan fill="{c["auto"]}">{auto:.0f}{unit}</tspan></text>')
    games = d["games"]
    LABEL_R = 190                     # right edge of game names
    P1, P2 = 210, 572                 # panel starts
    PW = 255                          # panel width
    P3 = 890                          # change since the previous release
    y = 34
    t(24, y, d["title"], 20, weight="600"); y += 22
    for line in d["subtitle"].split("\n"):   # one text line each
        t(24, y, line, 12.5, c["ink2"]); y += 17
    y += 13
    # legend
    bar(24, y - 7, 20, 6, c["off"])
    t(50, y, "Frameskip off", 12.5, c["ink2"])
    bar(160, y - 12, 20, 16, c["auto"], c["auto_bar"])
    t(186, y, "Auto frameskip", 12.5, c["ink2"])
    y += 10
    shown = [("old", "Old 3DS")]
    for console, title in shown:
        y += 34
        t(24, y, title, 16, weight="600")
        t(P1, y, "Speed", 13, c["ink2"], weight="600")
        t(P2, y, "FPS", 13, c["ink2"], weight="600")
        prev = d.get("prev_version")
        if prev:
            t(P3, y, f"Speed vs {prev}", 13, c["ink2"], weight="600")
        top = y + 14
        rh, ah, oh = 30, 20, 8            # row, outer (Auto) and inner (off) bar heights
        for i, g in enumerate(games):
            r = g[console]; ry = top + i * rh
            t(LABEL_R, ry + 19, g["name"], 13, anchor="end")
            nat = g["native_fps"]
            for P, scale, unit, key in ((P1, 100, "%", "speed"), (P2, 60, "", "fps")):
                off, auto = r[f"off_{key}"], r[f"auto_{key}"]
                if key == "fps":                 # counting jitter can read 30.3 / 60.6
                    off, auto = min(off, nat), min(auto, nat)
                wo, wa = min(off, scale) / scale * PW, min(auto, scale) / scale * PW
                bar(P, ry + 4, wa, ah, c["auto"], c["auto_bar"])
                bar(P, ry + 4 + (ah - oh) / 2, wo, oh, c["off"])
                end = max(wo, wa)
                if key == "fps":
                    nx = P2 + nat / 60 * PW      # the game's own maximum FPS
                    out.append(f'<line x1="{nx:.1f}" y1="{ry+1}" x2="{nx:.1f}" y2="{ry+ah+7}" stroke="{c["tick"]}" stroke-width="1.5" stroke-dasharray="3 2" stroke-opacity="0.7"/>')
                    if nx - P < end + 60 and nx - P > end - 4:
                        end = nx - P
                pair(P + end + 7, ry + 18, r[f"off_{key}"] if key == "speed" else off,
                     r[f"auto_{key}"] if key == "speed" else auto, unit)
            if prev and f"prev_off_speed" in r:
                # relative speed change against the previous release, off · auto
                d_off = (r["off_speed"] / r["prev_off_speed"] - 1) * 100
                d_auto = (r["auto_speed"] / r["prev_auto_speed"] - 1) * 100
                out.append(f'<text x="{P3:.1f}" y="{ry+18:.1f}" font-size="11.5" font-weight="600" xml:space="preserve">'
                           f'<tspan fill="{c["off"]}">{d_off:+.1f}%</tspan><tspan fill="{c["muted"]}"> · </tspan>'
                           f'<tspan fill="{c["auto"]}">{d_auto:+.1f}%</tspan></text>')
        bottom = top + len(games) * rh
        for v in (0, 50, 100):
            x = P1 + v / 100 * PW
            out.insert(0, f'<line x1="{x:.1f}" y1="{top-2}" x2="{x:.1f}" y2="{bottom}" stroke="{c["axis"] if v == 0 else c["grid"]}"/>')
            t(x, bottom + 15, f"{v}%", 10.5, c["muted"], "middle")
        for v in (0, 30, 60):
            x = P2 + v / 60 * PW
            out.insert(0, f'<line x1="{x:.1f}" y1="{top-2}" x2="{x:.1f}" y2="{bottom}" stroke="{c["axis"] if v == 0 else c["grid"]}"/>')
            t(x, bottom + 15, str(v), 10.5, c["muted"], "middle")
        y = bottom + 22
    if d.get("placeholder"):
        y += 26
        t(24, y, d["placeholder"], 13.5, c["auto"], weight="600")
    if d.get("new_note"):
        y += 30
        t(24, y, d["new_note"], 13.5, weight="600")
    y += 26
    t(24, y, "Dashed line = the game's own maximum FPS (30 for most PS1 3D games).", 12, c["ink2"])
    y += 17
    t(24, y, "Auto frameskip skips some frames to keep the game closer to full speed.", 12, c["ink2"])
    if d.get("prev_note"):
        y += 17
        t(24, y, d["prev_note"], 12, c["ink2"])
    H = y + 18
    head = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" font-family="{FONT}" role="img" aria-label="{escape(d["title"], quote=True)}">',
            f'<title>{escape(d["title"])}</title>']
    return "\n".join(head + out + ["</svg>"]) + "\n"

def table(d):
    rows = ["| Game (Old 3DS) | Frameskip off: speed / FPS | Auto frameskip: speed / FPS |",
            "|---|---:|---:|"]
    for g in d["games"]:
        r = g["old"]
        rows.append(f'| {g["name"]} | {r["off_speed"]:.1f}% / {r["off_fps"]:.1f} | {r["auto_speed"]:.1f}% / {r["auto_fps"]:.1f} |')
    if d.get("placeholder"):
        rows.insert(0, f'**{d["placeholder"]}**\n')
    return "\n".join(rows)

def from_results(d, path, console):
    tsvs = [path] if path.endswith(".tsv") else [os.path.join(r, f) for r, _, fs in os.walk(path) for f in fs if f == "results.tsv"]
    matching = [f for f in tsvs if os.path.basename(os.path.dirname(f)) == console]
    tsvs = matching or tsvs
    if len(tsvs) != 1:
        sys.exit("--results needs one results.tsv for the requested console; choose its batch directory")
    norm = lambda s: re.sub(r"[^a-z0-9]", "", s.lower())
    for tsv in tsvs:
        for row in csv.DictReader(open(tsv), delimiter="\t"):
            if row.get("status") != "ok": continue
            req = dict(w.split("=", 1) for w in row["request"].split() if "=" in w)
            mode = req.get("frameskip")
            if mode not in ("off", "auto"): continue
            key = norm(req.get("game", ""))
            matches = [g for g in d["games"] if norm(g["name"]) in key]
            g = max(matches, key=lambda g: len(norm(g["name"]))) if matches else None
            if not g: print("skip (add it to performance.json first):", req.get("game")); continue
            g.setdefault(console, {})[f"{mode}_speed"] = round(float(row["speed_pct"]), 1)
            counts = [float(row[k]) for k in ("drawn_count", "top_count") if row.get(k)]
            g[console][f"{mode}_fps"] = round(min(counts) if counts else 0, 1)

def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--results"); p.add_argument("--console", choices=("old",))
    p.add_argument("--set", nargs="+", metavar=("GAME", "console.key=value"))
    p.add_argument("--version")
    a = p.parse_args()
    d = json.load(open(DATA))
    if a.version: d["version"] = a.version
    if a.set:
        g = next(g for g in d["games"] if g["name"].lower() == a.set[0].lower())
        for kv in a.set[1:]:
            k, v = kv.split("="); con, key = k.split(".")
            g[con][key] = float(v)
    if a.results:
        if not a.console: sys.exit("--results needs --console old|new")
        from_results(d, a.results, a.console)
        d.pop("placeholder", None)   # measured numbers replace the placeholder
    with open(DATA, "w") as f:
        json.dump(d, f, indent=2); f.write("\n")
    for name, colors in THEMES.items():
        open(os.path.join(ROOT, "docs", f"performance-{name}.svg"), "w").write(svg(d, colors))
    text = open(README).read()
    new = re.sub(r"(<!-- perf-table -->\n).*?(\n<!-- /perf-table -->)", lambda m: m.group(1) + table(d) + m.group(2), text, flags=re.S)
    if new != text: open(README, "w").write(new)
    print("updated docs/performance-{dark,light}.svg" + (" and the README table" if new != text else ""))

if __name__ == "__main__":
    main()
