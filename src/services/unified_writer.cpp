#include "redline/services/unified_writer.hpp"

namespace redline {

std::string formatUnified(const std::string& oldName, const std::string& newName, const TextFile& a,
                          const TextFile& b, const FileDiff& diff, const std::vector<Hunk>& hunks, bool color) {
    if (hunks.empty()) return "";
    const char* red = color ? "\033[31m" : "";
    const char* green = color ? "\033[32m" : "";
    const char* cyan = color ? "\033[36m" : "";
    const char* bold = color ? "\033[1m" : "";
    const char* off = color ? "\033[0m" : "";

    std::string out;
    out += std::string(bold) + "--- " + oldName + "\n+++ " + newName + off + "\n";
    for (const Hunk& h : hunks) {
        out += std::string(cyan) + "@@ -" + std::to_string(h.oldStart) + "," + std::to_string(h.oldCount) + " +" +
               std::to_string(h.newStart) + "," + std::to_string(h.newCount) + " @@" + off + "\n";
        for (std::size_t i = h.opBegin; i < h.opEnd; i++) {
            const Op& op = diff.ops[i];
            bool missingNewline = false;
            switch (op.kind) {
                case OpKind::Equal:
                    out += " " + a.lines[static_cast<std::size_t>(op.a)] + "\n";
                    missingNewline = !a.endsWithNewline && static_cast<std::size_t>(op.a) + 1 == a.lines.size();
                    break;
                case OpKind::Delete:
                    out += std::string(red) + "-" + a.lines[static_cast<std::size_t>(op.a)] + off + "\n";
                    missingNewline = !a.endsWithNewline && static_cast<std::size_t>(op.a) + 1 == a.lines.size();
                    break;
                case OpKind::Insert:
                    out += std::string(green) + "+" + b.lines[static_cast<std::size_t>(op.b)] + off + "\n";
                    missingNewline = !b.endsWithNewline && static_cast<std::size_t>(op.b) + 1 == b.lines.size();
                    break;
            }
            if (missingNewline) out += "\\ No newline at end of file\n";
        }
    }
    return out;
}

}  // namespace redline
