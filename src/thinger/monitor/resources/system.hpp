
#ifndef THINGER_MONITOR_SYSTEM_HPP
#define THINGER_MONITOR_SYSTEM_HPP

namespace thinger::monitor::system {

  // Returns the hostname of the system
  std::string get_hostname();

  // Returns the OS version of the system, more specifically the pretty name of /proc/os-release
  std::string get_os_version();

  // Returns the kernel version of the system
  std::string get_kernel_version();

  std::string get_uptime();

  void retrieve_updates(unsigned int& normal_updates, unsigned int& security_updates);

  bool get_restart_status();

}

#endif //THINGER_MONITOR_SYSTEM_HPP