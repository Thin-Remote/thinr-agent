
#ifndef THINGER_MONITOR_CPU_HPP
#define THINGER_MONITOR_CPU_HPP

//#include "monitor.hpp"

namespace thinger::monitor::cpu {

  unsigned int get_cpu_cores();

  unsigned int get_cpu_procs();

  template <size_t N>
  void retrieve_cpu_loads(std::array<float, N>& loads) {
    std::ifstream loadinfo ("/proc/loadavg", std::ifstream::in);
    for (auto i = 0; i < 3; i++) {
      loadinfo >> loads[i];
    }
  }

  template <size_t N>
  float get_cpu_usage(std::array<float, N> const& loads, unsigned int const& cores) {
    return loads[0] * 100 / cores;
  }

}


#endif //THINGER_MONITOR_CPU_HPP
