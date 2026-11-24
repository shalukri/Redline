#include "redline/algorithms/myers.hpp"

#include <algorithm>

namespace redline {
namespace {

// Inside a run of non-Equal ops, put all deletes first. Both orders describe the same change,
// but "all removed lines, then all added lines" is how diffs are normally printed.
void deletesFirst(std::vector<Op>& ops) {
    std::size_t i = 0;
    while (i < ops.size()) {
        if (ops[i].kind == OpKind::Equal) { i++; continue; }
        std::size_t j = i;
        while (j < ops.size() && ops[j].kind != OpKind::Equal) j++;
        std::stable_partition(ops.begin() + static_cast<std::ptrdiff_t>(i), ops.begin() + static_cast<std::ptrdiff_t>(j),
                              [](const Op& o) { return o.kind == OpKind::Delete; });
        i = j;
    }
}

}  // namespace

std::optional<std::vector<Op>> myersDiff(const std::vector<int>& a, const std::vector<int>& b,
                                         std::size_t maxEdits) {
    const int n = static_cast<int>(a.size());
    const int m = static_cast<int>(b.size());
    std::vector<Op> ops;

    // nothing to search for when one side is empty
    if (n == 0 || m == 0) {
        for (int i = 0; i < n; i++) ops.push_back({OpKind::Delete, i, -1});
        for (int j = 0; j < m; j++) ops.push_back({OpKind::Insert, -1, j});
        return ops;
    }

    const int maxD = static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(n) + static_cast<std::size_t>(m), maxEdits));
    const int offset = maxD + 1;
    // v[offset + k] is the furthest x we can reach on diagonal k (k = x - y) with the current
    // number of edits d. Moving right on the grid deletes a line of `a`, moving down inserts a
    // line of `b`, and a diagonal step is a matching line and costs nothing.
    std::vector<int> v(static_cast<std::size_t>(2 * maxD + 3), 0);
    std::vector<std::vector<int>> trace;   // trace[d] = copy of v[-d..d] after round d
    trace.reserve(64);

    for (int d = 0; d <= maxD; d++) {
        for (int k = -d; k <= d; k += 2) {
            int x;
            if (k == -d || (k != d && v[offset + k - 1] < v[offset + k + 1])) x = v[offset + k + 1];   // down
            else x = v[offset + k - 1] + 1;                                                          // right
            int y = x - k;
            while (x < n && y < m && a[x] == b[y]) { x++; y++; }
            v[offset + k] = x;

            if (x >= n && y >= m) {
                // reached the bottom right corner with d edits. Walk back to the start.
                std::vector<Op> rev;
                int cx = n, cy = m;
                for (int dd = d; dd > 0; dd--) {
                    const std::vector<int>& pv = trace[static_cast<std::size_t>(dd - 1)];
                    auto at = [&](int kk) { return pv[static_cast<std::size_t>(kk + (dd - 1))]; };
                    const int ck = cx - cy;
                    const bool down = (ck == -dd) || (ck != dd && at(ck - 1) < at(ck + 1));
                    const int pk = down ? ck + 1 : ck - 1;
                    const int px = at(pk);
                    const int py = px - pk;
                    const int mx = down ? px : px + 1;   // position right after the single edit
                    const int my = down ? py + 1 : py;
                    while (cx > mx && cy > my) {
                        rev.push_back({OpKind::Equal, cx - 1, cy - 1});
                        cx--;
                        cy--;
                    }
                    if (down) rev.push_back({OpKind::Insert, -1, py});
                    else rev.push_back({OpKind::Delete, px, -1});
                    cx = px;
                    cy = py;
                }
                while (cx > 0 && cy > 0) {   // leading matching lines (d == 0 part)
                    rev.push_back({OpKind::Equal, cx - 1, cy - 1});
                    cx--;
                    cy--;
                }
                ops.assign(rev.rbegin(), rev.rend());
                deletesFirst(ops);
                return ops;
            }
        }
        trace.emplace_back(v.begin() + (offset - d), v.begin() + (offset + d + 1));
    }
    return std::nullopt;
}

}  // namespace redline
