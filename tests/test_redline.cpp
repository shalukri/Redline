// Small test program without a framework. A check only prints something when it fails.
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include "redline/algorithms/myers.hpp"
#include "redline/algorithms/text_utils.hpp"
#include "redline/services/folder_compare.hpp"
#include "redline/services/html_report.hpp"
#include "redline/services/inline_diff.hpp"
#include "redline/services/text_diff.hpp"
#include "redline/services/unified_writer.hpp"

using namespace redline;
namespace fs = std::filesystem;

static int failures = 0, checks = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        checks++;                                                            \
        if (!(cond)) {                                                       \
            failures++;                                                      \
            std::printf("  FAILED line %d: %s\n", __LINE__, #cond);          \
        }                                                                    \
    } while (0)

static void run(const char* name, const std::function<void()>& fn) {
    const int before = failures;
    fn();
    std::printf("[%s] %s\n", failures == before ? " ok " : "FAIL", name);
}

// length of the longest common subsequence, the plain O(N*M) way
static int lcsLength(const std::vector<int>& a, const std::vector<int>& b) {
    std::vector<std::vector<int>> t(a.size() + 1, std::vector<int>(b.size() + 1, 0));
    for (std::size_t i = 1; i <= a.size(); i++)
        for (std::size_t j = 1; j <= b.size(); j++)
            t[i][j] = a[i - 1] == b[j - 1] ? t[i - 1][j - 1] + 1 : std::max(t[i - 1][j], t[i][j - 1]);
    return t[a.size()][b.size()];
}

// Replays an edit script and checks that it really turns a into b and uses every index once, in order.
static bool scriptIsValid(const std::vector<int>& a, const std::vector<int>& b, const std::vector<Op>& ops) {
    int na = 0, nb = 0;
    for (const Op& op : ops) {
        if (op.kind == OpKind::Equal) {
            if (op.a != na || op.b != nb || a[na] != b[nb]) return false;
            na++; nb++;
        } else if (op.kind == OpKind::Delete) {
            if (op.a != na || op.b != -1) return false;
            na++;
        } else {
            if (op.b != nb || op.a != -1) return false;
            nb++;
        }
    }
    return na == static_cast<int>(a.size()) && nb == static_cast<int>(b.size());
}

static int countChanges(const std::vector<Op>& ops) {
    int c = 0;
    for (const Op& o : ops) c += o.kind != OpKind::Equal;
    return c;
}

static TextFile text(const std::string& s) { return splitText(s); }

// ---------------------------------------------------------------- text utilities

static void testSplitText() {
    TextFile f = text("a\nb\n");
    CHECK(f.lines == (std::vector<std::string>{"a", "b"}) && f.endsWithNewline && !f.hadCrlf);
    f = text("a\nb");
    CHECK(f.lines.size() == 2 && !f.endsWithNewline);
    f = text("");
    CHECK(f.lines.empty() && f.endsWithNewline);
    f = text("\n");
    CHECK(f.lines == (std::vector<std::string>{""}) && f.endsWithNewline);
    f = text("a\n\n\nb\n");
    CHECK(f.lines == (std::vector<std::string>{"a", "", "", "b"}));
    f = text("a\r\nb\r\n");
    CHECK(f.lines == (std::vector<std::string>{"a", "b"}) && f.hadCrlf);
    f = text("a\r\nb");
    CHECK(f.lines.size() == 2 && !f.endsWithNewline && f.hadCrlf);
    f = text("tab\there \r\n");
    CHECK(f.lines[0] == "tab\there ");   // only the \r is removed
}

