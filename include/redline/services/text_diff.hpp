#pragma once
#include <vector>

#include "redline/models/diff_types.hpp"

namespace redline {

// Line by line diff of two files.
//  1. Every distinct line is turned into a small integer, so later comparisons are int compares.
//  2. The common start and end of the two files are cut off. For files that are mostly the
//     same this leaves a tiny middle part, which is why the diff is fast on real edits.
//  3. Myers' algorithm runs on the middle part.
// A missing newline at the end of a file counts as a change of that last line (like GNU diff).
FileDiff diffFiles(const TextFile& a, const TextFile& b, const DiffOptions& opt);

// Groups changes into hunks with `context` unchanged lines around each change. Hunks that would
// touch or overlap are merged. With full == true the whole file is one hunk.
std::vector<Hunk> makeHunks(const std::vector<Op>& ops, int context, bool full);

}  // namespace redline
