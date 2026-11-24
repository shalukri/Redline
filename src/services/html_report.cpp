#include "redline/services/html_report.hpp"

#include <algorithm>

#include "redline/algorithms/text_utils.hpp"
#include "redline/services/inline_diff.hpp"

namespace redline {
namespace {

const char* kCss = R"CSS(
:root { color-scheme: light dark;
  --bg:#f6f8fa; --fg:#1f2328; --muted:#656d76; --card:#ffffff; --line:#d0d7de; --accent:#0969da;
  --add-bg:#e6ffec; --add-mark:#aceebb; --del-bg:#ffebe9; --del-mark:#ffc1ba; --hunk:#ddf4ff; --gap:#f0f3f6; }
@media (prefers-color-scheme: dark) { :root {
  --bg:#0d1117; --fg:#e6edf3; --muted:#8d96a0; --card:#161b22; --line:#30363d; --accent:#58a6ff;
  --add-bg:#12261e; --add-mark:#1f6f3c; --del-bg:#2d1618; --del-mark:#8e2b2f; --hunk:#102a43; --gap:#1c2128; } }
* { box-sizing: border-box; }
body { margin:0; background:var(--bg); color:var(--fg);
  font:15px/1.5 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif; }
.wrap { max-width:1400px; margin:0 auto; padding:24px 16px 48px; }
h1 { margin:0; font-size:1.5rem; letter-spacing:.02em; }
.sub { margin:4px 0 20px; color:var(--muted); word-break:break-all; }
.cards { display:flex; flex-wrap:wrap; gap:12px; margin-bottom:20px; }
.card { background:var(--card); border:1px solid var(--line); border-radius:8px; padding:10px 16px; min-width:120px; }
.card b { display:block; font-size:1.6rem; line-height:1.2; }
.card span { color:var(--muted); font-size:.85rem; }
.clean { background:var(--card); border:1px solid var(--line); border-radius:8px; padding:16px; }
table.files { width:100%; border-collapse:collapse; background:var(--card); border:1px solid var(--line);
  border-radius:8px; overflow:hidden; margin-bottom:24px; }
table.files th, table.files td { text-align:left; padding:8px 12px; border-bottom:1px solid var(--line); }
table.files th { font-size:.8rem; text-transform:uppercase; letter-spacing:.05em; color:var(--muted); }
table.files tr:last-child td { border-bottom:0; }
table.files td.path { word-break:break-all; }
a { color:var(--accent); text-decoration:none; } a:hover { text-decoration:underline; }
.badge { display:inline-block; padding:1px 8px; border-radius:999px; font-size:.75rem; font-weight:600; border:1px solid var(--line); }
.badge.added { background:var(--add-bg); } .badge.removed { background:var(--del-bg); }
.badge.modified { background:var(--hunk); } .badge.binary { background:var(--gap); }
.add { color:#1a7f37; } .del { color:#cf222e; }
@media (prefers-color-scheme: dark) { .add { color:#3fb950; } .del { color:#f85149; } }
details.file { background:var(--card); border:1px solid var(--line); border-radius:8px; margin-bottom:16px; overflow:hidden; }
details.file > summary { cursor:pointer; padding:10px 14px; font-weight:600; word-break:break-all; }
details.file > summary .num { margin-left:10px; font-weight:400; font-family:ui-monospace,monospace; }
.scroll { overflow-x:auto; border-top:1px solid var(--line); }
table.diff { width:100%; min-width:720px; border-collapse:collapse; table-layout:fixed;
  font:13px/1.45 ui-monospace,SFMono-Regular,Menlo,Consolas,monospace; }
table.diff col.cln { width:3.6rem; }
table.diff td { vertical-align:top; padding:0 8px; }
td.ln { text-align:right; color:var(--muted); user-select:none; border-right:1px solid var(--line); }
td.code { white-space:pre-wrap; overflow-wrap:anywhere; tab-size:4; }
td.code.r { border-left:1px solid var(--line); }
td.code.del { background:var(--del-bg); } td.code.ins { background:var(--add-bg); }
td.code.empty { background:var(--gap); }
td.code.del mark { background:var(--del-mark); color:inherit; border-radius:2px; }
td.code.ins mark { background:var(--add-mark); color:inherit; border-radius:2px; }
tr.hunk td { background:var(--hunk); color:var(--muted); padding:2px 12px; }
tr.gap td { background:var(--gap); color:var(--muted); text-align:center; padding:2px 12px; font-style:italic; }
.noeol { color:var(--muted); font-style:italic; margin-left:8px; user-select:none; }
footer { color:var(--muted); font-size:.8rem; margin-top:32px; }
)CSS";

const char* kCols = "<colgroup><col class=\"cln\"><col><col class=\"cln\"><col></colgroup>\n";

std::string num(int n) { return n > 0 ? std::to_string(n) : ""; }

// `text` is already escaped HTML.
std::string cell(const char* side, const char* cls, const std::string& text, bool noeol) {
    std::string s = std::string("<td class=\"code ") + side + " " + cls + "\">" + text;
    if (noeol) s += "<span class=\"noeol\">no newline at end of file</span>";
    return s + "</td>";
}

void row(std::string& h, const char* rowCls, int leftNo, const std::string& left, const char* leftCls, bool leftNoeol,
         int rightNo, const std::string& right, const char* rightCls, bool rightNoeol) {
    h += std::string("<tr class=\"") + rowCls + "\"><td class=\"ln\">" + num(leftNo) + "</td>" +
         cell("l", leftCls, left, leftNoeol) + "<td class=\"ln\">" + num(rightNo) + "</td>" +
         cell("r", rightCls, right, rightNoeol) + "</tr>\n";
}

}  // namespace

std::string renderSideBySide(const TextFile& a, const TextFile& b, const FileDiff& d, const std::vector<Hunk>& hunks) {
    std::string h = "<table class=\"diff\">";
    h += kCols;

    auto lastOld = [&](int i) { return !a.endsWithNewline && static_cast<std::size_t>(i) + 1 == a.lines.size(); };
    auto lastNew = [&](int j) { return !b.endsWithNewline && static_cast<std::size_t>(j) + 1 == b.lines.size(); };
    auto gap = [&](std::size_t count) {
        if (count == 0) return;
        h += "<tr class=\"gap\"><td colspan=\"4\">" + std::to_string(count) + " unchanged line" + (count == 1 ? "" : "s") +
             " hidden</td></tr>\n";
    };

    std::size_t prevEnd = 0;
    for (const Hunk& hk : hunks) {
        gap(hk.opBegin - prevEnd);
        const bool wholeFile = hk.opBegin == 0 && hk.opEnd == d.ops.size();
        if (!wholeFile)
            h += "<tr class=\"hunk\"><td colspan=\"4\">@@ -" + std::to_string(hk.oldStart) + "," + std::to_string(hk.oldCount) +
                 " +" + std::to_string(hk.newStart) + "," + std::to_string(hk.newCount) + " @@</td></tr>\n";

        std::size_t i = hk.opBegin;
        while (i < hk.opEnd) {
            const Op& op = d.ops[i];
            if (op.kind == OpKind::Equal) {
                const std::string t = htmlEscape(a.lines[static_cast<std::size_t>(op.a)]);
                row(h, "eq", op.a + 1, t, "", lastOld(op.a), op.b + 1, t, "", lastNew(op.b));
                i++;
                continue;
            }
            // One run of changes: line up the k-th deleted line with the k-th inserted line.
            std::vector<int> dels, inss;
            std::size_t j = i;
            while (j < hk.opEnd && d.ops[j].kind != OpKind::Equal) {
                if (d.ops[j].kind == OpKind::Delete) dels.push_back(d.ops[j].a);
                else inss.push_back(d.ops[j].b);
                j++;
            }
            const std::size_t paired = std::min(dels.size(), inss.size());
            for (std::size_t k = 0; k < paired; k++) {
                HighlightedPair hp = highlightPair(a.lines[static_cast<std::size_t>(dels[k])], b.lines[static_cast<std::size_t>(inss[k])]);
                row(h, "chg", dels[k] + 1, hp.left, "del", lastOld(dels[k]), inss[k] + 1, hp.right, "ins", lastNew(inss[k]));
            }
            for (std::size_t k = paired; k < dels.size(); k++)
                row(h, "del", dels[k] + 1, htmlEscape(a.lines[static_cast<std::size_t>(dels[k])]), "del", lastOld(dels[k]), 0, "", "empty", false);
            for (std::size_t k = paired; k < inss.size(); k++)
                row(h, "ins", 0, "", "empty", false, inss[k] + 1, htmlEscape(b.lines[static_cast<std::size_t>(inss[k])]), "ins", lastNew(inss[k]));
            i = j;
        }
        prevEnd = hk.opEnd;
    }
    gap(d.ops.size() - prevEnd);
    h += "</table>\n";
    return h;
}

std::string renderPage(const std::string& title, const std::string& subtitle, const std::vector<ReportFile>& files,
                       int unchangedFiles) {
    int added = 0, removed = 0, modified = 0, binary = 0, plus = 0, minus = 0;
    for (const ReportFile& f : files) {
        if (f.status == "added") added++;
        else if (f.status == "removed") removed++;
        else if (f.status == "modified") modified++;
        else binary++;
        plus += f.added;
        minus += f.removed;
    }

    std::string h;
    h += "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
         "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n<title>" + htmlEscape(title) + "</title>\n<style>";
    h += kCss;
    h += "</style>\n</head>\n<body>\n<div class=\"wrap\">\n<h1>redline</h1>\n<p class=\"sub\">" + htmlEscape(subtitle) + "</p>\n";

    auto card = [&](int n, const char* label) { h += "<div class=\"card\"><b>" + std::to_string(n) + "</b><span>" + label + "</span></div>\n"; };
    h += "<div class=\"cards\">\n";
    card(modified, "modified");
    card(added, "added");
    card(removed, "removed");
    if (binary) card(binary, "binary / too large");
    card(unchangedFiles, "unchanged");
    h += "<div class=\"card\"><b><span class=\"add\">+" + std::to_string(plus) + "</span> <span class=\"del\">-" + std::to_string(minus) +
         "</span></b><span>lines changed</span></div>\n</div>\n";

    if (files.empty()) {
        h += "<p class=\"clean\">No differences found.</p>\n";
    } else {
        h += "<table class=\"files\">\n<tr><th>File</th><th>Status</th><th>Changes</th><th>Note</th></tr>\n";
        for (std::size_t i = 0; i < files.size(); i++) {
            const ReportFile& f = files[i];
            const std::string name = htmlEscape(f.path);
            h += "<tr data-path=\"" + name + "\" data-status=\"" + f.status + "\"><td class=\"path\">";
            h += f.diffHtml.empty() ? name : "<a href=\"#f" + std::to_string(i) + "\">" + name + "</a>";
            h += "</td><td><span class=\"badge " + f.status + "\">" + f.status + "</span></td><td>";
            if (f.added) h += "<span class=\"add\">+" + std::to_string(f.added) + "</span> ";
            if (f.removed) h += "<span class=\"del\">-" + std::to_string(f.removed) + "</span>";
            h += "</td><td>" + htmlEscape(f.detail) + "</td></tr>\n";
        }
        h += "</table>\n";

        std::size_t sections = 0;
        for (const ReportFile& f : files) sections += !f.diffHtml.empty();
        const bool openAll = sections <= 3;   // few diffs: show them right away
        for (std::size_t i = 0; i < files.size(); i++) {
            const ReportFile& f = files[i];
            if (f.diffHtml.empty()) continue;
            h += "<details class=\"file\" id=\"f" + std::to_string(i) + "\"" + (openAll ? " open" : "") + ">\n<summary>" + htmlEscape(f.path) +
                 "<span class=\"num\"><span class=\"add\">+" + std::to_string(f.added) + "</span> <span class=\"del\">-" +
                 std::to_string(f.removed) + "</span></span></summary>\n<div class=\"scroll\">\n" + f.diffHtml + "</div>\n</details>\n";
        }
    }
    h += "<footer>Generated by redline. This page is self contained: no scripts, no external files.</footer>\n</div>\n</body>\n</html>\n";
    return h;
}

}  // namespace redline
