#include "network.hpp"

namespace thinger::monitor::network {

  std::string getPublicIPAddress() {
    httplib::Client cli("https://ifconfig.me");
    auto res = cli.Get("/ip");
    if ( res.error() == httplib::Error::SSLServerVerification ) {
      httplib::Client cli("http://ifconfig.me");
      res = cli.Get("/ip");
    }
    return res->body;
  }

  std::string getIPAddress(const std::string_view& interface){
    std::string ipAddress="Unable to get IP Address";
    struct ifaddrs *interfaces = nullptr;
    struct ifaddrs *temp_addr = nullptr;
    int success = 0;
    // retrieve the current interfaces - returns 0 on success
    success = getifaddrs(&interfaces);
    if (success == 0) {
      // Loop through linked list of interfaces
      temp_addr = interfaces;
      while(temp_addr != nullptr) {
        if(temp_addr->ifa_addr != nullptr
           && temp_addr->ifa_addr->sa_family == AF_INET
           && temp_addr->ifa_name == interface)
        {
          // Check if interface is the default interface
          ipAddress=inet_ntoa(((struct sockaddr_in*)temp_addr->ifa_addr)->sin_addr);
        }
        temp_addr = temp_addr->ifa_next;
      }
    }
    // Free memory
    freeifaddrs(interfaces);
    return ipAddress;
  }

  void retrieve_ifc_stats(std::vector<interface>& interfaces) {
    // TODO: Do not send values if null, just avoid adding it to the resource
    for (auto & ifc : interfaces) {

      std::ifstream netinfo ("/proc/net/dev", std::ifstream::in);
      std::string line;
      std::string null;

      while(netinfo >> line) {
        if (line == ifc.name+":") {
          netinfo >> ifc.total_transfer[0][1]; //first bytes inc
          netinfo >> ifc.total_packets[0]; // total packets inc
          netinfo >> null >> ifc.total_packets[1]; // drop packets inc
          netinfo >> null >> null >> null >> null >> ifc.total_transfer[1][1]; // total bytes out
          netinfo >> ifc.total_packets[2]; // total packets out
          netinfo >> null >> ifc.total_packets[3]; // drop packets out

          ifc.total_transfer[2][1] = std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::system_clock::now().time_since_epoch()).count();

          break;
        }
      }
    }
  }

}
