#include "monitoring.hpp"
#include <spdlog/spdlog.h>
#include <filesystem>
#include <cstdlib>
#include <thread>

#ifdef __linux__
#include <fstream>
#include <sstream>
#endif

#ifdef __APPLE__
#include <mach/mach.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <net/if_dl.h>
#include <libproc.h>
#endif

namespace thinr::extensions {

// -- cpu_sample helpers ------------------------------------------------------

uint64_t monitoring::cpu_sample::total() const {
    return user + nice + system + idle + iowait + irq + softirq + steal;
}

uint64_t monitoring::cpu_sample::active() const {
    return total() - idle - iowait;
}

// -- constructor -------------------------------------------------------------

monitoring::monitoring(thinger::iotmp::client& client) {
    prev_cpu_ = read_cpu_sample();
    prev_net_ = read_network_sample();

    client["monitoring"] = [this](thinger::iotmp::output& out) {
        collect(out);
    };

    spdlog::info("Monitoring extension initialized");
}

// -- collect -----------------------------------------------------------------

void monitoring::collect(thinger::iotmp::output& out) {
    auto& json = out.payload();
    auto& cpu = json["cpu"];
    cpu["usage"] = collect_cpu();
    cpu["cores"] = std::thread::hardware_concurrency();
#ifdef __linux__
    auto temp = read_cpu_temperature();
    if (temp >= 0) cpu["temperature"] = temp;
#endif
    collect_memory(json["memory"]);
    collect_disk(json["disk"]);
    collect_network(json["network"]);
    collect_load(json["load"]);
    out["uptime"]              = collect_uptime();
    json["processes"]["total"] = collect_processes();
}

// -- CPU ---------------------------------------------------------------------

#ifdef __linux__

monitoring::cpu_sample monitoring::read_cpu_sample() {
    cpu_sample s;
    std::ifstream stat("/proc/stat");
    if (!stat) return s;

    std::string label;
    stat >> label
         >> s.user >> s.nice >> s.system >> s.idle
         >> s.iowait >> s.irq >> s.softirq >> s.steal;
    return s;
}

#elif defined(__APPLE__)

monitoring::cpu_sample monitoring::read_cpu_sample() {
    cpu_sample s;

    natural_t cpu_count = 0;
    processor_info_array_t info_array = nullptr;
    mach_msg_type_number_t info_count = 0;

    kern_return_t kr = host_processor_info(
        mach_host_self(),
        PROCESSOR_CPU_LOAD_INFO,
        &cpu_count,
        &info_array,
        &info_count);

    if (kr != KERN_SUCCESS) return s;

    for (natural_t i = 0; i < cpu_count; ++i) {
        auto* ticks = reinterpret_cast<processor_cpu_load_info_data_t*>(info_array) + i;
        s.user   += ticks->cpu_ticks[CPU_STATE_USER];
        s.nice   += ticks->cpu_ticks[CPU_STATE_NICE];
        s.system += ticks->cpu_ticks[CPU_STATE_SYSTEM];
        s.idle   += ticks->cpu_ticks[CPU_STATE_IDLE];
    }

    vm_deallocate(mach_task_self(),
                  reinterpret_cast<vm_address_t>(info_array),
                  info_count * sizeof(integer_t));
    return s;
}

#endif

double monitoring::collect_cpu() {
    auto cur = read_cpu_sample();
    uint64_t total_delta  = cur.total()  - prev_cpu_.total();
    uint64_t active_delta = cur.active() - prev_cpu_.active();
    prev_cpu_ = cur;

    if (total_delta == 0) return 0.0;
    return static_cast<double>(active_delta) / static_cast<double>(total_delta) * 100.0;
}

// -- CPU Temperature (Linux only) --------------------------------------------

#ifdef __linux__

double monitoring::read_cpu_temperature() {
    // Try thermal_zone0 first (most common for CPU)
    std::ifstream temp_file("/sys/class/thermal/thermal_zone0/temp");
    if (!temp_file) return -1;
    long millideg = 0;
    temp_file >> millideg;
    return static_cast<double>(millideg) / 1000.0;
}

#endif

// -- Memory ------------------------------------------------------------------

#ifdef __linux__

void monitoring::collect_memory(nlohmann::json& out) {
    std::ifstream meminfo("/proc/meminfo");
    if (!meminfo) return;

    uint64_t total = 0, available = 0, swap_total = 0, swap_free = 0;
    std::string line;
    while (std::getline(meminfo, line)) {
        uint64_t val = 0;
        if (line.rfind("MemTotal:", 0) == 0) {
            std::istringstream(line.substr(9)) >> val;
            total = val;
        } else if (line.rfind("MemAvailable:", 0) == 0) {
            std::istringstream(line.substr(13)) >> val;
            available = val;
        } else if (line.rfind("SwapTotal:", 0) == 0) {
            std::istringstream(line.substr(10)) >> val;
            swap_total = val;
        } else if (line.rfind("SwapFree:", 0) == 0) {
            std::istringstream(line.substr(9)) >> val;
            swap_free = val;
        }
    }

    total      *= 1024;   // kB -> bytes
    available  *= 1024;
    swap_total *= 1024;
    swap_free  *= 1024;

    out["total"]     = total;
    out["available"] = available;
    out["usage"]     = total > 0
        ? static_cast<double>(total - available) / static_cast<double>(total) * 100.0
        : 0.0;

    if (swap_total > 0) {
        auto& swap      = out["swap"];
        swap["total"]   = swap_total;
        swap["free"]    = swap_free;
        swap["usage"]   = static_cast<double>(swap_total - swap_free) /
                          static_cast<double>(swap_total) * 100.0;
    }
}

#elif defined(__APPLE__)

void monitoring::collect_memory(nlohmann::json& out) {
    // Total physical memory via sysctl
    int64_t total = 0;
    size_t len = sizeof(total);
    sysctlbyname("hw.memsize", &total, &len, nullptr, 0);

    // VM stats for available memory estimate
    vm_statistics64_data_t vm;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    host_statistics64(mach_host_self(), HOST_VM_INFO64,
                      reinterpret_cast<host_info64_t>(&vm), &count);

    vm_size_t page_size = 0;
    host_page_size(mach_host_self(), &page_size);

    int64_t available = (static_cast<int64_t>(vm.free_count) +
                         static_cast<int64_t>(vm.inactive_count)) *
                        static_cast<int64_t>(page_size);

    out["total"]     = static_cast<uint64_t>(total);
    out["available"] = static_cast<uint64_t>(available);
    out["usage"]     = total > 0
        ? static_cast<double>(total - available) / static_cast<double>(total) * 100.0
        : 0.0;

    // Swap via sysctl
    struct xsw_usage swap_info{};
    size_t swap_len = sizeof(swap_info);
    if (sysctlbyname("vm.swapusage", &swap_info, &swap_len, nullptr, 0) == 0 &&
        swap_info.xsu_total > 0) {
        auto& swap    = out["swap"];
        swap["total"] = swap_info.xsu_total;
        swap["free"]  = swap_info.xsu_avail;
        swap["usage"] = static_cast<double>(swap_info.xsu_used) /
                        static_cast<double>(swap_info.xsu_total) * 100.0;
    }
}

#endif

// -- Disk --------------------------------------------------------------------

void monitoring::set_disk_paths(std::map<std::string, std::string> paths) {
    if (paths.empty()) {
        disk_paths_ = {{"root", "/"}};
    } else {
        disk_paths_ = std::move(paths);
    }
}

void monitoring::collect_disk(nlohmann::json& out) {
    for (const auto& [name, path] : disk_paths_) {
        std::error_code ec;
        auto info = std::filesystem::space(path, ec);
        if (ec) continue;

        auto& disk       = out[name];
        disk["total"]     = info.capacity;
        disk["available"] = info.available;
        disk["usage"]     = info.capacity > 0
            ? static_cast<double>(info.capacity - info.available) /
              static_cast<double>(info.capacity) * 100.0
            : 0.0;
    }
}

// -- Load Average ------------------------------------------------------------

void monitoring::collect_load(nlohmann::json& out) {
    double loads[3] = {};
    if (getloadavg(loads, 3) == 3) {
        out["1m"]  = loads[0];
        out["5m"]  = loads[1];
        out["15m"] = loads[2];
    }
}

// -- Uptime ------------------------------------------------------------------

#ifdef __linux__

uint64_t monitoring::collect_uptime() {
    std::ifstream uptime("/proc/uptime");
    if (!uptime) return 0;
    double secs = 0;
    uptime >> secs;
    return static_cast<uint64_t>(secs);
}

#elif defined(__APPLE__)

uint64_t monitoring::collect_uptime() {
    struct timeval boot;
    size_t len = sizeof(boot);
    int mib[2] = { CTL_KERN, KERN_BOOTTIME };
    if (sysctl(mib, 2, &boot, &len, nullptr, 0) != 0) return 0;
    return static_cast<uint64_t>(time(nullptr) - boot.tv_sec);
}

#endif

// -- Processes ---------------------------------------------------------------

#ifdef __linux__

uint32_t monitoring::collect_processes() {
    uint32_t count = 0;
    // Count numeric directories in /proc (each is a PID)
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator("/proc", ec)) {
        if (!entry.is_directory()) continue;
        const auto& name = entry.path().filename().string();
        if (!name.empty() && std::isdigit(static_cast<unsigned char>(name[0]))) {
            ++count;
        }
    }
    return count;
}

