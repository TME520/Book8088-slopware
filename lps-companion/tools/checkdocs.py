#!/usr/bin/env python3
"""Documentation check (LPS-DOS-115-CHG16), run by `make test`.

The files to copy to the Book 8088 and their sizes appear in several
documents. This checks them against the files at the repository root, so a
rebuilt EXE or DAT file cannot leave a stale size or file list behind:
  - BOOK8088-TEST.md: exact sizes, FR table ("106 416 o") and EN list
    ("106,416 B"); HELLO.EXE in both;
  - README.md (EN and FR) and ../README.md: every file named, sizes in KB/Ko
    rounded to the nearest KiB, the four files to copy listed together;
  - dosbox.conf: the four files in the copy hint.
Python 3 standard library only. Exit status 1 with one line per problem."""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
COMP = os.path.dirname(HERE)
ROOT = os.path.dirname(COMP)
COPY = ["LPSCOMP.EXE", "ROSARY.DAT", "MEALS.DAT", "MEALSCGA.DAT"]
ALL = COPY + ["HELLO.EXE"]


def text(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def main():
    problems = []

    def need(doc, body, needle):
        if needle not in body:
            problems.append("%s: missing %r" % (doc, needle))

    size = dict((n, os.path.getsize(os.path.join(ROOT, n))) for n in ALL)
    kib = dict((n, int(size[n] / 1024.0 + 0.5)) for n in ALL)
    book = text(os.path.join(COMP, "BOOK8088-TEST.md"))
    for n in COPY:
        need("BOOK8088-TEST.md", book, "| `%s` | %s o |" % (n, "{:,}".format(size[n]).replace(",", " ")))
        need("BOOK8088-TEST.md", book, "`%s` (%s B)" % (n, "{:,}".format(size[n])))
    need("BOOK8088-TEST.md", book, "`HELLO.EXE` (%s o)" % "{:,}".format(size["HELLO.EXE"]).replace(",", " "))

    readme = text(os.path.join(COMP, "README.md"))
    need("README.md", readme, "`LPSCOMP.EXE`, `ROSARY.DAT`, `MEALS.DAT` and `MEALSCGA.DAT`")
    need("README.md", readme, "`LPSCOMP.EXE`, `ROSARY.DAT`, `MEALS.DAT` et `MEALSCGA.DAT`")
    for n in COPY:
        need("README.md", readme, "`%s` (~%d KB)" % (n, kib[n]))
        need("README.md", readme, "`%s` (~%d Ko)" % (n, kib[n]))
    for n in ALL:
        need("README.md", readme, "[`../%s`](../%s)" % (n, n))
    need("README.md", readme, "cp ../%s ../HELLO.EXE ." % " ../".join(COPY))

    top = text(os.path.join(ROOT, "README.md"))
    for n in ALL:
        need("../README.md", top, "[`%s`](%s)" % (n, n))
    need("../README.md", top, "`LPSCOMP.EXE`, `ROSARY.DAT`, `MEALS.DAT` and `MEALSCGA.DAT`")

    conf = text(os.path.join(COMP, "dosbox.conf"))
    need("dosbox.conf", conf, "cp ../%s ." % " ../".join(COPY))

    for p in problems:
        print("checkdocs: " + p)
    if problems:
        return 1
    print("checkdocs: file list and sizes match the root files (%s)" % ", ".join("%s %d B" % (n, size[n]) for n in ALL))
    return 0


if __name__ == "__main__":
    sys.exit(main())
