#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace redline {

enum class OpKind : char { Equal, Delete, Insert };

// One step of an edit script. Equal has both indexes, Delete only `a`, Insert only `b`
// (the unused one is -1). Indexes are 0-based line numbers.
struct Op {
    OpKind kind;
    int a;
    int b;
};

struct TextFile {
    std::vector<std::string> lines;   // line text without the line ending
    bool endsWithNewline = true;      // false when the last line has no \n
    bool hadCrlf = false;             // true if some line ended with \r\n
};

struct DiffOptions {
    bool ignoreSpace = false;
    std::size_t maxEdits = 4000;      // give up on a minimal diff after this many changed lines
};

struct DiffStats {
    int added = 0;
    int removed = 0;
    int unchanged = 0;
};

struct FileDiff {
    std::vector<Op> ops;
    DiffStats stats;
    bool exact = true;   // false if the files were too different and we fell back to "replace all"
};

// A group of changes plus the unchanged lines around them.
struct Hunk {
    std::size_t opBegin;    // range inside FileDiff::ops
    std::size_t opEnd;
    int oldStart, oldCount;
    int newStart, newCount;
};

}  // namespace redline