static void testUtils() {
    CHECK(normalizeSpace("  a \t b   c  ") == "a b c");
    CHECK(normalizeSpace("   ") == "");
    CHECK(normalizeSpace("x") == "x");
    CHECK(htmlEscape("<a href=\"x\">&'</a>") == "&lt;a href=&quot;x&quot;&gt;&amp;&#39;&lt;/a&gt;");
    CHECK(htmlEscape("plain") == "plain");

    std::string zeros(8000, 'a');
    CHECK(!looksBinary(zeros));
    zeros[7999] = '\0';
    CHECK(looksBinary(zeros));
    zeros[7999] = 'a';
    zeros.push_back('\0');   // a NUL after the first 8000 bytes is ignored, like git does
    CHECK(!looksBinary(zeros));
    CHECK(!looksBinary(""));

    auto t = tokenize("foo(bar, 42);  x_1");
    CHECK(t == (std::vector<std::string>{"foo", "(", "bar", ",", " ", "42", ")", ";", "  ", "x_1"}));
    CHECK(tokenize("").empty());
    auto u = tokenize("caf\xC3\xA9 \xE2\x86\x92 ok");   // UTF-8 characters stay in one piece
    CHECK(u == (std::vector<std::string>{"caf\xC3\xA9", " ", "\xE2\x86\x92", " ", "ok"}));
}

// ---------------------------------------------------------------- myers

static void testMyersBasics() {
    std::vector<int> e;
    auto r = myersDiff(e, e, 10);
    CHECK(r && r->empty());
    r = myersDiff({1, 2, 3}, {1, 2, 3}, 0 + 1);     // identical needs zero edits, even with a tiny limit
    CHECK(r && countChanges(*r) == 0 && r->size() == 3);
    r = myersDiff({}, {1, 2}, 1);                   // pure inserts never need the search
    CHECK(r && countChanges(*r) == 2);
    r = myersDiff({1, 2}, {}, 1);
    CHECK(r && countChanges(*r) == 2);
    r = myersDiff({1, 2, 3}, {4, 5, 6}, 100);
    CHECK(r && countChanges(*r) == 6);
    // the classic example from the paper: ABCABBA -> CBABAC needs 5 edits
    std::vector<int> a = {'A', 'B', 'C', 'A', 'B', 'B', 'A'}, b = {'C', 'B', 'A', 'B', 'A', 'C'};
    r = myersDiff(a, b, 100);
    CHECK(r && countChanges(*r) == 5 && scriptIsValid(a, b, *r));
}

static void testMyersLimit() {
    std::vector<int> a = {1, 2, 3, 4}, b = {5, 6, 7, 8};   // needs exactly 8 edits
    CHECK(!myersDiff(a, b, 7));
    CHECK(myersDiff(a, b, 8).has_value());
    CHECK(!myersDiff({1, 2, 3}, {1, 9, 3}, 1));            // needs 2 (one delete, one insert)
    CHECK(myersDiff({1, 2, 3}, {1, 9, 3}, 2).has_value());
}

static void testMyersRandom() {
    std::mt19937 rng(2024);
    for (int round = 0; round < 3000; round++) {
        const int alphabet = 1 + static_cast<int>(rng() % 6);
        std::vector<int> a(rng() % 40), b(rng() % 40);
        for (int& x : a) x = static_cast<int>(rng() % alphabet);
        for (int& x : b) x = static_cast<int>(rng() % alphabet);
        auto r = myersDiff(a, b, 1000);
        CHECK(r.has_value());
        if (!r) continue;
        CHECK(scriptIsValid(a, b, *r));
        // minimal: edits = N + M - 2 * LCS
        CHECK(countChanges(*r) == static_cast<int>(a.size() + b.size()) - 2 * lcsLength(a, b));
        // deletes before inserts inside every run of changes
        for (std::size_t i = 1; i < r->size(); i++)
            if ((*r)[i - 1].kind == OpKind::Insert) CHECK((*r)[i].kind != OpKind::Delete);
    }
}

// ---------------------------------------------------------------- file diff

static std::vector<std::string> replay(const TextFile& a, const TextFile& b, const FileDiff& d) {
    std::vector<std::string> out;
    for (const Op& op : d.ops) {
        if (op.kind == OpKind::Equal) out.push_back(a.lines[static_cast<std::size_t>(op.a)]);
        else if (op.kind == OpKind::Insert) out.push_back(b.lines[static_cast<std::size_t>(op.b)]);
    }
    return out;
}

