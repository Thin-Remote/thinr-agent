
#ifndef THINGER_MONITOR_NETWORK_HPP
#define THINGER_MONITOR_NETWORK_HPP

#include <httplib.h>

namespace thinger::monitor::network {

  struct interface {
    std::string name;
    std::string internal_ip;
    std::array<std::array<unsigned long long int, 2>, 3> total_transfer; // before, after; b incoming, b outgoing, 3-> ts
    std::array<unsigned long long, 4> total_packets; // incoming (total, dropped), outgoing (total, dropped)
  };

  std::string getPublicIPAddress();

  std::string getIPAddress(const std::string_view& interface);

  void retrieve_ifc_stats(std::vector<interface>& interfaces);

}

#endif //THINGER_MONITOR_NETWORK_HPP
