#include <thread>
#include <filesystem>
#include <fstream>

#include "cpu.hpp"

namespace thinger::monitor::cpu {

  unsigned int get_cpu_cores() {
    return std::thread::hardware_concurrency();
  }

  unsigned int get_cpu_procs() {

    std::string path = "/proc";
    unsigned int procs = 0;
    for (const auto & entry : std::filesystem::directory_iterator(path)) {
      if (entry.is_directory() && isdigit(entry.path().u8string().back()))  {
        procs++;
      }
    }

    return procs;
  }

}