#elif defined(__APPLE__)

uint32_t monitoring::collect_processes() {
    // proc_listallpids with nullptr returns the count
    int count = proc_listallpids(nullptr, 0);
    return count > 0 ? static_cast<uint32_t>(count) : 0;
}

#endif

// -- Network -----------------------------------------------------------------

#ifdef __linux__

monitoring::network_sample monitoring::read_network_sample() {
    network_sample s;
    s.timestamp = std::chrono::steady_clock::now();

    std::ifstream net("/proc/net/dev");
    if (!net) return s;

    std::string line;
    // Skip header lines
    std::getline(net, line);
    std::getline(net, line);

    while (std::getline(net, line)) {
        // Format: "  iface: rx_bytes rx_packets ... tx_bytes tx_packets ..."
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;

        // Skip loopback
        std::string iface = line.substr(0, colon);
        // Trim leading spaces
        auto start = iface.find_first_not_of(' ');
        if (start != std::string::npos) iface = iface.substr(start);
        if (iface == "lo") continue;

        std::istringstream iss(line.substr(colon + 1));
        uint64_t rx_bytes = 0, tx_bytes = 0, skip = 0;
        // rx: bytes packets errs drop fifo frame compressed multicast
        iss >> rx_bytes;
        for (int i = 0; i < 7; ++i) iss >> skip;
        // tx: bytes ...
        iss >> tx_bytes;

        s.rx_bytes += rx_bytes;
        s.tx_bytes += tx_bytes;
    }
    return s;
}

