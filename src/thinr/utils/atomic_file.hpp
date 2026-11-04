#pragma once

#include <string>
#include <string_view>

namespace thinr::utils {

// Writes `content` to `path` atomically: it writes to a temporary file in the
// same directory (created with `mode`), fsyncs it, renames it over `path` and
// fsyncs the directory so the replacement survives a power cut. The target is
// never left truncated: either the old content stays or the new one is fully
// in place. Returns true on success; on failure `path` is untouched and the
// temporary file is removed. `mode` is the POSIX permission applied at
// creation (subject to umask), not a widening chmod afterwards.
bool write_atomic(const std::string& path, std::string_view content, int mode = 0644);

} // namespace thinr::utils
