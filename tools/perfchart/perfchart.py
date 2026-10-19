#!/usr/bin/env python3
"""Draw the README performance chart from docs/performance.json.

    tools/perfchart/perfchart.py                  # redraw both SVGs and the README table
    tools/perfchart/perfchart.py --set "Crash Bandicoot" old_fps=54.1 old_auto=100
    tools/perfchart/perfchart.py --results <fastbench dir> --console old|new

--results reads a fastbench results.tsv and updates old_fps/old_auto (or new_*) for
every game it contains: frameskip=off rows set *_fps, frameskip=auto rows set *_auto.
To start a new release, move the current numbers to old_prev_fps with --promote.
"""
import argparse, csv, json, os, re, sys
from html import escape

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DATA = os.path.join(ROOT, "docs", "performance.json")
README = os.path.join(ROOT, "README.md")
THEMES = {
    "dark": dict(surf="#161b22", border="#30363d", ink="#f0f6fc", ink2="#c9d1d9", muted="#8b949e",
                 grid="#262c34", axis="#6e7681", prev="#5a6270", cur="#3987e5", full="#199e70",
                 tile="#3fd49214", tile_border="#3fd49255", tile_ink="#3fd492"),
    "light": dict(surf="#fcfcfb", border="#e4e3df", ink="#0b0b0b", ink2="#52514e", muted="#8a8984",
                  grid="#e9e8e4", axis="#8a8984", prev="#b9b8b2", cur="#2a78d6", full="#1baf7a",
                  tile="#1baf7a14", tile_border="#1baf7a55", tile_ink="#0d6b49"),
}
FONT = "-apple-system,BlinkMacSystemFont,'Segoe UI',Helvetica,Arial,sans-serif"

def svg(d, c):
    games, W, X0 = d["games"], 880, 230
    PW = W - X0 - 110
    out = []
    def t(x, y, s, size=13, fill=c["ink"], anchor="start", weight="normal"):
        out.append(f'<text x="{x:.1f}" y="{y:.1f}" font-size="{size}" fill="{fill}" text-anchor="{anchor}" font-weight="{weight}">{escape(str(s))}</text>')
    def bar(x, y, w, h, fill):
        if w <= 0: return
        r = min(4, h / 2, w / 2)
        out.append(f'<path d="M{x:.1f},{y:.1f} H{x+w-r:.1f} Q{x+w:.1f},{y:.1f} {x+w:.1f},{y+r:.1f} V{y+h-r:.1f} Q{x+w:.1f},{y+h:.1f} {x+w-r:.1f},{y+h:.1f} H{x:.1f} Z" fill="{fill}"/>')
    def grid(top, bottom, ticks, scale, fmt):
        for v in ticks:
            x = X0 + v * scale
            out.insert(0, f'<line x1="{x:.1f}" y1="{top-4}" x2="{x:.1f}" y2="{bottom}" stroke="{c["grid"] if v else c["axis"]}" stroke-width="1"/>')
            t(x, bottom + 16, fmt(v), 11, c["muted"], "middle")
    y = 40
    t(28, y, d["title"], 20, weight="600"); y += 22
    t(28, y, d["subtitle"], 13, c["ink2"]); y += 44
    t(28, y, "Old 3DS — frameskip off (frames per second)", 15, weight="600")
    lx = X0 + PW - 250
    for k, (label, col) in enumerate(((d["previous"], c["prev"]), (d["current"], c["cur"]))):
        out.append(f'<rect x="{lx+k*80}" y="{y-10}" width="12" height="12" rx="2" fill="{col}"/>')
        t(lx + k * 80 + 18, y, label, 12, c["ink2"])
    top = y + 18; s = PW / 60.0; rh, bh = 38, 13
    for i, g in enumerate(games):
        ry = top + i * rh
        t(X0 - 14, ry + bh + 6, g["name"], 13, anchor="end")
        if g.get("old_prev_fps"):
            bar(X0, ry + 2, g["old_prev_fps"] * s, bh - 2, c["prev"])
        bar(X0, ry + bh + 2, g["old_fps"] * s, bh, c["cur"])
        t(X0 + g["old_fps"] * s + 8, ry + bh * 2 - 1, f'{g["old_fps"]:.1f}', 13, weight="600")
        if g.get("old_prev_fps"):
            gain = (g["old_fps"] / g["old_prev_fps"] - 1) * 100
            t(X0 + g["old_fps"] * s + 46, ry + bh * 2 - 1, f"{gain:+.0f}%", 12, c["muted"])
    bottom = top + len(games) * rh
    grid(top, bottom, (0, 15, 30, 45, 60), s, str)
    t(X0 + 60 * s, top - 8, "full speed", 11, c["muted"], "middle")
    y = bottom + 56
    t(28, y, f'Old 3DS — Auto frameskip (game speed, {d["current"]})', 15, weight="600")
    top = y + 18; s = (PW - 150) / 100.0; rh = 26
    for i, g in enumerate(games):
        ry = top + i * rh
        t(X0 - 14, ry + 14, g["name"], 13, anchor="end")
        bar(X0, ry + 3, g["old_auto"] * s, 14, c["full"] if g["old_auto"] >= 99.5 else c["cur"])
        t(X0 + g["old_auto"] * s + 8, ry + 15, f'{g["old_auto"]:.0f}%', 13, weight="600")
        if g.get("old_auto_shown") and g.get("old_auto_game") and g["old_auto_shown"] < g["old_auto_game"] - 0.5:
            t(X0 + g["old_auto"] * s + 52, ry + 15, f'shows {g["old_auto_shown"]:.0f} of {g["old_auto_game"]:.0f} frames/s', 12, c["muted"])
    bottom = top + len(games) * rh
    grid(top, bottom, (0, 25, 50, 75, 100), s, lambda v: f"{v}%")
    t(X0 + 12, bottom + 36, "Green = full game speed. \"Shows 14 of 30\": pictures shown per second out of the pictures the game draws.", 12, c["ink2"])
    y = bottom + 74
    full = all(g["new_fps"] >= 59.5 and g["new_auto"] >= 99.5 for g in games)
    t(28, y, "New 3DS — " + ("every game above: 60 fps, frameskip off and Auto" if full else "frameskip off (fps) / Auto (game speed)"), 15, weight="600")
    y += 14
    n = len(games); cw = (W - 56 - (n - 1) * 8) / n
    for i, g in enumerate(games):
        x = 28 + i * (cw + 8)
        out.append(f'<rect x="{x:.1f}" y="{y}" width="{cw:.1f}" height="54" rx="6" fill="{c["tile"]}" stroke="{c["tile_border"]}"/>')
        label = f'{g["new_fps"]:.0f} fps' if full else f'{g["new_fps"]:.0f} / {g["new_auto"]:.0f}%'
        t(x + cw / 2, y + 26, label, 16, c["tile_ink"], "middle", "600")
        t(x + cw / 2, y + 44, g["short"], 11, c["ink2"], "middle")
    H = y + 82
    head = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" font-family="{FONT}" role="img" aria-label="{escape(d["title"], quote=True)}">',
            f'<title>{escape(d["title"])}</title>',
            ]  # transparent: the page background shows through
    return "\n".join(head + out + ["</svg>"]) + "\n"

