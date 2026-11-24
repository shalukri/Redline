#pragma once
#include <string>
#include <vector>

#include "redline/models/diff_types.hpp"

namespace redline {

// Classic unified diff text ("--- / +++ / @@ -a,b +c,d @@"), the same format `diff -u` and
// `git diff` print, including the "\ No newline at end of file" marker.
std::string formatUnified(const std::string& oldName, const std::string& newName, const TextFile& a,
                          const TextFile& b, const FileDiff& diff, const std::vector<Hunk>& hunks, bool color);

}  // namespace redline
