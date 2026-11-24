#!/usr/bin/env python3
"""Check the C++ redline binary against a separate Python implementation.

For random pairs of files and random pairs of folders it checks that:
  * the printed unified diff, applied to the old file with the Python code below, gives the new file
  * the diff is minimal (changed lines == N + M - 2 * LCS, computed with a plain DP table)
  * the hunk headers are consistent and respect --context
  * the HTML report, parsed again, contains exactly the old lines on the left and the new lines on
    the right, with correct line numbers, balanced tags, no raw markup and no scripts
  * folder comparison reports the same added / removed / modified / binary files as Python

Nothing here shares code with the C++ project.

    python3 tools/verify.py --rounds 300
"""
import argparse
import os
import random
import re
import subprocess
import sys
import tempfile
from html.parser import HTMLParser

failures = 0
BIN = None


def check(ok, what):
    global failures
    if not ok:
        failures += 1
        print("    FAIL: " + what)
    return ok


def run(*args):
    p = subprocess.run([BIN, *args], capture_output=True)
    return p.returncode, p.stdout.decode("utf-8"), p.stderr.decode("utf-8")


# ------------------------------------------------------------------ text helpers

def split_text(data):
    """Same rules as the C++ side: \\n ends a line, a trailing \\r is dropped, a missing final
    newline is remembered."""
    text = data.decode("utf-8")
    if text == "":
        return [], True
    parts = text.split("\n")
    eol = parts[-1] == ""
    if eol:
        parts.pop()
    return [p[:-1] if p.endswith("\r") else p for p in parts], eol


def keys(lines, eol):
    k = list(lines)
    if lines and not eol:
        k[-1] += "\nNOEOL"
    return k


def lcs_len(a, b):
    # strip common ends first, it makes the DP much smaller for typical edits
    s = 0
    while s < len(a) and s < len(b) and a[s] == b[s]:
        s += 1
    e = 0
    while e < len(a) - s and e < len(b) - s and a[-1 - e] == b[-1 - e]:
        e += 1
    a2, b2 = a[s:len(a) - e], b[s:len(b) - e]
    prev = [0] * (len(b2) + 1)
    for x in a2:
        cur = [0]
        for j, y in enumerate(b2):
            cur.append(prev[j] + 1 if x == y else max(prev[j + 1], cur[j]))
        prev = cur
    return s + e + prev[-1]


# ------------------------------------------------------------------ unified diff applier

HUNK = re.compile(r"^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@")


def apply_unified(a_lines, a_eol, diff, ctx):
    """Returns (new_lines, new_eol, minimal_edit_count) or raises ValueError with a reason."""
    lines = diff.split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    if len(lines) < 2 or not lines[0].startswith("--- ") or not lines[1].startswith("+++ "):
        raise ValueError("missing --- / +++ header")
    out, pos, i = [], 0, 2
    changes = 0
    new_marker = False
    prev_end = None
    while i < len(lines):
        m = HUNK.match(lines[i])
        if not m:
            raise ValueError("expected a hunk header, got %r" % lines[i])
        oc = int(m.group(2)) if m.group(2) is not None else 1
        nc = int(m.group(4)) if m.group(4) is not None else 1
        os_ = int(m.group(1))
        start0 = os_ - 1 if oc > 0 else os_
        if start0 < pos:
            raise ValueError("hunks overlap or are out of order")
        if prev_end is not None and start0 - prev_end < 1:
            raise ValueError("two hunks touch each other, they should have been merged")
        out.extend(a_lines[pos:start0])
        pos = start0
        old_seen = new_seen = 0
        kinds = []
        prev_tag = None
        i += 1
        while i < len(lines) and not lines[i].startswith("@@"):
            tag, body = lines[i][:1], lines[i][1:]
            if tag == " " or tag == "-":
                if pos >= len(a_lines) or a_lines[pos] != body:
                    raise ValueError("line %d of the old file does not match the diff" % (pos + 1))
                pos += 1
                old_seen += 1
                if tag == " ":
                    out.append(body)
                    new_seen += 1
                else:
                    changes += 1
            elif tag == "+":
                out.append(body)
                new_seen += 1
                changes += 1
            elif tag == "\\":
                if prev_tag in (" ", "+"):
                    new_marker = True
            else:
                raise ValueError("bad diff line %r" % lines[i])
            if tag == "-" and prev_tag == "+":
                raise ValueError("a removed line comes after an added line in the same change")
            if tag != "\\":
                kinds.append(tag)
                prev_tag = tag
            i += 1
        if old_seen != oc or new_seen != nc:
            raise ValueError("hunk header says -%d +%d but the body has -%d +%d" % (oc, nc, old_seen, new_seen))
        lead = 0
        while lead < len(kinds) and kinds[lead] == " ":
            lead += 1
        trail = 0
        while trail < len(kinds) and kinds[-1 - trail] == " ":
            trail += 1
        if lead > ctx or trail > ctx:
            raise ValueError("more than --context lines of context")
        prev_end = pos
    tail = len(a_lines) - pos
    out.extend(a_lines[pos:])
    new_eol = a_eol if tail > 0 else (not new_marker)
    return out, new_eol, changes