static void testDiffFiles() {
    DiffOptions opt;
    TextFile a = text("a\nb\nc\nd\n"), b = text("a\nB\nc\nd\ne\n");
    FileDiff d = diffFiles(a, b, opt);
    CHECK(d.exact && d.stats.removed == 1 && d.stats.added == 2 && d.stats.unchanged == 3);
    CHECK(replay(a, b, d) == b.lines);

    d = diffFiles(text(""), text(""), opt);
    CHECK(d.ops.empty());
    d = diffFiles(text("x\n"), text("x\n"), opt);
    CHECK(d.stats.added == 0 && d.stats.removed == 0 && d.stats.unchanged == 1);

    // missing newline at the end is a change of that line
    d = diffFiles(text("x\ny\n"), text("x\ny"), opt);
    CHECK(d.stats.removed == 1 && d.stats.added == 1 && d.stats.unchanged == 1);
    d = diffFiles(text("x\ny"), text("x\ny"), opt);
    CHECK(d.stats.removed == 0 && d.stats.added == 0);

    // CRLF against LF is NOT a change (the \r is dropped while reading)
    d = diffFiles(text("a\r\nb\r\n"), text("a\nb\n"), opt);
    CHECK(d.stats.removed == 0 && d.stats.added == 0);

    // whitespace only matters when not ignored
    d = diffFiles(text("a  b\n\tc\n"), text("a b\nc\n"), opt);
    CHECK(d.stats.removed == 2);
    opt.ignoreSpace = true;
    d = diffFiles(text("a  b\n\tc\n"), text("a b\nc  \n"), opt);
    CHECK(d.stats.removed == 0 && d.stats.added == 0);
    opt.ignoreSpace = false;

    // too different for the limit: reported as not exact but still a valid script
    opt.maxEdits = 3;
    TextFile x = text("1\n2\n3\n4\n5\n"), y = text("a\nb\nc\nd\ne\n");
    d = diffFiles(x, y, opt);
    CHECK(!d.exact && d.stats.removed == 5 && d.stats.added == 5 && replay(x, y, d) == y.lines);
}

static void testDiffFilesRandom() {
    std::mt19937 rng(7);
    DiffOptions opt;
    for (int round = 0; round < 1500; round++) {
        auto make = [&](int n) {
            std::string s;
            for (int i = 0; i < n; i++) s += "line" + std::to_string(rng() % 8) + "\n";
            if (rng() % 4 == 0 && !s.empty()) s.pop_back();   // sometimes no newline at the end
            return text(s);
        };
        TextFile a = make(static_cast<int>(rng() % 30)), b = make(static_cast<int>(rng() % 30));
        FileDiff d = diffFiles(a, b, opt);
        CHECK(d.exact);
        CHECK(replay(a, b, d) == b.lines);
        CHECK(d.stats.unchanged + d.stats.removed == static_cast<int>(a.lines.size()));
        CHECK(d.stats.unchanged + d.stats.added == static_cast<int>(b.lines.size()));
    }
}

// ---------------------------------------------------------------- hunks and unified output

static std::vector<Op> opsFromPattern(const std::string& p) {   // '=' equal, '-' delete, '+' insert
    std::vector<Op> ops;
    int a = 0, b = 0;
    for (char c : p) {
        if (c == '=') ops.push_back({OpKind::Equal, a++, b++});
        else if (c == '-') ops.push_back({OpKind::Delete, a++, -1});
        else ops.push_back({OpKind::Insert, -1, b++});
    }
    return ops;
}

