#include "redline/services/folder_compare.hpp"

#include <algorithm>
#include <fstream>

#include "redline/algorithms/text_utils.hpp"
#include "redline/services/text_diff.hpp"

namespace fs = std::filesystem;

namespace redline {
namespace {

constexpr std::uintmax_t kMaxTextBytes = 8u * 1024u * 1024u;   // bigger files are only compared, not diffed

bool readAll(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return !f.bad();
}

// Compares in 64 KB blocks so two huge files never have to fit in memory at once.
bool sameBytes(const fs::path& a, const fs::path& b, bool& ok) {
    std::ifstream fa(a, std::ios::binary), fb(b, std::ios::binary);
    ok = static_cast<bool>(fa) && static_cast<bool>(fb);
    if (!ok) return false;
    std::vector<char> ba(1 << 16), bb(1 << 16);
    while (true) {
        fa.read(ba.data(), static_cast<std::streamsize>(ba.size()));
        fb.read(bb.data(), static_cast<std::streamsize>(bb.size()));
        const std::streamsize na = fa.gcount(), nb = fb.gcount();
        if (na != nb || !std::equal(ba.begin(), ba.begin() + na, bb.begin())) return false;
        if (na == 0) return true;
    }
}

bool hasIgnoredPart(const fs::path& rel, const std::set<std::string>& names) {
    for (const fs::path& part : rel)
        if (names.count(part.string())) return true;
    return false;
}

void listFiles(const fs::path& root, const std::set<std::string>& ignore, std::vector<std::string>& out,
               std::vector<std::string>& warnings) {
    std::error_code ec;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    if (ec) {
        warnings.push_back("cannot read folder '" + root.string() + "': " + ec.message());
        return;
    }
    while (it != end) {
        std::error_code e2;
        const fs::path rel = fs::relative(it->path(), root, e2);
        const fs::file_status st = it->symlink_status(e2);   // symlink_status: do not follow links
        if (!e2 && hasIgnoredPart(rel, ignore)) {
            if (fs::is_directory(st)) it.disable_recursion_pending();
        } else if (!e2 && fs::is_regular_file(st)) {
            out.push_back(rel.generic_string());
        }
        it.increment(e2);
        if (e2) {
            warnings.push_back("stopped reading '" + root.string() + "': " + e2.message());
            break;
        }
    }
    std::sort(out.begin(), out.end());
}

std::string linesNote(const TextFile& f) {
    return std::to_string(f.lines.size()) + (f.lines.size() == 1 ? " line" : " lines");
}

}  // namespace

FolderResult compareFolders(const fs::path& a, const fs::path& b, const FolderOptions& opt) {
    FolderResult res;
    std::vector<std::string> la, lb;
    listFiles(a, opt.ignore, la, res.warnings);
    listFiles(b, opt.ignore, lb, res.warnings);

    auto describeSingle = [&](const fs::path& full, const std::string& rel, const char* status) {
        ReportFile f;
        f.path = rel;
        f.status = status;
        std::string bytes;
        std::error_code ec;
        const std::uintmax_t size = fs::file_size(full, ec);
        if (ec || size > kMaxTextBytes) {
            f.status = "binary";   // shown as "binary / too large"
            f.detail = std::string(status) + ", too large to read";
        } else if (!readAll(full, bytes)) {
            res.warnings.push_back("cannot read '" + full.string() + "'");
            f.detail = "unreadable";
        } else if (looksBinary(bytes)) {
            f.detail = std::string("binary file, ") + std::to_string(bytes.size()) + " bytes";
            f.status = status;
        } else {
            TextFile t = splitText(bytes);
            f.detail = linesNote(t);
            (f.status == "added" ? f.added : f.removed) = static_cast<int>(t.lines.size());
        }
        res.files.push_back(std::move(f));
    };

    std::size_t i = 0, j = 0;
    while (i < la.size() || j < lb.size()) {
        const int cmp = i == la.size() ? 1 : j == lb.size() ? -1 : la[i].compare(lb[j]);
        if (cmp < 0) {
            describeSingle(a / la[i], la[i], "removed");
            i++;
        } else if (cmp > 0) {
            describeSingle(b / lb[j], lb[j], "added");
            j++;
        } else {
            const std::string& rel = la[i];
            const fs::path pa = a / rel, pb = b / rel;
            i++;
            j++;
            std::error_code e1, e2;
            const std::uintmax_t sa = fs::file_size(pa, e1), sb = fs::file_size(pb, e2);
            bool readable = true;
            if (!e1 && !e2 && sa == sb && sameBytes(pa, pb, readable)) { res.unchanged++; continue; }
            if (e1 || e2 || !readable) {
                res.warnings.push_back("cannot read '" + rel + "'");
                continue;
            }
            ReportFile f;
            f.path = rel;
            if (sa > kMaxTextBytes || sb > kMaxTextBytes) {
                f.status = "binary";
                f.detail = "too large to diff, contents differ";
                res.files.push_back(std::move(f));
                continue;
            }
            std::string ca, cb;
            if (!readAll(pa, ca) || !readAll(pb, cb)) {
                res.warnings.push_back("cannot read '" + rel + "'");
                continue;
            }
            if (looksBinary(ca) || looksBinary(cb)) {
                f.status = "binary";
                f.detail = "binary, contents differ";
                res.files.push_back(std::move(f));
                continue;
            }
            const TextFile ta = splitText(ca), tb = splitText(cb);
            const FileDiff d = diffFiles(ta, tb, opt.diff);
            if (d.stats.added == 0 && d.stats.removed == 0) { res.unchanged++; continue; }   // e.g. only CRLF or spacing
            f.status = "modified";
            f.added = d.stats.added;
            f.removed = d.stats.removed;
            if (!d.exact) f.detail = "very different, shown as replaced";
            if (opt.wantHtml) f.diffHtml = renderSideBySide(ta, tb, d, makeHunks(d.ops, opt.context, opt.full));
            res.files.push_back(std::move(f));
        }
    }
    return res;
}

}  // namespace redline
