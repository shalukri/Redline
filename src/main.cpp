// redline: compare two files or two folders, print a unified diff, write a side by side HTML report.
//
//   redline <old> <new> [options]
//
// Exit codes follow GNU diff so it can be used in scripts: 0 no differences, 1 differences, 2 error.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "redline/algorithms/text_utils.hpp"
#include "redline/services/folder_compare.hpp"
#include "redline/services/html_report.hpp"
#include "redline/services/text_diff.hpp"
#include "redline/services/unified_writer.hpp"

namespace fs = std::filesystem;
using namespace redline;

namespace {

int usage() {
    std::fprintf(stderr,
        "usage: redline <old> <new> [options]\n"
        "  <old> and <new> are two files or two folders\n\n"
        "options:\n"
        "  --html FILE      also write a side by side HTML report\n"
        "  --context N      unchanged lines shown around a change (default 3)\n"
        "  --full           show whole files in the HTML report\n"
        "  --ignore-space   ignore differences in spaces and tabs\n"
        "  --color          colour the terminal output\n"
        "  --ignore a,b     folders only: skip files or folders with these names (default .git)\n"
        "  --max-edits N    give up on an exact diff after N changed lines (default 4000, max 10000)\n"
        "exit code: 0 same, 1 different, 2 error\n");
    return 2;
}

struct Args {
    std::string oldPath, newPath, html, ignore;
    int context = 3;
    std::size_t maxEdits = 4000;
    bool full = false, ignoreSpace = false, color = false, hasIgnore = false;
};

bool parseInt(const char* s, long long lo, long long hi, long long& out) {
    char* end = nullptr;
    long long v = std::strtoll(s, &end, 10);
    if (end == s || *end != '\0' || v < lo || v > hi) return false;
    out = v;
    return true;
}

// returns 0 when fine, 2 for bad usage
int parseArgs(int argc, char** argv, Args& a) {
    std::vector<std::string> positional;
    for (int i = 1; i < argc; i++) {
        const std::string s = argv[i];
        auto needValue = [&]() -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s needs a value\n", s.c_str());
                return nullptr;
            }
            return argv[++i];
        };
        long long n = 0;
        if (s == "--html") { const char* v = needValue(); if (!v) return 2; a.html = v; }
        else if (s == "--ignore") { const char* v = needValue(); if (!v) return 2; a.ignore = v; a.hasIgnore = true; }
        else if (s == "--context") {
            const char* v = needValue();
            if (!v || !parseInt(v, 0, 1000, n)) { std::fprintf(stderr, "--context must be a number from 0 to 1000\n"); return 2; }
            a.context = static_cast<int>(n);
        } else if (s == "--max-edits") {
            const char* v = needValue();
            if (!v || !parseInt(v, 1, 10000, n)) { std::fprintf(stderr, "--max-edits must be a number from 1 to 10000\n"); return 2; }
            a.maxEdits = static_cast<std::size_t>(n);
        } else if (s == "--full") a.full = true;
        else if (s == "--ignore-space") a.ignoreSpace = true;
        else if (s == "--color") a.color = true;
        else if (s.rfind("--", 0) == 0) { std::fprintf(stderr, "unknown option %s\n", s.c_str()); return 2; }
        else positional.push_back(s);
    }
    if (positional.size() != 2) return 2;
    a.oldPath = positional[0];
    a.newPath = positional[1];
    return 0;
}

bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool writeFile(const std::string& path, const std::string& content) {
    std::ofstream f(path, std::ios::binary);
    f << content;
    return static_cast<bool>(f);
}

std::set<std::string> splitList(const std::string& s) {
    std::set<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ','))
        if (!item.empty()) out.insert(item);
    return out;
}