static void testHunks() {
    auto ops = opsFromPattern("====-====");
    auto h = makeHunks(ops, 2, false);
    CHECK(h.size() == 1 && h[0].opBegin == 2 && h[0].opEnd == 7);
    CHECK(h[0].oldStart == 3 && h[0].oldCount == 5 && h[0].newStart == 3 && h[0].newCount == 4);

    // two changes with exactly 2*context unchanged lines in between are merged, one more is not
    CHECK(makeHunks(opsFromPattern("=-====+="), 2, false).size() == 1);
    CHECK(makeHunks(opsFromPattern("=-=====+="), 2, false).size() == 2);
    CHECK(makeHunks(opsFromPattern("=-==+="), 0, false).size() == 2);

    CHECK(makeHunks(opsFromPattern("====="), 3, false).empty());
    CHECK(makeHunks({}, 3, false).empty());
    h = makeHunks(opsFromPattern("====="), 3, true);   // --full keeps unchanged files too
    CHECK(h.size() == 1 && h[0].opBegin == 0 && h[0].opEnd == 5);

    // a side with no lines starts at the line BEFORE the hunk (unified diff rule)
    h = makeHunks(opsFromPattern("==++"), 0, false);
    CHECK(h.size() == 1 && h[0].oldCount == 0 && h[0].oldStart == 2 && h[0].newStart == 3 && h[0].newCount == 2);
    h = makeHunks(opsFromPattern("--=="), 0, false);
    CHECK(h[0].oldStart == 1 && h[0].oldCount == 2 && h[0].newCount == 0 && h[0].newStart == 0);
}

static std::string unified(const std::string& x, const std::string& y, int ctx = 3) {
    TextFile a = text(x), b = text(y);
    FileDiff d = diffFiles(a, b, DiffOptions{});
    return formatUnified("old", "new", a, b, d, makeHunks(d.ops, ctx, false), false);
}

static void testUnified() {
    CHECK(unified("a\nb\n", "a\nb\n") == "");
    CHECK(unified("a\nb\nc\n", "a\nX\nc\n", 1) == "--- old\n+++ new\n@@ -1,3 +1,3 @@\n a\n-b\n+X\n c\n");
    CHECK(unified("", "a\n") == "--- old\n+++ new\n@@ -0,0 +1,1 @@\n+a\n");
    CHECK(unified("a\n", "") == "--- old\n+++ new\n@@ -1,1 +0,0 @@\n-a\n");
    CHECK(unified("a\nb\n", "a\nb", 1) ==
          "--- old\n+++ new\n@@ -1,2 +1,2 @@\n a\n-b\n+b\n\\ No newline at end of file\n");
    CHECK(unified("a\nb", "a\nb") == "");
    // colours only add escape codes around lines, the text stays the same
    TextFile a = text("x\n"), b = text("y\n");
    FileDiff d = diffFiles(a, b, DiffOptions{});
    std::string c = formatUnified("o", "n", a, b, d, makeHunks(d.ops, 3, false), true);
    CHECK(c.find("\033[31m-x") != std::string::npos && c.find("\033[32m+y") != std::string::npos);
}

// ---------------------------------------------------------------- html

static std::size_t countOf(const std::string& s, const std::string& what) {
    std::size_t n = 0, pos = 0;
    while ((pos = s.find(what, pos)) != std::string::npos) { n++; pos += what.size(); }
    return n;
}

static void testInlineDiff() {
    HighlightedPair p = highlightPair("int total = count + 1;", "int total = count + 2;");
    CHECK(p.highlighted);
    CHECK(p.left == "int total = count + <mark>1</mark>;" && p.right == "int total = count + <mark>2</mark>;");

    p = highlightPair("alpha beta gamma", "completely different words here");
    CHECK(!p.highlighted && p.left.find("<mark>") == std::string::npos);

    p = highlightPair("<b>x</b>", "<i>x</i>");   // markup in the file must come out escaped
    CHECK(p.left.find("<b>") == std::string::npos && p.left.find("&lt;") != std::string::npos);

    p = highlightPair("same", "same");
    CHECK(p.left == "same" && p.right == "same");
    p = highlightPair("", "x");
    CHECK(!p.highlighted && p.right == "x");
}

