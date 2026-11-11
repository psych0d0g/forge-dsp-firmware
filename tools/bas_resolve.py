#!/usr/bin/env python3
"""Resolve the BASCOM build of one Forge device: follow $include, evaluate
#IF/#ELSE/#ENDIF with the `const` values in source order.

  bas_resolve.py MAIN.bas [--flat out.bas] [--report out.md]

--flat   active code only, every line prefixed with  'file:line
--report active files with line counts, unresolved #IF expressions, consts
"""

import argparse
import collections
import os
import re
import sys

INCLUDE = re.compile(r'^\s*\$include\s+"([^"]+)"', re.I)
CONST = re.compile(r'^\s*const\s+([A-Za-z_]\w*)\s*=\s*(.+)$', re.I)
IF = re.compile(r'^\s*#if\s+(.+)$', re.I)
ELSE = re.compile(r'^\s*#else\b', re.I)
ENDIF = re.compile(r'^\s*#endif\b', re.I)


def strip_comment(line):
    out, q = [], False
    for c in line:
        if c == '"':
            q = not q
        if c == "'" and not q:
            break
        out.append(c)
    s = "".join(out)
    s = re.sub(r"\brem\b.*$", "", s, flags=re.I) if not q else s
    return s.strip()


class Resolver:
    def __init__(self, main):
        self.base = os.path.dirname(os.path.abspath(main))
        self.main = main
        self.consts = {}
        self.unresolved = collections.Counter()
        self.files = collections.OrderedDict()  # path -> [active, total]
        self.flat = []
        self.missing = []

    def path(self, inc, cur_dir):
        p = inc.replace("\\", "/")
        for d in (cur_dir, self.base):
            q = os.path.normpath(os.path.join(d, p))
            if os.path.exists(q):
                return q
            # case-insensitive fallback (Windows sources)
            dd, name = os.path.split(q)
            if os.path.isdir(dd):
                for f in os.listdir(dd):
                    if f.lower() == name.lower():
                        return os.path.join(dd, f)
        return None

    def value(self, expr):
        e = expr.strip()
        e = re.sub(r"&H([0-9A-Fa-f]+)", r"0x\1", e)
        e = re.sub(r"&B([01]+)", r"0b\1", e)
        e = re.sub(r"%", "", e)
        e = re.sub(r"<>", " != ", e)
        e = re.sub(r"(?<![<>=!])=(?!=)", " == ", e)
        e = re.sub(r"\band\b", " and ", e, flags=re.I)
        e = re.sub(r"\bor\b", " or ", e, flags=re.I)
        e = re.sub(r"\bnot\b", " not ", e, flags=re.I)
        e = re.sub(r"\bmod\b", " % ", e, flags=re.I)
        e = re.sub(r'varexist\("[^"]*"\)', "False", e, flags=re.I)  # debug-only checks
        bare = re.sub(r'"[^"]*"', '""', e)
        names = set(re.findall(r"\b[A-Za-z_]\w*\b", bare)) - {"and", "or", "not", "True", "False"}
        env = {}
        for n in names:
            k = n.lower()
            if k not in self.consts:
                raise KeyError(n)
            env[n] = self.consts[k]
        return eval(e, {"__builtins__": {}}, env)  # sources are trusted local files

    def run(self):
        self.walk(self.main)
        return self

    def walk(self, path):
        rel = os.path.relpath(path, os.path.join(self.base, "../../../.."))
        st = self.files.setdefault(rel, [0, 0])
        with open(path, encoding="latin-1") as f:
            lines = f.read().splitlines()
        st[1] += len(lines)
        stack = []  # entries: (parent_active, this_branch_active, taken)
        active = True
        for no, raw in enumerate(lines, 1):
            s = strip_comment(raw)
            m = IF.match(s)
            if m:
                cond = False
                if active:
                    try:
                        cond = bool(self.value(m.group(1)))
                    except Exception:
                        self.unresolved[f"{rel}:{no}: {m.group(1).strip()}"] += 1
                        cond = False
                stack.append((active, cond))
                active = active and cond
                continue
            if ELSE.match(s):
                parent, cond = stack[-1]
                stack[-1] = (parent, not cond)
                active = parent and not cond
                continue
            if ENDIF.match(s):
                parent, _ = stack.pop()
                active = parent
                continue
            if not active:
                continue
            m = INCLUDE.match(raw)
            if m:
                p = self.path(m.group(1), os.path.dirname(path))
                if p is None:
                    self.missing.append(f"{rel}:{no}: {m.group(1)}")
                else:
                    self.flat.append(f"'>>> {os.path.relpath(p, os.path.join(self.base, '../../../..'))}")
                    self.walk(p)
                continue
            m = CONST.match(s)
            if m:
                try:
                    v = m.group(2).strip()
                    self.consts[m.group(1).lower()] = v[1:-1] if v.startswith('"') else self.value(v)
                except Exception:
                    self.consts[m.group(1).lower()] = m.group(2).strip()
            if s:
                st[0] += 1
                self.flat.append(f"{raw}    '{rel}:{no}")
        if stack:
            self.unresolved[f"{rel}: unbalanced #IF"] += 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("main")
    ap.add_argument("--flat")
    ap.add_argument("--report")
    a = ap.parse_args()
    r = Resolver(a.main).run()
    if a.flat:
        with open(a.flat, "w", encoding="utf-8") as f:
            f.write("\n".join(r.flat) + "\n")
    rep = []
    total = sum(v[0] for v in r.files.values())
    rep.append(f"# Aktiver BASCOM-Code: {os.path.relpath(a.main)}\n")
    rep.append(f"{len(r.files)} Dateien eingebunden, {total} aktive Codezeilen (ohne Kommentare/Leerzeilen).\n")
    groups = collections.Counter()
    for p, (act, _) in r.files.items():
        parts = p.split("/")
        key = "/".join(parts[:2]) if parts[0] == "libs" else parts[0]
        groups[key] += act
    rep.append("## Nach Modul\n\n| Modul | aktive Zeilen |\n|---|---:|")
    for k, v in sorted(groups.items(), key=lambda x: -x[1]):
        rep.append(f"| `{k}` | {v} |")
    rep.append("\n## Dateien\n\n| Datei | aktiv | gesamt |\n|---|---:|---:|")
    for p, (act, tot) in r.files.items():
        rep.append(f"| `{p}` | {act} | {tot} |")
    if r.unresolved:
        rep.append("\n## Nicht auswertbare #IF (als falsch behandelt)\n")
        rep += [f"- `{k}`" for k in r.unresolved]
    if r.missing:
        rep.append("\n## Fehlende Includes\n")
        rep += [f"- `{k}`" for k in r.missing]
    feats = {k: v for k, v in r.consts.items()
             if re.match(r"(modul|ext|feature|com_port|cmd_version|mcu_|libs_)", k)}
    rep.append("\n## Build-Schalter (const modul_/ext_/feature_/com_port/cmd_version/mcu_/libs_)\n")
    rep.append("| Name | Wert |\n|---|---|")
    for k in sorted(feats):
        rep.append(f"| `{k}` | `{feats[k]}` |")
    text = "\n".join(rep) + "\n"
    if a.report:
        with open(a.report, "w", encoding="utf-8") as f:
            f.write(text)
    else:
        sys.stdout.write(text)


if __name__ == "__main__":
    main()
