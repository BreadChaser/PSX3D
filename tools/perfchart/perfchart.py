#!/usr/bin/env python3
"""Draw the README performance chart from docs/performance.json.

Two honest numbers per run: game speed (emulation speed, 100% = normal) and FPS
(new pictures actually shown per second). Each is shown with frameskip off and auto.

    tools/perfchart/perfchart.py                         # redraw SVGs + README table
    tools/perfchart/perfchart.py --results <fastbench dir> --console old|new
    tools/perfchart/perfchart.py --set "Silent Hill" old.auto_fps=11.2
    tools/perfchart/perfchart.py --version v0.5.0

--results reads fastbench results.tsv: speed_pct -> *_speed, top_count -> *_fps,
split by the frameskip=off|auto request word.
"""
import argparse, csv, json, os, re, sys
from html import escape

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DATA = os.path.join(ROOT, "docs", "performance.json")
README = os.path.join(ROOT, "README.md")
THEMES = {
    "dark": dict(ink="#f0f6fc", ink2="#c9d1d9", muted="#8b949e", grid="#30363d", axis="#6e7681",
                 off="#3987e5", auto="#d95926", tick="#f0f6fc"),
    "light": dict(ink="#0b0b0b", ink2="#52514e", muted="#7a7974", grid="#e4e3df", axis="#8a8984",
                  off="#2a78d6", auto="#eb6834", tick="#0b0b0b"),
}
FONT = "-apple-system,BlinkMacSystemFont,'Segoe UI',Helvetica,Arial,sans-serif"
W = 900

def svg(d, c):
    out = []
    def t(x, y, s, size=13, fill=c["ink"], anchor="start", weight="normal"):
        out.append(f'<text x="{x:.1f}" y="{y:.1f}" font-size="{size}" fill="{fill}" text-anchor="{anchor}" font-weight="{weight}">{escape(str(s))}</text>')
    def bar(x, y, w, h, fill):
        if w <= 0: return
        r = min(3, h / 2, w / 2)
        out.append(f'<path d="M{x:.1f},{y:.1f} H{x+w-r:.1f} Q{x+w:.1f},{y:.1f} {x+w:.1f},{y+r:.1f} V{y+h-r:.1f} Q{x+w:.1f},{y+h:.1f} {x+w-r:.1f},{y+h:.1f} H{x:.1f} Z" fill="{fill}"/>')
    games = d["games"]
    LABEL_R = 190                     # right edge of game names
    P1, P2 = 210, 560                 # panel starts
    PW = 270                          # panel width
    y = 34
    t(24, y, d["title"], 20, weight="600"); y += 22
    t(24, y, d["subtitle"], 12.5, c["ink2"]); y += 30
    # legend
    for k, (label, col) in enumerate((("Frameskip off", c["off"]), ("Auto frameskip", c["auto"]))):
        out.append(f'<rect x="{24 + k*150}" y="{y-10}" width="12" height="12" rx="2" fill="{col}"/>')
        t(24 + k * 150 + 18, y, label, 12.5, c["ink2"])
    y += 10
    for console, title in (("old", "Old 3DS"), ("new", "New 3DS")):
        y += 34
        t(24, y, title, 16, weight="600")
        t(P1, y, "Game speed", 13, c["ink2"], weight="600")
        t(P2, y, "FPS you see", 13, c["ink2"], weight="600")
        top = y + 14
        rh, bh = 34, 11
        for i, g in enumerate(games):
            r = g[console]; ry = top + i * rh
            t(LABEL_R, ry + 18, g["name"], 13, anchor="end")
            for k, mode in enumerate(("off", "auto")):
                yy = ry + 4 + k * (bh + 2)
                sp, fp = r[f"{mode}_speed"], r[f"{mode}_fps"]
                col = c[mode]
                bar(P1, yy, min(sp, 100) / 100 * PW, bh, col)
                t(P1 + min(sp, 100) / 100 * PW + 6, yy + bh - 1, f"{sp:.0f}%", 11.5, weight="600")
                fp = min(fp, g["native_fps"])      # counting jitter can read 30.3 / 60.6
                bar(P2, yy, fp / 60 * PW, bh, col)
                nx = P2 + g["native_fps"] / 60 * PW
                lx = max(P2 + fp / 60 * PW, nx if fp > g["native_fps"] * 0.8 else 0) + 6
                t(lx, yy + bh - 1, f"{fp:.0f}", 11.5, weight="600")
            nx = P2 + g["native_fps"] / 60 * PW   # the game's own picture rate
            out.append(f'<line x1="{nx:.1f}" y1="{ry+1}" x2="{nx:.1f}" y2="{ry+2*bh+7}" stroke="{c["tick"]}" stroke-width="2" stroke-dasharray="3 2"/>')
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
    y += 18
    t(24, y, "Dashed line = the most pictures per second the game itself draws (most PS1 3D games draw 30).", 12, c["ink2"])
    y += 17
    t(24, y, "Auto frameskip can improve game speed by showing fewer pictures.", 12, c["ink2"])
    H = y + 18
    head = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" font-family="{FONT}" role="img" aria-label="{escape(d["title"], quote=True)}">',
            f'<title>{escape(d["title"])}</title>']
    return "\n".join(head + out + ["</svg>"]) + "\n"

def table(d):
    rows = ["| Game | Console | Frameskip off: speed / FPS | Auto frameskip: speed / FPS |",
            "|---|---|---:|---:|"]
    for g in d["games"]:
        for console, label in (("old", "Old 3DS"), ("new", "New 3DS")):
            r = g[console]
            rows.append(f'| {g["name"]} | {label} | {r["off_speed"]:.1f}% / {r["off_fps"]:.1f} | {r["auto_speed"]:.1f}% / {r["auto_fps"]:.1f} |')
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
            g[console][f"{mode}_fps"] = round(float(row.get("top_count") or row.get("drawn_count") or 0), 1)

def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--results"); p.add_argument("--console", choices=("old", "new"))
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