static void testHtml() {
    TextFile a = text("keep\n<script>alert(1)</script>\nold & line\nend\n");
    TextFile b = text("keep\nnew & line\nend\nextra\n");
    FileDiff d = diffFiles(a, b, DiffOptions{});
    std::string html = renderSideBySide(a, b, d, makeHunks(d.ops, 3, true));
    CHECK(html.find("<script>") == std::string::npos);                 // never raw
    CHECK(html.find("&lt;script&gt;") != std::string::npos);
    // rows: keep, two deleted lines (one paired with the insert), end, extra = 5, and no hunk header for a whole file
    CHECK(countOf(html, "<tr ") == 5);
    CHECK(countOf(html, "class=\"code l ") == countOf(html, "class=\"code r "));

    // with context 0 the unchanged lines turn into "hidden" rows
    TextFile c = text("1\n2\n3\n4\n5\n6\n7\n"), e = text("1\n2\n3\nX\n5\n6\n7\n");
    FileDiff d2 = diffFiles(c, e, DiffOptions{});
    std::string h2 = renderSideBySide(c, e, d2, makeHunks(d2.ops, 0, false));
    CHECK(h2.find("3 unchanged lines hidden") != std::string::npos && countOf(h2, "unchanged line") == 2);
    CHECK(renderSideBySide(c, e, d2, makeHunks(d2.ops, 1, false)).find("2 unchanged lines hidden") != std::string::npos);

    // the page: totals, escaped paths, no external references
    ReportFile f;
    f.path = "dir/<weird>&\"name\".txt";
    f.status = "modified";
    f.added = 2;
    f.removed = 1;
    f.diffHtml = html;
    std::string page = renderPage("t", "sub", {f}, 4);
    CHECK(page.find("<weird>") == std::string::npos && page.find("&lt;weird&gt;&amp;&quot;name&quot;") != std::string::npos);
    CHECK(page.find("data-status=\"modified\"") != std::string::npos);
    CHECK(page.find("<script") == std::string::npos && page.find("http://") == std::string::npos && page.find("https://") == std::string::npos);
    CHECK(renderPage("t", "s", {}, 3).find("No differences found") != std::string::npos);
}

// ---------------------------------------------------------------- folders

struct TempDir {
    fs::path root;
    TempDir() {
        std::random_device rd;
        root = fs::temp_directory_path() / ("redline_test_" + std::to_string(rd()) + std::to_string(rd()));
        fs::create_directories(root);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(root, ec); }
    void write(const std::string& rel, const std::string& content) const {
        fs::create_directories((root / rel).parent_path());
        std::ofstream(root / rel, std::ios::binary) << content;
    }
};

static const ReportFile* find(const FolderResult& r, const std::string& path) {
    for (const ReportFile& f : r.files)
        if (f.path == path) return &f;
    return nullptr;
}

