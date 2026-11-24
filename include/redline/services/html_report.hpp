#pragma once
#include <string>
#include <vector>

#include "redline/models/diff_types.hpp"

namespace redline {

// One row of the summary table, and (when there is a diff) one collapsible section below it.
struct ReportFile {
    std::string path;
    std::string status;      // "added", "removed", "modified" or "binary"
    int added = 0;
    int removed = 0;
    std::string detail;      // short note shown in the summary, for example "12 lines"
    std::string diffHtml;    // output of renderSideBySide, empty when there is nothing to show
};

// <table> with the old file on the left and the new one on the right. Changed lines that replace
// each other are paired up and the changed words inside them are marked.
std::string renderSideBySide(const TextFile& a, const TextFile& b, const FileDiff& diff,
                             const std::vector<Hunk>& hunks);

// Complete standalone page: inline CSS, no JavaScript, no external files. Works offline and
// follows the system light/dark setting.
std::string renderPage(const std::string& title, const std::string& subtitle,
                       const std::vector<ReportFile>& files, int unchangedFiles);

}  // namespace redline
