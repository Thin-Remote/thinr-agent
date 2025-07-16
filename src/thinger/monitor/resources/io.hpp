
#ifndef THINGER_MONITOR_IO_HPP
#define THINGER_MONITOR_IO_HPP

#include <array>
#include <vector>
#include <string>

namespace thinger::monitor::io {

  struct drive {
    std::string name;
    std::array<std::array<unsigned long long int, 4>, 2> total_io; // before, after; sectors read, sectors writte, io tics, ts
  };

  void retrieve_dv_stats(std::vector<drive>& drives);

}


#endif //THINGER_MONITOR_IO_HPP