# ------------------------------------------------------------------ html parsing

VOID = {"meta", "col", "br", "hr", "img", "input", "link"}


class Page(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.stack, self.errors = [], []
        self.row = self.cell = None
        self.diff_rows, self.files = [], []
        self.skip = 0
        self.scripts = 0
        self.diff_depth = 0

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        cls = set((a.get("class") or "").split())
        if tag == "script":
            self.scripts += 1
        is_diff = tag == "table" and "diff" in cls
        noeol = tag == "span" and "noeol" in cls
        if tag not in VOID:
            self.stack.append((tag, noeol, is_diff))
        if is_diff:
            self.diff_depth += 1
        if noeol:
            self.skip += 1
        if tag == "tr" and "data-path" in a:
            self.files.append((a["data-path"], a["data-status"]))
        if self.diff_depth and tag == "tr":
            self.row = []
        if self.diff_depth and tag == "td" and self.row is not None:
            self.cell = {"cls": cls, "text": []}

    def handle_endtag(self, tag):
        if tag in VOID:
            return
        if not self.stack or self.stack[-1][0] != tag:
            self.errors.append("unexpected </%s>" % tag)
            return
        _, noeol, is_diff = self.stack.pop()
        if noeol:
            self.skip -= 1
        if is_diff:
            self.diff_depth -= 1
        if tag == "td" and self.cell is not None:
            self.row.append(self.cell)
            self.cell = None
        if tag == "tr" and self.row is not None:
            self.diff_rows.append(self.row)
            self.row = None

    def handle_data(self, data):
        if self.cell is not None and self.skip == 0:
            self.cell["text"].append(data)


def parse_page(html):
    p = Page()
    p.feed(html)
    p.close()
    if p.stack:
        p.errors.append("unclosed tags: %s" % [t for t, _, _ in p.stack])
    return p


def rebuild_from_html(page):
    left, right, lnl, lnr = [], [], [], []
    for row in page.diff_rows:
        if len(row) != 4:
            continue  # hunk header or "hidden lines" row
        a_ln, a_code, b_ln, b_code = row
        if "empty" not in a_code["cls"]:
            left.append("".join(a_code["text"]))
            lnl.append(int("".join(a_ln["text"])))
        if "empty" not in b_code["cls"]:
            right.append("".join(b_code["text"]))
            lnr.append(int("".join(b_ln["text"])))
    return left, right, lnl, lnr


# ------------------------------------------------------------------ random data

WORDS = ["alpha", "beta", "gamma", "delta", "total", "count", "value", "index", "node", "item"]


def rand_line(rng):
    k = rng.randrange(8)
    if k == 0:
        return "line %d" % rng.randrange(12)
    if k == 1:
        return "x = %s + %d;  // %s" % (rng.choice(WORDS), rng.randrange(5), rng.choice(WORDS))
    if k == 2:
        return ""
    if k == 3:
        return "  \tindented %s %d" % (rng.choice(WORDS), rng.randrange(4))
    if k == 4:
        return "<b>tag & \"quote\" 'single'</b> %d" % rng.randrange(3)
    if k == 5:
        return "unicode caf\u00e9 \u2192 %d \u00fc" % rng.randrange(3)
    if k == 6:
        return "<script>alert(%d)</script>" % rng.randrange(3)
    return "long " + " ".join(rng.choice(WORDS) for _ in range(rng.randrange(1, 25)))


def mutate(rng, lines):
    b = list(lines)
    for _ in range(rng.choice([0, 1, 2, 4, 9, len(b) // 3 + 1])):
        op = rng.randrange(5)
        if op == 0 and b:
            b[rng.randrange(len(b))] = rand_line(rng)
        elif op == 1 and b:
            del b[rng.randrange(len(b))]
        elif op == 2:
            b.insert(rng.randrange(len(b) + 1), rand_line(rng))
        elif op == 3 and b:  # edit a word inside a line
            k = rng.randrange(len(b))
            b[k] = b[k].replace(rng.choice(WORDS), rng.choice(WORDS), 1) + (" " if rng.random() < 0.3 else "")
        elif op == 4 and len(b) > 3:  # move a block
            s = rng.randrange(len(b) - 2)
            block = b[s:s + 2]
            del b[s:s + 2]
            b[rng.randrange(len(b) + 1):0] = block
    return b


def to_bytes(rng, lines, eol=None, crlf=None):
    if not lines:
        return b""
    if eol is None:
        eol = rng.random() > 0.15
    if not eol and lines[-1] == "":
        eol = True  # "a\n" and "a\n" + "" cannot be told apart without a final newline
    if crlf is None:
        crlf = rng.random() < 0.15
    nl = "\r\n" if crlf else "\n"
    return (nl.join(lines) + (nl if eol else "")).encode("utf-8")


def make_pair(rng):
    n = rng.choice([0, 1, 2, 5, 20, 60, 150, 300] if rng.random() < 0.9 else [300, 400])
    a = [rand_line(rng) for _ in range(n)]
    shape = rng.random()
    if shape < 0.08:
        b = list(a)
    elif shape < 0.14:
        b = [rand_line(rng) for _ in range(rng.choice([0, 1, 30, 120]))]
    else:
        b = mutate(rng, a)
    return to_bytes(rng, a), to_bytes(rng, b)


# ------------------------------------------------------------------ checks

def check_file_pair(tmp, rng, i):
    da, db = make_pair(rng)
    pa, pb = os.path.join(tmp, "a%d.txt" % i), os.path.join(tmp, "b%d.txt" % i)
    open(pa, "wb").write(da)
    open(pb, "wb").write(db)
    ctx = rng.choice([0, 1, 3, 3, 5])
    a_lines, a_eol = split_text(da)
    b_lines, b_eol = split_text(db)
    ka, kb = keys(a_lines, a_eol), keys(b_lines, b_eol)
    minimal = len(ka) + len(kb) - 2 * lcs_len(ka, kb)
    tag = "pair %d (ctx %d, %d vs %d lines)" % (i, ctx, len(a_lines), len(b_lines))

    rc, out, err = run(pa, pb, "--context", str(ctx))
    if not check(rc == (0 if minimal == 0 else 1), "%s: exit code %d, expected %d" % (tag, rc, 0 if minimal == 0 else 1)):
        return
    if minimal == 0:
        check(out == "", tag + ": identical files must print nothing")
    else:
        try:
            new_lines, new_eol, changes = apply_unified(a_lines, a_eol, out, ctx)
            check(new_lines == b_lines, tag + ": applying the diff does not give the new file")
            check(new_eol == b_eol, tag + ": final newline flag differs after applying the diff")
            check(changes == minimal, "%s: diff is not minimal (%d changed lines, best is %d)" % (tag, changes, minimal))
        except ValueError as e:
            check(False, "%s: %s" % (tag, e))

    # whole-file HTML report, parsed back
    html_path = os.path.join(tmp, "r%d.html" % i)
    rc2, _, _ = run(pa, pb, "--html", html_path, "--full", "--context", str(ctx))
    check(rc2 == rc, tag + ": --html changed the exit code")
    page = parse_page(open(html_path, encoding="utf-8").read())
    check(not page.errors, "%s: html problems %s" % (tag, page.errors[:3]))
    check(page.scripts == 0, tag + ": html contains a script tag")
    if minimal != 0:
        left, right, lnl, lnr = rebuild_from_html(page)
        check(left == a_lines, tag + ": left side of the html is not the old file")
        check(right == b_lines, tag + ": right side of the html is not the new file")
        check(lnl == list(range(1, len(a_lines) + 1)) and lnr == list(range(1, len(b_lines) + 1)), tag + ": html line numbers are wrong")
    else:
        check("No differences found" in open(html_path, encoding="utf-8").read(), tag + ": html should say there are no differences")

    # whitespace-only edits must disappear with --ignore-space
    noisy = [l.replace(" ", "  ") + "\t " if l else l for l in a_lines]
    pn = os.path.join(tmp, "n%d.txt" % i)
    open(pn, "wb").write(to_bytes(rng, noisy, eol=a_eol))
    rc3, out3, _ = run(pa, pn, "--ignore-space")
    check(rc3 == 0 and out3 == "", tag + ": --ignore-space should see no difference")

    # tiny edit limit: the result may be "replaced", but it must still be a correct diff
    if minimal > 6:
        rc4, out4, err4 = run(pa, pb, "--max-edits", "2")
        try:
            new_lines, new_eol, _ = apply_unified(a_lines, a_eol, out4, 3)
            check(new_lines == b_lines and new_eol == b_eol, tag + ": fallback diff does not give the new file")
        except ValueError as e:
            check(False, "%s fallback: %s" % (tag, e))
        # With one side empty (after trimming) no search is needed, so the diff stays exact even
        # with a tiny limit. The message may only appear when the fallback really happened, which
        # can only be when the best diff needs more than 2 changes (always true in this branch).
        if "very different" in err4:
            check(minimal > 2, tag + ": fallback reported although the diff fits the limit")


def write_tree(root, files):
    for rel, data in files.items():
        p = os.path.join(root, rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        open(p, "wb").write(data)


def has_nul(data):
    return b"\0" in data[:8000]


def expected_folder(fa, fb, ignore):
    def ignored(rel):
        return any(part in ignore for part in rel.split("/"))
    exp, unchanged = {}, 0
    for rel in sorted(set(fa) | set(fb)):
        if ignored(rel):
            continue
        if rel in fa and rel not in fb:
            exp[rel] = ("removed", None)
        elif rel in fb and rel not in fa:
            exp[rel] = ("added", None)
        elif fa[rel] == fb[rel]:
            unchanged += 1
        elif has_nul(fa[rel]) or has_nul(fb[rel]):
            exp[rel] = ("binary", None)
        else:
            la, ea = split_text(fa[rel])
            lb, eb = split_text(fb[rel])
            ka, kb = keys(la, ea), keys(lb, eb)
            common = lcs_len(ka, kb)
            if len(ka) == common == len(kb):
                unchanged += 1
            else:
                exp[rel] = ("modified", (len(kb) - common, len(ka) - common))
    return exp, unchanged


def check_folder_pair(tmp, rng, i):
    names = ["f%d.txt" % k for k in range(6)] + ["file two.txt", "a/b.cpp", "a/c/deep.h", "z/y/x.md", "data.bin", "empty.txt", "node_modules/m.js"]
    fa, fb = {}, {}
    for rel in names:
        if rng.random() < 0.15:
            continue
        if rel.endswith(".bin"):
            base = bytes(rng.randrange(256) for _ in range(rng.randrange(1, 60))) + b"\0"
            other = base if rng.random() < 0.5 else base[:-1] + bytes([rng.randrange(256)]) + b"\0"
        elif rel == "empty.txt":
            base = other = b""
        else:
            lines = [rand_line(rng) for _ in range(rng.choice([1, 3, 10, 40]))]
            base = to_bytes(rng, lines)
            r = rng.random()
            if r < 0.35:
                other = base
            elif r < 0.5:  # only line endings differ
                other = base.replace(b"\r\n", b"\n") if b"\r\n" in base else base.replace(b"\n", b"\r\n")
            else:
                other = to_bytes(rng, mutate(rng, lines))
        place = rng.random()
        if place < 0.1:
            fa[rel] = base
        elif place < 0.2:
            fb[rel] = other
        else:
            fa[rel], fb[rel] = base, other
    fa[".git/config"], fb[".git/config"] = b"one\n", b"two\n"  # ignored by default
    ignore = {".git"}
    extra = []
    if rng.random() < 0.4:
        ignore.add("node_modules")
        extra = ["--ignore", ".git,node_modules"]

    da, db = os.path.join(tmp, "A%d" % i), os.path.join(tmp, "B%d" % i)
    os.makedirs(da)
    os.makedirs(db)
    write_tree(da, fa)
    write_tree(db, fb)
    exp, unchanged = expected_folder(fa, fb, ignore)
    tag = "folders %d" % i
    html_path = os.path.join(tmp, "F%d.html" % i)
    rc, out, err = run(da, db, "--html", html_path, *extra)
    check(rc == (1 if exp else 0), "%s: exit code %d" % (tag, rc))
    got, summary = {}, None
    paths_in_order = []
    for line in out.splitlines():
        m = re.match(r"^(added|removed|modified|binary): (.*?)(?: \((.*)\))?$", line)
        if m:
            status, rel, info = m.groups()
            counts = None
            if status == "modified":
                mm = re.match(r"^\+(\d+) -(\d+)$", info or "")
                counts = (int(mm.group(1)), int(mm.group(2))) if mm else None
            got[rel] = (status, counts)
            paths_in_order.append(rel)
        elif line.startswith("summary: "):
            summary = line
    check(got == exp, "%s: reported files differ.\n      expected %s\n      got      %s" % (tag, exp, got))
    check(paths_in_order == sorted(paths_in_order), tag + ": files are not listed in sorted order")
    n = {s: sum(1 for v in exp.values() if v[0] == s) for s in ("added", "removed", "modified", "binary")}
    want = "summary: %d added, %d removed, %d modified, %d binary, %d unchanged" % (n["added"], n["removed"], n["modified"], n["binary"], unchanged)
    check(summary == want, "%s: summary line %r, expected %r" % (tag, summary, want))
    page = parse_page(open(html_path, encoding="utf-8").read())
    check(not page.errors and page.scripts == 0, "%s: html problems %s" % (tag, page.errors[:3]))
    check(sorted(page.files) == sorted((rel, v[0]) for rel, v in exp.items()), tag + ": html summary table differs")


def check_errors(tmp):
    f = os.path.join(tmp, "f.txt")
    open(f, "w").write("x\n")
    d = os.path.join(tmp, "dir")
    os.makedirs(d, exist_ok=True)
    check(run(f, os.path.join(tmp, "missing"))[0] == 2, "missing file should exit with 2")
    check(run(f, d)[0] == 2, "file vs folder should exit with 2")
    check(run(f)[0] == 2, "one argument should exit with 2")
    check(run(f, f, "--context", "abc")[0] == 2, "bad --context should exit with 2")
    check(run(f, f, "--max-edits", "0")[0] == 2, "bad --max-edits should exit with 2")
    check(run(f, f, "--nope")[0] == 2, "unknown option should exit with 2")
    b1, b2 = os.path.join(tmp, "b1"), os.path.join(tmp, "b2")
    open(b1, "wb").write(b"a\0b")
    open(b2, "wb").write(b"a\0c")
    rc, out, _ = run(b1, b2)
    check(rc == 1 and "Binary files" in out, "binary files should say so")
    check(run(b1, b1)[0] == 0, "same binary file is not a difference")


def main():
    global BIN
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bin", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "redline"))
    ap.add_argument("--rounds", type=int, default=200)
    ap.add_argument("--seed", type=int, default=1)
    args = ap.parse_args()
    BIN = args.bin
    rng = random.Random(args.seed)
    with tempfile.TemporaryDirectory() as tmp:
        print("file pairs: %d random pairs (unified diff applied, minimality, html round trip)" % args.rounds)
        def guarded(fn, label, *a):
            try:
                fn(*a)
            except Exception as e:  # for example the binary crashed and wrote no html file
                check(False, "%s: check crashed with %s: %s" % (label, type(e).__name__, e))

        for i in range(args.rounds):
            guarded(check_file_pair, "pair %d" % i, tmp, rng, i)
        folders = max(1, args.rounds // 5)
        print("folder pairs: %d random trees" % folders)
        for i in range(folders):
            guarded(check_folder_pair, "folders %d" % i, tmp, rng, i)
        print("error handling")
        guarded(check_errors, "errors", tmp)
    print("RESULT: " + ("ok" if failures == 0 else "%d check(s) failed" % failures))
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
