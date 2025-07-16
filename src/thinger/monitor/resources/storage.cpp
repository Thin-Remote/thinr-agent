#include <vector>
#include <filesystem>

#include "storage.hpp"

namespace thinger::monitor::storage {

  void retrieve_fs_stats(std::vector<filesystem>& filesystems) {
    for (auto & fs : filesystems) {
      fs.space_info = std::filesystem::space(fs.path);
    }
  }

}
