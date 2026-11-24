#pragma once
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "redline/models/diff_types.hpp"
#include "redline/services/html_report.hpp"

namespace redline {

struct FolderOptions {
    std::set<std::string> ignore = {".git"};   // names of files or folders to skip, at any depth
    DiffOptions diff;
    int context = 3;
    bool full = false;
    bool wantHtml = true;                      // build the side by side tables (skipped otherwise)
};

struct FolderResult {
    std::vector<ReportFile> files;             // sorted by path
    int unchanged = 0;
    std::vector<std::string> warnings;         // unreadable files and the like
};

// Compares two folder trees. Both file lists are sorted and then walked together with two
// pointers (like the merge step of merge sort), so every path is looked at once: O(n log n) for
// the sorting plus the cost of comparing the files that exist on both sides.
// Files that exist on both sides are compared by size first, then byte by byte, and only
// files that really differ are diffed line by line.
FolderResult compareFolders(const std::filesystem::path& a, const std::filesystem::path& b, const FolderOptions& opt);

}  // namespace redline
