#pragma once

#ifndef THINR_DEVICE_ID_GENERATOR_HPP
#define THINR_DEVICE_ID_GENERATOR_HPP

#include <string>

namespace thinr::utils {

class DeviceIdGenerator {
public:
    // Generate a unique device ID based on hostname prefix and MAC address
    static std::string generate();
    
private:
    // Get the first word from hostname (before any separator)
    static std::string get_hostname_prefix();
    
    // Get the MAC address of the first physical network interface
    static std::string get_first_hardware_mac();
    
    // Check if an interface name is virtual (docker, lo, etc.)
    static bool is_virtual_interface(const std::string& name);
    
    // Format MAC address to a compact string (no colons)
    static std::string format_mac_compact(const std::string& mac);
};

} // namespace thinr::utils

#endif // THINR_DEVICE_ID_GENERATOR_HPP