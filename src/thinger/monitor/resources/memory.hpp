//
// Created by jaime on 10/30/23.
//

#ifndef THINGER_MONITOR_MEMORY_HPP
#define THINGER_MONITOR_MEMORY_HPP

#include <array>

namespace thinger::monitor::memory {

  void get_ram(std::array<unsigned long, 4>& ram);

}

#endif //THINGER_MONITOR_MEMORY_HPP