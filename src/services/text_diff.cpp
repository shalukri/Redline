#include "redline/services/text_diff.hpp"

#include <algorithm>
#include <unordered_map>

#include "redline/algorithms/myers.hpp"
#include "redline/algorithms/text_utils.hpp"

namespace redline {
namespace {

std::vector<int> toIds(const TextFile& f, bool ignoreSpace, std::unordered_map<std::string, int>& table) {
    std::vector<int> ids;
    ids.reserve(f.lines.size());
    for (std::size_t i = 0; i < f.lines.size(); i++) {
        std::string key = ignoreSpace ? normalizeSpace(f.lines[i]) : f.lines[i];
        // A real line can never contain '\n', so this marker cannot clash with file content.
        if (!f.endsWithNewline && i + 1 == f.lines.size()) key += "\nNOEOL";
        auto it = table.emplace(std::move(key), static_cast<int>(table.size())).first;
        ids.push_back(it->second);
    }
    return ids;
}

}  // namespace

FileDiff diffFiles(const TextFile& a, const TextFile& b, const DiffOptions& opt) {
    std::unordered_map<std::string, int> table;
    table.reserve(a.lines.size() + b.lines.size());
    const std::vector<int> ka = toIds(a, opt.ignoreSpace, table);
    const std::vector<int> kb = toIds(b, opt.ignoreSpace, table);
    const std::size_t n = ka.size(), m = kb.size();

    std::size_t prefix = 0;
    while (prefix < n && prefix < m && ka[prefix] == kb[prefix]) prefix++;
    std::size_t suffix = 0;
    while (suffix < n - prefix && suffix < m - prefix && ka[n - 1 - suffix] == kb[m - 1 - suffix]) suffix++;

    FileDiff out;
    out.ops.reserve(n + m - prefix - suffix);
    for (std::size_t i = 0; i < prefix; i++) out.ops.push_back({OpKind::Equal, static_cast<int>(i), static_cast<int>(i)});

    const std::vector<int> midA(ka.begin() + static_cast<std::ptrdiff_t>(prefix), ka.end() - static_cast<std::ptrdiff_t>(suffix));
    const std::vector<int> midB(kb.begin() + static_cast<std::ptrdiff_t>(prefix), kb.end() - static_cast<std::ptrdiff_t>(suffix));
    auto middle = myersDiff(midA, midB, opt.maxEdits);
    const int p = static_cast<int>(prefix);
    if (middle) {
        for (Op op : *middle) {
            if (op.a >= 0) op.a += p;
            if (op.b >= 0) op.b += p;
            out.ops.push_back(op);
        }
    } else {
        // Too different for an exact answer within the limit: show the middle as replaced.
        out.exact = false;
        for (std::size_t i = 0; i < midA.size(); i++) out.ops.push_back({OpKind::Delete, p + static_cast<int>(i), -1});
        for (std::size_t j = 0; j < midB.size(); j++) out.ops.push_back({OpKind::Insert, -1, p + static_cast<int>(j)});
    }

    for (std::size_t i = 0; i < suffix; i++)
        out.ops.push_back({OpKind::Equal, static_cast<int>(n - suffix + i), static_cast<int>(m - suffix + i)});

    for (const Op& op : out.ops) {
        if (op.kind == OpKind::Equal) out.stats.unchanged++;
        else if (op.kind == OpKind::Delete) out.stats.removed++;
        else out.stats.added++;
    }
    return out;
}

std::vector<Hunk> makeHunks(const std::vector<Op>& ops, int context, bool full) {
    std::vector<Hunk> hunks;
    if (ops.empty()) return hunks;
    const std::size_t ctx = static_cast<std::size_t>(std::max(context, 0));

    std::vector<std::pair<std::size_t, std::size_t>> ranges;   // [begin, end) in ops
    if (full) {
        ranges.push_back({0, ops.size()});
    } else {
        std::size_t i = 0;
        while (i < ops.size()) {
            if (ops[i].kind == OpKind::Equal) { i++; continue; }
            std::size_t j = i;
            while (j < ops.size() && ops[j].kind != OpKind::Equal) j++;   // end of this run of changes
            const std::size_t begin = i >= ctx ? i - ctx : 0;
            const std::size_t end = std::min(ops.size(), j + ctx);
            if (!ranges.empty() && begin <= ranges.back().second) ranges.back().second = end;   // merge
            else ranges.push_back({begin, end});
            i = j;
        }
    }

    // running line counts, so we can turn an op position into old/new line numbers
    std::vector<int> oldBefore(ops.size() + 1, 0), newBefore(ops.size() + 1, 0);
    for (std::size_t i = 0; i < ops.size(); i++) {
        oldBefore[i + 1] = oldBefore[i] + (ops[i].kind != OpKind::Insert ? 1 : 0);
        newBefore[i + 1] = newBefore[i] + (ops[i].kind != OpKind::Delete ? 1 : 0);
    }
    for (auto [b, e] : ranges) {
        Hunk h;
        h.opBegin = b;
        h.opEnd = e;
        h.oldCount = oldBefore[e] - oldBefore[b];
        h.newCount = newBefore[e] - newBefore[b];
        // Unified diff rule: when a side has no lines, its start is the line BEFORE the hunk.
        h.oldStart = h.oldCount ? oldBefore[b] + 1 : oldBefore[b];
        h.newStart = h.newCount ? newBefore[b] + 1 : newBefore[b];
        hunks.push_back(h);
    }
    return hunks;
}

}  // namespace redline
