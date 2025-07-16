#include <string>
#include <fstream>
#include <filesystem>

#include "system.hpp"

namespace thinger::monitor::system {

  std::string get_hostname() {
    std::string hostname;
    std::ifstream hostinfo ("/etc/hostname", std::ifstream::in);
    hostinfo >> hostname;
    return hostname;
  }

  std::string get_os_version() {
    std::string os_version;
    std::ifstream osinfo ("/etc/os-release", std::ifstream::in);
    std::string line;

    while(std::getline(osinfo,line)) {
      if (line.find("PRETTY_NAME") != std::string::npos) {
        // get text in between "
        size_t first_del = line.find('"');
        size_t last_del = line.find_last_of('"');
        os_version = line.substr(first_del +1, last_del - first_del -1);
      }
    }

    return os_version;
  }

  std::string get_kernel_version() {
    std::string kernel_version;
    std::ifstream kernelinfo ("/proc/version", std::ifstream::in);
    std::string version;

    kernelinfo >> kernel_version;
    kernelinfo >> version; // skipping second word
    kernelinfo >> version;

    kernel_version = kernel_version + " " + version;
    return kernel_version;
  }

  std::string get_uptime() {
    std::string uptime;

    if (double uptime_seconds; std::ifstream("/proc/uptime", std::ios::in) >> uptime_seconds) {
      int days = (int)uptime_seconds / (60*60*24);
      int hours = ((int)uptime_seconds % (((days > 0) ? days : 1)*60*60*24)) / (60*60);
      int minutes = (int)uptime_seconds % (((days > 0) ? days : 1)*60*60*24) % (((hours > 0) ? hours: 1)*60*60) / 60;

      uptime = "";

      // Days
      if (days > 0) {
        uptime += std::to_string(days);
        uptime += (days == 1) ? " day, " : " days, ";
      }

      // Hours
      if (hours > 0) {
        uptime += std::to_string(hours);
        uptime += (hours == 1) ? " hour, " : " hours, ";
      }

      // Minutes
      uptime += std::to_string(minutes);
      uptime += (minutes == 1) ? " minute" : " minutes";
    }

    return uptime;
  }

  // Gets the number of updates pending to be installed (ubuntu specific)
  void retrieve_updates(unsigned int& normal_updates, unsigned int& security_updates) {
    // We will use default ubuntu server notifications
    std::filesystem::path f("/var/lib/update-notifier/updates-available");
    if (std::filesystem::exists(f)) {
      std::ifstream updatesinfo ("/var/lib/update-notifier/updates-available", std::ifstream::in);
      std::string line;
      updatesinfo >> normal_updates;
      if(getline(updatesinfo, line)) {
        updatesinfo >> security_updates;
      } else {
        security_updates = 0;
      }
    }
  }

  // Checks if the current system requires a reboot (ubuntu specific)
  bool get_restart_status() {
    std::filesystem::path f("/var/run/reboot-required");
    if (std::filesystem::exists(f)) {
      return true;
    } else {
      return false;
    }
  }

}
