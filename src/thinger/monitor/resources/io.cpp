
#include <chrono>
#include <fstream>

#include "io.hpp"

namespace thinger::monitor::io {

  void retrieve_dv_stats(std::vector<drive>& drives) {
    for(auto & dv : drives) {

      std::ifstream dvinfo ("/sys/block/"+dv.name+"/stat", std::ifstream::in);
      std::string null;

      dvinfo >> null >> null >> dv.total_io[1][0]; // sectors read [0]
      dvinfo >> null >> null >> null >> dv.total_io[1][1]; // sectors written [1]
      dvinfo >> null >> null >> dv.total_io[1][2]; // io ticks -> time spent in io [2]

      dv.total_io[1][3] = std::chrono::duration_cast<std::chrono::milliseconds>( // millis [3]
          std::chrono::system_clock::now().time_since_epoch()).count();
    }
  }

}