int diffTwoFiles(const Args& a) {
    std::string ca, cb;
    if (!readFile(a.oldPath, ca)) { std::fprintf(stderr, "redline: cannot read '%s'\n", a.oldPath.c_str()); return 2; }
    if (!readFile(a.newPath, cb)) { std::fprintf(stderr, "redline: cannot read '%s'\n", a.newPath.c_str()); return 2; }

    if (looksBinary(ca) || looksBinary(cb)) {
        if (ca == cb) return 0;
        std::printf("Binary files %s and %s differ\n", a.oldPath.c_str(), a.newPath.c_str());
        return 1;
    }

    DiffOptions opt;
    opt.ignoreSpace = a.ignoreSpace;
    opt.maxEdits = a.maxEdits;
    const TextFile ta = splitText(ca), tb = splitText(cb);
    const FileDiff d = diffFiles(ta, tb, opt);
    const bool same = d.stats.added == 0 && d.stats.removed == 0;

    std::fputs(formatUnified(a.oldPath, a.newPath, ta, tb, d, makeHunks(d.ops, a.context, false), a.color).c_str(), stdout);
    if (!d.exact) std::fprintf(stderr, "redline: files are very different, the changed part is shown as replaced (see --max-edits)\n");

    if (!a.html.empty()) {
        std::vector<ReportFile> files;
        if (!same) {
            ReportFile f;
            f.path = a.newPath;
            f.status = "modified";
            f.added = d.stats.added;
            f.removed = d.stats.removed;
            if (!d.exact) f.detail = "very different, shown as replaced";
            f.diffHtml = renderSideBySide(ta, tb, d, makeHunks(d.ops, a.context, a.full));
            files.push_back(std::move(f));
        }
        const std::string page = renderPage(a.oldPath + " vs " + a.newPath, a.oldPath + "  ->  " + a.newPath, files, same ? 1 : 0);
        if (!writeFile(a.html, page)) { std::fprintf(stderr, "redline: cannot write '%s'\n", a.html.c_str()); return 2; }
        std::fprintf(stderr, "wrote %s\n", a.html.c_str());
    }
    return same ? 0 : 1;
}

int diffTwoFolders(const Args& a) {
    FolderOptions opt;
    if (a.hasIgnore) opt.ignore = splitList(a.ignore);
    opt.diff.ignoreSpace = a.ignoreSpace;
    opt.diff.maxEdits = a.maxEdits;
    opt.context = a.context;
    opt.full = a.full;
    opt.wantHtml = !a.html.empty();

    const FolderResult res = compareFolders(a.oldPath, a.newPath, opt);
    int added = 0, removed = 0, modified = 0, binary = 0;
    for (const ReportFile& f : res.files) {
        std::printf("%s: %s", f.status.c_str(), f.path.c_str());
        if (f.status == "modified") std::printf(" (+%d -%d)", f.added, f.removed);
        else if (!f.detail.empty()) std::printf(" (%s)", f.detail.c_str());
        std::printf("\n");
        if (f.status == "added") added++;
        else if (f.status == "removed") removed++;
        else if (f.status == "modified") modified++;
        else binary++;
    }
    std::printf("summary: %d added, %d removed, %d modified, %d binary, %d unchanged\n", added, removed, modified, binary, res.unchanged);
    for (const std::string& w : res.warnings) std::fprintf(stderr, "redline: %s\n", w.c_str());

    if (!a.html.empty()) {
        const std::string page = renderPage(a.oldPath + " vs " + a.newPath, a.oldPath + "  ->  " + a.newPath, res.files, res.unchanged);
        if (!writeFile(a.html, page)) { std::fprintf(stderr, "redline: cannot write '%s'\n", a.html.c_str()); return 2; }
        std::fprintf(stderr, "wrote %s\n", a.html.c_str());
    }
    if (!res.warnings.empty()) return 2;
    return res.files.empty() ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    Args a;
    if (parseArgs(argc, argv, a) != 0) return usage();

    std::error_code ec;
    const bool oldExists = fs::exists(a.oldPath, ec), newExists = fs::exists(a.newPath, ec);
    if (!oldExists) { std::fprintf(stderr, "redline: '%s' does not exist\n", a.oldPath.c_str()); return 2; }
    if (!newExists) { std::fprintf(stderr, "redline: '%s' does not exist\n", a.newPath.c_str()); return 2; }
    const bool oldDir = fs::is_directory(a.oldPath, ec), newDir = fs::is_directory(a.newPath, ec);
    if (oldDir != newDir) {
        std::fprintf(stderr, "redline: cannot compare a folder with a file\n");
        return 2;
    }
    return oldDir ? diffTwoFolders(a) : diffTwoFiles(a);
}