def table(d):
    rows = ["| Game | Old, off (FPS) | Old, auto (game speed) | Old, auto (frames shown) | New, off (FPS) | New, auto (game speed) |",
            "|---|---:|---:|---:|---:|---:|"]
    for g in d["games"]:
        shown = f'{g["old_auto_shown"]:.0f} of {g["old_auto_game"]:.0f}/s' if g.get("old_auto_shown") else "—"
        rows.append(f'| {g["name"]} | {g["old_fps"]:.1f} | {g["old_auto"]:.1f}% | {shown} | {g["new_fps"]:.1f} | {g["new_auto"]:.1f}% |')
    return "\n".join(rows)

def from_results(d, path, console):
    tsv = path if path.endswith(".tsv") else next(os.path.join(r, f) for r, _, fs in os.walk(path) for f in fs if f == "results.tsv")
    byname = {re.sub(r"\W", "", g["name"]).lower(): g for g in d["games"]}
    for row in csv.DictReader(open(tsv), delimiter="\t"):
        if row.get("status") != "ok": continue
        req = dict(w.split("=", 1) for w in row["request"].split() if "=" in w)
        key = re.sub(r"\W", "", req.get("game", "").replace("_", " ")).lower()
        g = byname.get(key)
        if g is None:
            # Full titles also match mode suffixes and the Castlevania prefix.
            matches = [(len(k), v) for k, v in byname.items() if k in key]
            g = max(matches, key=lambda item: item[0])[1] if matches else None
        if not g: print("skip (not in performance.json):", req.get("game")); continue
        if req.get("frameskip") == "off": g[f"{console}_fps"] = round(float(row["fps"]), 1)
        elif req.get("frameskip") == "auto":
            g[f"{console}_auto"] = round(float(row["speed_pct"]), 1)
            shown, skipped = float(row.get("drawn_count") or 0), float(row.get("skipped_count") or 0)
            if shown + skipped > 0:
                g[f"{console}_auto_shown"] = round(shown, 1)
                g[f"{console}_auto_game"] = round(shown + skipped, 1)

def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--set", nargs="+", metavar=("GAME", "KEY=VALUE"))
    p.add_argument("--results"); p.add_argument("--console", choices=("old", "new"))
    p.add_argument("--promote", metavar="NEW_VERSION", help="current numbers become the previous release")
    a = p.parse_args()
    d = json.load(open(DATA))
    if a.promote:
        d["previous"], d["current"] = d["current"], a.promote
        for g in d["games"]: g["old_prev_fps"] = g["old_fps"]
    if a.set:
        g = next(g for g in d["games"] if g["name"].lower() == a.set[0].lower())
        for kv in a.set[1:]:
            k, v = kv.split("="); g[k] = float(v)
    if a.results:
        if not a.console: sys.exit("--results needs --console old|new")
        from_results(d, a.results, a.console)
    json.dump(d, open(DATA, "w"), indent=2); open(DATA, "a").write("\n")
    for name, colors in THEMES.items():
        open(os.path.join(ROOT, "docs", f"performance-{name}.svg"), "w").write(svg(d, colors))
    text = open(README).read()
    new = re.sub(r"(<!-- perf-table -->\n).*?(\n<!-- /perf-table -->)", lambda m: m.group(1) + table(d) + m.group(2), text, flags=re.S)
    if new != text: open(README, "w").write(new)
    print("updated docs/performance-{dark,light}.svg" + (" and README table" if new != text else ""))

if __name__ == "__main__":
    main()