#elif defined(__APPLE__)

monitoring::network_sample monitoring::read_network_sample() {
    network_sample s;
    s.timestamp = std::chrono::steady_clock::now();

    struct ifaddrs* addrs = nullptr;
    if (getifaddrs(&addrs) != 0) return s;

    for (auto* ifa = addrs; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_LINK) continue;
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;

        auto* data = reinterpret_cast<const struct if_data*>(ifa->ifa_data);
        if (!data) continue;

        s.rx_bytes += data->ifi_ibytes;
        s.tx_bytes += data->ifi_obytes;
    }

    freeifaddrs(addrs);
    return s;
}

#endif

void monitoring::collect_network(nlohmann::json& out) {
    auto cur = read_network_sample();
    auto elapsed = std::chrono::duration<double>(cur.timestamp - prev_net_.timestamp).count();

    if (elapsed > 0) {
        out["rx_rate"] = static_cast<double>(cur.rx_bytes - prev_net_.rx_bytes) / elapsed;
        out["tx_rate"] = static_cast<double>(cur.tx_bytes - prev_net_.tx_bytes) / elapsed;
    } else {
        out["rx_rate"] = 0.0;
        out["tx_rate"] = 0.0;
    }

    out["rx_bytes"] = cur.rx_bytes;
    out["tx_bytes"] = cur.tx_bytes;

    prev_net_ = cur;
}

} // namespace thinr::extensions
