
#ifndef THINGER_MONITOR_STORAGE_HPP
#define THINGER_MONITOR_STORAGE_HPP

namespace thinger::monitor::storage {

  struct filesystem {
    std::string path;
    std::filesystem::space_info space_info;
  };

  void retrieve_fs_stats(std::vector<filesystem>& filesystems);

}


#endif //THINGER_MONITOR_STORAGE_HPP
