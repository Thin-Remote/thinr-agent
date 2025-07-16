#include "system_info.hpp"
#include <fstream>
#include <sstream>
#include <sys/utsname.h>
#include <spdlog/spdlog.h>

namespace thinr::utils {

std::string SystemInfo::get_os_description() {
    struct utsname system_info;
    if (uname(&system_info) == 0) {
        std::string sysname = system_info.sysname;
        std::string arch = system_info.machine;
        std::string distro;
        
        // Get distribution info
        if (sysname == "Darwin") {
            distro = get_macos_description();
        } else if (sysname == "Linux") {
            distro = get_linux_description();
        } else {
            distro = sysname;
        }
        
        // If distribution is empty or just kernel version, omit the distribution part
        if (distro.empty()) {
            return sysname + " (" + arch + ")";
        }
        
        // If distro is just kernel version (e.g., "2.6.28.2"), show it after Linux
        if (!distro.empty() && distro[0] >= '0' && distro[0] <= '9') {
            return sysname + " " + distro + " (" + arch + ")";
        }
        
        // Check if distro name already contains the system name to avoid duplication
        if (distro.find(sysname) == 0) {
            // Distro already contains system name (e.g., "Linux 2.6.28.2")
            return distro + " (" + arch + ")";
        }
        
        // Format: "System Distribution (arch)"
        return sysname + " " + distro + " (" + arch + ")";
    }
    
    // Fallback
    return "Unknown OS";
}

std::string SystemInfo::get_linux_description() {
    // Try /etc/os-release first (most common)
    std::ifstream os_release("/etc/os-release");
    if (os_release.is_open()) {
        std::string line;
        while (std::getline(os_release, line)) {
            if (line.find("PRETTY_NAME=") == 0) {
                std::string distro = line.substr(12);
                // Remove quotes
                if (!distro.empty() && distro.front() == '"' && distro.back() == '"') {
                    distro = distro.substr(1, distro.length() - 2);
                }
                if (!distro.empty()) {
                    return distro;
                }
            }
        }
    }
    
    // Fallback to uname - just return kernel version without "Linux" prefix
    struct utsname system_info;
    if (uname(&system_info) == 0) {
        return system_info.release;
    }
    
    // If all else fails, return empty string (parent will add "Linux")
    return "";
}

std::string SystemInfo::get_macos_description() {
    std::string version;
    std::string build;
    
    // Try to get macOS version
    std::ifstream version_file("/System/Library/CoreServices/SystemVersion.plist");
    if (version_file.is_open()) {
        std::string line;
        bool found_version = false;
        bool found_build = false;
        
        while (std::getline(version_file, line)) {
            if (line.find("ProductUserVisibleVersion") != std::string::npos) {
                found_version = true;
                continue;
            }
            if (line.find("ProductBuildVersion") != std::string::npos) {
                found_build = true;
                continue;
            }
            
            if (found_version && line.find("<string>") != std::string::npos) {
                size_t start = line.find("<string>") + 8;
                size_t end = line.find("</string>");
                if (end != std::string::npos) {
                    version = line.substr(start, end - start);
                    found_version = false;
                }
            }
            
            if (found_build && line.find("<string>") != std::string::npos) {
                size_t start = line.find("<string>") + 8;
                size_t end = line.find("</string>");
                if (end != std::string::npos) {
                    build = line.substr(start, end - start);
                    found_build = false;
                }
            }
        }
    }
    
    if (!version.empty()) {
        std::string result = "macOS " + version;
        std::string version_name = get_macos_version_name(version);
        if (!version_name.empty()) {
            result += " " + version_name;
        }
        return result;
    }
    
    return "macOS";
}

std::string SystemInfo::get_macos_version_name(const std::string& version) {
    // Extract major version number
    size_t dot_pos = version.find('.');
    if (dot_pos == std::string::npos) return "";
    
    int major_version = 0;
    try {
        major_version = std::stoi(version.substr(0, dot_pos));
    } catch (...) {
        return "";
    }
    
    // macOS version names
    switch (major_version) {
        case 15: return "Sequoia";
        case 14: return "Sonoma";
        case 13: return "Ventura";
        case 12: return "Monterey";
        case 11: return "Big Sur";
        case 10: {
            // For macOS 10.x, need to check minor version
            size_t second_dot = version.find('.', dot_pos + 1);
            if (second_dot != std::string::npos) {
                try {
                    int minor_version = std::stoi(version.substr(dot_pos + 1, second_dot - dot_pos - 1));
                    switch (minor_version) {
                        case 15: return "Catalina";
                        case 14: return "Mojave";
                        case 13: return "High Sierra";
                        case 12: return "Sierra";
                        case 11: return "El Capitan";
                        case 10: return "Yosemite";
                        case 9: return "Mavericks";
                    }
                } catch (...) {}
            }
            break;
        }
    }
    
    return "";
}

} // namespace thinr::utils