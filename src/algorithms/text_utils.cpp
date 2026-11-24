#include "redline/algorithms/text_utils.hpp"

#include <algorithm>
#include <cctype>

namespace redline {

TextFile splitText(const std::string& content) {
    TextFile f;
    std::size_t pos = 0;
    while (pos < content.size()) {
        std::size_t nl = content.find('\n', pos);
        const bool last = (nl == std::string::npos);
        const std::size_t end = last ? content.size() : nl;
        std::string line = content.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
            f.hadCrlf = true;
        }
        f.lines.push_back(std::move(line));
        if (last) {
            f.endsWithNewline = false;
            break;
        }
        pos = nl + 1;
    }
    return f;
}

std::string normalizeSpace(const std::string& line) {
    std::string out;
    bool pendingSpace = false;
    for (unsigned char c : line) {
        if (c == ' ' || c == '\t' || c == '\v' || c == '\f' || c == '\r') {
            pendingSpace = !out.empty();
        } else {
            if (pendingSpace) out.push_back(' ');
            pendingSpace = false;
            out.push_back(static_cast<char>(c));
        }
    }
    return out;
}

std::vector<std::string> tokenize(const std::string& s) {
    auto cls = [](unsigned char c) {
        if (std::isalnum(c) || c == '_' || c >= 0x80) return 1;   // word
        if (c == ' ' || c == '\t') return 2;                      // blanks
        return 0;                                                 // one punctuation character
    };
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < s.size()) {
        const int c = cls(static_cast<unsigned char>(s[i]));
        std::size_t j = i + 1;
        if (c != 0)
            while (j < s.size() && cls(static_cast<unsigned char>(s[j])) == c) j++;
        out.push_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}

std::string htmlEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + s.size() / 8);
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out.push_back(c);
        }
    }
    return out;
}

bool looksBinary(const std::string& bytes) {
    const std::size_t limit = std::min<std::size_t>(bytes.size(), 8000);
    return std::find(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(limit), '\0') != bytes.begin() + static_cast<std::ptrdiff_t>(limit);
}

}  // namespace redline
