#include "memory.hpp"

#include <fstream>

namespace thinger::monitor::memory {

  void get_ram(std::array<unsigned long, 4>& ram) {

    std::ifstream raminfo ("/proc/meminfo", std::ifstream::in);
    std::string line;

    while(raminfo >> line) {
      if (line == "MemTotal:") {
        raminfo >> ram[0];
      } else if (line == "MemAvailable:") {
        raminfo >> ram[1];
      } else if (line == "SwapTotal:") {
        raminfo >> ram[2];
      } else if (line == "SwapFree:") {
        raminfo >> ram[3];
        // From /proc/meminfo order once we reach available we may stop reading
        break;
      }
    }
  }
}
