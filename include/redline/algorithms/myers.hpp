#pragma once
#include <cstddef>
#include <optional>
#include <vector>

#include "redline/models/diff_types.hpp"

namespace redline {

// Shortest edit script between two integer sequences (Eugene Myers, 1986).
// Time is O(N * D) where D is the number of inserted plus deleted items, so two files that are
// nearly the same are fast no matter how long they are.
//
// Returns nullopt if more than maxEdits changes would be needed. The trace that is needed to
// walk the path back takes about 4 * D * D bytes, so maxEdits also caps memory (4000 is ~64 MB).
//
// In every run of changes the deletes come before the inserts, which is what people expect to read.
std::optional<std::vector<Op>> myersDiff(const std::vector<int>& a, const std::vector<int>& b,
                                         std::size_t maxEdits);

}  // namespace redline
