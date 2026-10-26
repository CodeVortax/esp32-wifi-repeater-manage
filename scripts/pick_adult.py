#!/usr/bin/env python3
"""Chooses the names for the built-in adult list (blocklist/adult/names.txt).

This is run by hand, not in CI (the raw lists are 10+ MB). Inputs, from a local clone:
  --a  blocklistproject/Lists  porn.txt         (hosts format, Unlicense/MIT)
  --b  Bon-Appetit/porn-domains block.*.txt     (one name per line, CC BY-SA 4.0)
A name is kept only when BOTH lists carry it (two independent maintainers agree, so dead and
spam entries mostly drop out), names that the firmware's keyword rule already catches are
skipped, subdomains of a listed parent are folded into the parent, and the shortest names
(the ones people actually visit) are taken until the byte budget is reached.

    python3 scripts/pick_adult.py --a Lists/porn.txt --b porn-domains/block.*.txt --budget 330000
"""
import argparse, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_blocklist as g

KEYWORDS = ("porn", "hentai", "xvideo", "xnxx", "xhamster", "redtube", "brazzers", "bangbros")
TLDS = ("xxx", "porn", "adult", "sex")      # keep in sync with ADULT_KEYWORDS in src/adblock.cpp

def covered(n):
    return n.rsplit(".", 1)[-1] in TLDS or any(k in n for k in KEYWORDS)

def read(path, hosts):
    s = set()
    for l in open(path, encoding="utf-8", errors="ignore"):
        l = l.split("#")[0].strip().lower()
        if not l: continue
        if hosts:
            p = l.split()
            if len(p) < 2: continue
            l = p[1]
        l = l.strip(".")
        if re.fullmatch(r"[a-z0-9._-]+", l) and "." in l and len(l) <= 80: s.add(l)
    return s

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--a", required=True); ap.add_argument("--b", required=True)
    ap.add_argument("--budget", type=int, default=330000)
    a = ap.parse_args()
    both = read(a.a, True) & read(a.b, False)
    both = {n for n in both if not covered(n)}
    both = {n for n in both if not any(s in both for s in g.suffixes(n)[1:])}
    keep = set(g.read_list(os.path.join(g.ROOT, "blocklist/keep.txt")))
    both = {n for n in both if not any(k == n or k.endswith("." + n) for k in keep)}
    order = sorted(both, key=lambda n: (len(n), n))
    lo, hi = 1000, len(order)
    while lo < hi:                                  # largest prefix that fits the budget
        mid = (lo + hi + 1) // 2
        size = len(g.build(sorted(order[:mid]))[0])
        if size <= a.budget: lo = mid
        else: hi = mid - 1
    pick = sorted(order[:lo])
    open(os.path.join(g.ROOT, "blocklist/adult/names.txt"), "w").write("\n".join(pick) + "\n")
    print("candidates %d, kept %d, %d bytes" % (len(order), len(pick), len(g.build(pick)[0])))

main() if __name__ == "__main__" else None