static void testFolders() {
    TempDir A, B;
    A.write("same.txt", "hello\n");           B.write("same.txt", "hello\n");
    A.write("crlf.txt", "a\r\nb\r\n");        B.write("crlf.txt", "a\nb\n");            // only line endings differ
    A.write("edit.txt", "1\n2\n3\n");         B.write("edit.txt", "1\n2 changed\n3\n");
    A.write("gone.txt", "bye\n");
    B.write("new.txt", "hi\nthere\n");
    A.write("sub/deep/x.cpp", "int a;\n");    B.write("sub/deep/x.cpp", "int b;\n");
    A.write("sub/only_a/f.txt", "x\n");
    B.write("sub/only_b/f.txt", "y\n");
    A.write("img.bin", std::string("\0\1\2", 3)); B.write("img.bin", std::string("\0\1\3", 3));
    A.write("same.bin", std::string("\0\1", 2));  B.write("same.bin", std::string("\0\1", 2));
    A.write("empty.txt", "");                 B.write("empty.txt", "");
    A.write("file with space.txt", "a\n");    B.write("file with space.txt", "b\n");
    A.write(".git/config", "one\n");          B.write(".git/config", "two\n");            // ignored by default
    A.write("build/out.o", "x\n");            B.write("build/out.o", "y\n");

    FolderOptions opt;
    FolderResult r = compareFolders(A.root, B.root, opt);
    CHECK(r.warnings.empty());
    CHECK(find(r, "same.txt") == nullptr && find(r, "crlf.txt") == nullptr && find(r, ".git/config") == nullptr);
    CHECK(r.unchanged == 4);   // same.txt, crlf.txt, same.bin, empty.txt
    CHECK(find(r, "edit.txt") && find(r, "edit.txt")->status == "modified" && find(r, "edit.txt")->added == 1 && find(r, "edit.txt")->removed == 1);
    CHECK(find(r, "edit.txt") && !find(r, "edit.txt")->diffHtml.empty());
    CHECK(find(r, "gone.txt") && find(r, "gone.txt")->status == "removed" && find(r, "gone.txt")->removed == 1);
    CHECK(find(r, "new.txt") && find(r, "new.txt")->status == "added" && find(r, "new.txt")->added == 2);
    CHECK(find(r, "sub/deep/x.cpp") && find(r, "sub/deep/x.cpp")->status == "modified");
    CHECK(find(r, "sub/only_a/f.txt") && find(r, "sub/only_a/f.txt")->status == "removed");
    CHECK(find(r, "sub/only_b/f.txt") && find(r, "sub/only_b/f.txt")->status == "added");
    CHECK(find(r, "img.bin") && find(r, "img.bin")->status == "binary");
    CHECK(find(r, "file with space.txt") && find(r, "file with space.txt")->status == "modified");
    CHECK(find(r, "build/out.o") && find(r, "build/out.o")->status == "modified");
    CHECK(std::is_sorted(r.files.begin(), r.files.end(), [](const ReportFile& x, const ReportFile& y) { return x.path < y.path; }));

    opt.ignore = {".git", "build"};
    r = compareFolders(A.root, B.root, opt);
    CHECK(find(r, "build/out.o") == nullptr);
    opt.ignore = {"deep"};   // a folder name is matched at any depth
    r = compareFolders(A.root, B.root, opt);
    CHECK(find(r, "sub/deep/x.cpp") == nullptr && find(r, ".git/config") != nullptr);

    opt = FolderOptions{};
    opt.diff.ignoreSpace = true;
    TempDir C, D;
    C.write("a.txt", "x  y\n");  D.write("a.txt", "x y\n");
    CHECK(compareFolders(C.root, D.root, opt).files.empty());

    // empty folders, and a folder that does not exist
    TempDir E, F;
    r = compareFolders(E.root, F.root, FolderOptions{});
    CHECK(r.files.empty() && r.unchanged == 0 && r.warnings.empty());
    r = compareFolders(E.root / "missing", F.root, FolderOptions{});
    CHECK(!r.warnings.empty());

    // a name that is a file on one side and a folder on the other shows up as removed + added
    TempDir G, H;
    G.write("x", "file\n");  H.write("x/y.txt", "inside\n");
    r = compareFolders(G.root, H.root, FolderOptions{});
    CHECK(find(r, "x") && find(r, "x")->status == "removed" && find(r, "x/y.txt") && find(r, "x/y.txt")->status == "added");
}

int main() {
    run("split text: LF, CRLF, missing final newline, empty", testSplitText);
    run("utilities: spaces, escaping, binary rule, tokens", testUtils);
    run("myers: basics, paper example, empty sides", testMyersBasics);
    run("myers: edit limit is exact", testMyersLimit);
    run("myers: 3000 random pairs against the LCS table", testMyersRandom);
    run("file diff: trimming, EOF newline, CRLF, ignore-space, fallback", testDiffFiles);
    run("file diff: 1500 random files replay correctly", testDiffFilesRandom);
    run("hunks: context, merging, start numbers", testHunks);
    run("unified output: exact text for small cases", testUnified);
    run("inline diff: word marks, unrelated lines, escaping", testInlineDiff);
    run("html: escaping, hidden-line rows, page totals", testHtml);
    run("folders: every status, ignore list, edge cases", testFolders);
    std::printf("\n%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
