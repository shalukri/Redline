#pragma once
#include <string>
#include <vector>

#include "redline/models/diff_types.hpp"

namespace redline {

// Splits file content into lines. "\r\n" and "\n" both end a line (the \r is dropped), and a
// missing newline at the very end is remembered instead of being lost.
TextFile splitText(const std::string& content);

// Collapses runs of blanks into one space and trims both ends (for --ignore-space).
std::string normalizeSpace(const std::string& line);

// Cuts a line into words, blank runs and single punctuation characters.
// Bytes >= 0x80 count as word characters so a UTF-8 character is never split in the middle.
std::vector<std::string> tokenize(const std::string& line);

// Escapes & < > " ' so file content can never turn into markup.
std::string htmlEscape(const std::string& s);

// Same rule as git: a NUL byte in the first 8000 bytes means "binary".
bool looksBinary(const std::string& bytes);

}  // namespace redline
