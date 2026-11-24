#include "redline/services/inline_diff.hpp"

#include <unordered_map>
#include <vector>

#include "redline/algorithms/myers.hpp"
#include "redline/algorithms/text_utils.hpp"

namespace redline {
namespace {

std::string render(const std::vector<std::string>& tokens, const std::vector<char>& changed) {
    std::string out;
    bool open = false;
    for (std::size_t i = 0; i < tokens.size(); i++) {
        if (changed[i] && !open) { out += "<mark>"; open = true; }
        if (!changed[i] && open) { out += "</mark>"; open = false; }
        out += htmlEscape(tokens[i]);
    }
    if (open) out += "</mark>";
    return out;
}

}  // namespace

HighlightedPair highlightPair(const std::string& oldLine, const std::string& newLine) {
    HighlightedPair r;
    r.left = htmlEscape(oldLine);
    r.right = htmlEscape(newLine);

    const auto ta = tokenize(oldLine), tb = tokenize(newLine);
    std::unordered_map<std::string, int> table;
    auto ids = [&](const std::vector<std::string>& t) {
        std::vector<int> v;
        v.reserve(t.size());
        for (const std::string& s : t) v.push_back(table.emplace(s, static_cast<int>(table.size())).first->second);
        return v;
    };
    auto ops = myersDiff(ids(ta), ids(tb), 2000);
    if (!ops) return r;

    std::vector<char> delA(ta.size(), 0), insB(tb.size(), 0);
    std::size_t equal = 0;
    for (const Op& op : *ops) {
        if (op.kind == OpKind::Equal) equal++;
        else if (op.kind == OpKind::Delete) delA[static_cast<std::size_t>(op.a)] = 1;
        else insB[static_cast<std::size_t>(op.b)] = 1;
    }
    const std::size_t total = ta.size() + tb.size();
    if (total == 0 || 2.0 * static_cast<double>(equal) / static_cast<double>(total) < 0.4) return r;

    r.left = render(ta, delA);
    r.right = render(tb, insB);
    r.highlighted = true;
    return r;
}

}  // namespace redline
