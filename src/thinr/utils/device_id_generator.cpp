#include "device_id_generator.hpp"
#include <unistd.h>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>
#include <spdlog/spdlog.h>

#ifdef __linux__
#include <ifaddrs.h>
#include <netpacket/packet.h>
#include <net/if.h>
#elif __APPLE__
#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <sys/socket.h>
#endif

namespace thinr::utils {

std::string DeviceIdGenerator::generate() {
    std::string hostname_prefix = get_hostname_prefix();
    std::string mac = get_first_hardware_mac();
    
    if (hostname_prefix.empty() && mac.empty()) {
        // Fallback to timestamp if both fail
        return "thinr_" + std::to_string(std::time(nullptr));
    }
    
    if (hostname_prefix.empty()) {
        hostname_prefix = "device";
    }
    
    if (mac.empty()) {
        // Use timestamp as fallback for MAC
        mac = std::to_string(std::time(nullptr));
    } else {
        // Format MAC to compact form (remove colons)
        mac = format_mac_compact(mac);
    }
    
    std::string device_id = hostname_prefix + "_" + mac;
    
    // Ensure the ID doesn't exceed 32 characters
    if (device_id.length() > 32) {
        // Keep as much of the hostname as possible, but ensure we have at least 6 chars from MAC
        size_t mac_chars = std::min(mac.length(), size_t(6));
        size_t hostname_chars = 32 - mac_chars - 1; // -1 for underscore
        device_id = hostname_prefix.substr(0, hostname_chars) + "_" + mac.substr(mac.length() - mac_chars);
    }
    
    // Final validation: ensure only alphanumeric, underscore, and hyphen
    for (char& c : device_id) {
        if (!std::isalnum(c) && c != '_' && c != '-') {
            c = '_'; // Replace invalid characters with underscore
        }
    }
    
    return device_id;
}

std::string DeviceIdGenerator::get_hostname_prefix() {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        return "";
    }
    
    std::string host(hostname);
    
    // Find first separator (-, _, ., space, etc.)
    size_t pos = host.find_first_of("-_. \t");
    if (pos != std::string::npos) {
        host = host.substr(0, pos);
    }
    
    // Convert to lowercase for consistency
    std::transform(host.begin(), host.end(), host.begin(), ::tolower);
    
    // Ensure it's not empty and reasonable length
    if (host.empty() || host.length() > 32) {
        return "device";
    }
    
    return host;
}

bool DeviceIdGenerator::is_virtual_interface(const std::string& name) {
    const std::vector<std::string> virtual_prefixes = {
        "lo", "vir", "docker", "veth", "br-", "tun", "tap", "vmnet", "utun",
        "awdl", "llw", "anpi", "bridge", "ap", "gif", "stf", "XHC"
    };
    
    for (const auto& prefix : virtual_prefixes) {
        if (name.rfind(prefix, 0) == 0) {
            return true;
        }
    }
    
    // On macOS, en0 is typically the primary WiFi interface
    #ifdef __APPLE__
    if (name == "en0") {
        return false; // This is the primary hardware interface
    }
    #endif
    
    return false;
}

std::string DeviceIdGenerator::format_mac_compact(const std::string& mac) {
    std::string compact;
    for (char c : mac) {
        if (c != ':' && c != '-') {
            compact += static_cast<char>(std::tolower(c));
        }
    }
    return compact;
}

#ifdef __linux__
std::string DeviceIdGenerator::get_first_hardware_mac() {
    struct ifaddrs *ifaddr, *ifa;
    char mac[18] = {0};
    
    if (getifaddrs(&ifaddr) == -1) {
        spdlog::debug("Failed to get network interfaces");
        return "";
    }
    
    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_PACKET) {
            continue;
        }
        
        std::string ifname(ifa->ifa_name);
        if (is_virtual_interface(ifname)) {
            continue;
        }
        
        struct sockaddr_ll *s = (struct sockaddr_ll*)ifa->ifa_addr;
        
        // Check for valid 6-byte MAC address
        if (s->sll_halen == 6 &&
            !(s->sll_addr[0] == 0 && s->sll_addr[1] == 0 && s->sll_addr[2] == 0 &&
              s->sll_addr[3] == 0 && s->sll_addr[4] == 0 && s->sll_addr[5] == 0)) {
            
            snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x",
                     s->sll_addr[0], s->sll_addr[1], s->sll_addr[2],
                     s->sll_addr[3], s->sll_addr[4], s->sll_addr[5]);
            break;
        }
    }
    
    freeifaddrs(ifaddr);
    return std::string(mac);
}

#elif __APPLE__
std::string DeviceIdGenerator::get_first_hardware_mac() {
    struct ifaddrs *ifaddr, *ifa;
    char mac[18] = {0};
    
    if (getifaddrs(&ifaddr) == -1) {
        spdlog::debug("Failed to get network interfaces");
        return "";
    }
    
    // Priority list of interface names on macOS
    // en0 is typically WiFi, others could be Ethernet adapters
    const std::vector<std::string> priority_interfaces = {"en0"};
    
    // First pass: try priority interfaces
    for (const auto& priority_if : priority_interfaces) {
        for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_LINK) {
                continue;
            }
            
            std::string ifname(ifa->ifa_name);
            if (ifname != priority_if) {
                continue;
            }
            
            struct sockaddr_dl *sdl = (struct sockaddr_dl *)ifa->ifa_addr;
            if (sdl->sdl_alen == 6) {
                unsigned char *mac_addr = (unsigned char *)LLADDR(sdl);
                
                // Check it's not all zeros
                if (!(mac_addr[0] == 0 && mac_addr[1] == 0 && mac_addr[2] == 0 &&
                      mac_addr[3] == 0 && mac_addr[4] == 0 && mac_addr[5] == 0)) {
                    
                    snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x",
                             mac_addr[0], mac_addr[1], mac_addr[2],
                             mac_addr[3], mac_addr[4], mac_addr[5]);
                    freeifaddrs(ifaddr);
                    return std::string(mac);
                }
            }
        }
    }
    
    // Second pass: any non-virtual interface
    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_LINK) {
            continue;
        }
        
        std::string ifname(ifa->ifa_name);
        if (is_virtual_interface(ifname)) {
            continue;
        }
        
        struct sockaddr_dl *sdl = (struct sockaddr_dl *)ifa->ifa_addr;
        if (sdl->sdl_alen == 6) {
            unsigned char *mac_addr = (unsigned char *)LLADDR(sdl);
            
            // Check it's not all zeros
            if (!(mac_addr[0] == 0 && mac_addr[1] == 0 && mac_addr[2] == 0 &&
                  mac_addr[3] == 0 && mac_addr[4] == 0 && mac_addr[5] == 0)) {
                
                snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x",
                         mac_addr[0], mac_addr[1], mac_addr[2],
                         mac_addr[3], mac_addr[4], mac_addr[5]);
                break;
            }
        }
    }
    
    freeifaddrs(ifaddr);
    return std::string(mac);
}
#else
std::string DeviceIdGenerator::get_first_hardware_mac() {
    // Unsupported platform
    return "";
}
#endif

} // namespace thinr::utils