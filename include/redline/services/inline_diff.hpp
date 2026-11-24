#pragma once
#include <string>

namespace redline {

struct HighlightedPair {
    std::string left;    // HTML, already escaped
    std::string right;
    bool highlighted = false;   // false when the lines are too different to be worth marking
};

// Word level diff of two lines that replaced each other. Changed words are wrapped in <mark>.
// If less than 40% of the tokens match the lines are treated as unrelated and nothing is marked,
// because highlighting almost everything is just noise.
HighlightedPair highlightPair(const std::string& oldLine, const std::string& newLine);

}  // namespace redline
