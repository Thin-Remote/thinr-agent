#include "monitor.hpp"

namespace thinger::monitor{

  monitor::monitor(iotmp::client& client, Config& config, httplib::Server& server) :
    client_(client), config_(config), server_(server)
  {

    // Executed only once
    out_["hostname"] = system::get_hostname();
    out_["os_version"] = system::get_os_version();
    out_["kernel_version"] = system::get_kernel_version();

    cpu_cores = cpu::get_cpu_cores();
    out_["cpu_cores"] = cpu_cores;

    client_["monitor"](server_) = [this](iotmp::output& out) {

      get_system_values();
      get_cpu_values();
      get_storage_values();
      get_io_values();
      get_network_values();
      get_ram_values();

      if (config_.get_backup() == "platform")
        out_["console_version"] = platform::getConsoleVersion();

      protoson::json_decoder::parse(out_, out);
    };

  }

  void monitor::reload() {

    interfaces_.clear();
    filesystems_.clear();
    drives_.clear();

    for (const auto& fs_path : config_.get_filesystems()) {
      storage::filesystem fs;
      fs.path = fs_path;
      filesystems_.push_back(fs);
    }
    retrieve_fs_stats(filesystems_);

    for (const auto& dv_name : config_.get_drives()) {
      io::drive dv;
      dv.name = dv_name;
      drives_.push_back(dv);
    }
    retrieve_dv_stats(drives_);

    for (const auto& ifc_name : config_.get_interfaces()) {
      network::interface ifc;
      ifc.name = ifc_name;
      ifc.internal_ip = network::getIPAddress(ifc.name);
      interfaces_.push_back(ifc);
    }
    retrieve_ifc_stats(interfaces_);

  }

  void monitor::get_system_values() {
    // System information
    out_["si_uptime"] = system::get_uptime();
    out_["si_restart"] = system::get_restart_status();

    unsigned int normal_updates = 0;
    unsigned int security_updates = 0;
    system::retrieve_updates(normal_updates, security_updates);

    out_["si_normal_updates"] = normal_updates;
    out_["si_security_updates"] = security_updates;
    out_["si_sw_version"] = VERSION;

  }

  void monitor::get_cpu_values() {

    cpu::retrieve_cpu_loads(cpu_loads); // load and usage is updated every 5s by sysinfo
    out_["cpu_load_1m"] = cpu_loads[0];
    out_["cpu_load_5m"] = cpu_loads[1];
    out_["cpu_load_15m"] = cpu_loads[2];

    out_["cpu_usage"] = cpu::get_cpu_usage(cpu_loads, cpu_cores);

    out_["cpu_procs"] = cpu::get_cpu_procs();

  }

  void monitor::get_storage_values() {

    // Storage
    storage::retrieve_fs_stats(filesystems_);
    for (auto const & fs : filesystems_) {
      std::string name = fs.path;
      if ( config_.get_defaults() && &fs == &filesystems_.front()) {
        name = "default";
      }
      out_[("st_"+name+"_capacity").c_str()] = std::trunc( (float)fs.space_info.capacity / (float)btogb * 100 ) / 100;
      out_[("st_"+name+"_used").c_str()] = std::trunc( (float)(fs.space_info.capacity - fs.space_info.free) / (float)btogb * 100 ) / 100;
      out_[("st_"+name+"_free").c_str()] = std::trunc( (float)fs.space_info.free / (float)btogb * 100 ) / 100;
      out_[("st_"+name+"_usage").c_str()] = std::trunc( ((float)(fs.space_info.capacity - fs.space_info.free)*100) / (float)fs.space_info.capacity * 100 ) / 100; // usage based on time of io spend doing io operations
    }

  }

  void monitor::get_io_values() {

    // IO
    retrieve_dv_stats(drives_);
    for (auto & dv : drives_) {
      std::string name = dv.name;
      if ( config_.get_defaults() && &dv == &drives_.front()) {
        name = "default";
      }

      float speed_reading = (((float)(dv.total_io[1][0] - dv.total_io[0][0]) * SECTOR_SIZE) /
                             (float)(dv.total_io[1][3] - dv.total_io[0][3]))*1000;
      float speed_writing = (((float)(dv.total_io[1][1] - dv.total_io[0][1]) * SECTOR_SIZE) /
                             (float)(dv.total_io[1][3] - dv.total_io[0][3]))*1000;
      float usage = (float)(dv.total_io[1][2] - dv.total_io[0][2]) / (float)(dv.total_io[1][3] - dv.total_io[0][3]);

      out_[("dv_"+name+"_speed_read").c_str()] = std::trunc( speed_reading / btokb * 100 ) / 100;
      out_[("dv_"+name+"_speed_written").c_str()] = std::trunc( speed_writing / btokb * 100 ) / 100;
      out_[("dv_"+name+"_usage").c_str()] = usage < 1 ? std::trunc( usage * 100 * 100 ) / 100 : 100;

      // flip matrix for speed and usage calculations
      for (int z = 0; z < 4; z++) {
        dv.total_io[0][z] = dv.total_io[1][z];
      }
    }

  }

  void monitor::get_network_values() {

    // Network

    network::retrieve_ifc_stats(interfaces_);
    for (auto & ifc : interfaces_) {
      std::string name = ifc.name;
      if ( config_.get_defaults() && &ifc == &interfaces_.front()) {
        name = "default";
      }

      out_[("nw_"+name+"_internal_ip").c_str()] = ifc.internal_ip;

      out_[("nw_"+name+"_transfer_incoming").c_str()]    = std::trunc( (float)ifc.total_transfer[0][1] / (float)btogb * 100 ) / 100;
      out_[("nw_"+name+"_transfer_outgoing").c_str()]    = std::trunc( (float)ifc.total_transfer[1][1] / (float)btogb * 100 ) / 100;
      out_[("nw_"+name+"_transfer_total").c_str()]       = std::trunc( ((float)ifc.total_transfer[0][1] + (float)ifc.total_transfer[1][1]) / (float)btogb * 100 ) / 100;
      out_[("nw_"+name+"_packetloss_incoming").c_str()]  = ifc.total_packets[1];
      out_[("nw_"+name+"_packetloss_outgoing").c_str()]  = ifc.total_packets[3];

      // speeds in B/s
      float speed_incoming = ((float)(ifc.total_transfer[0][1] - ifc.total_transfer[0][0]) /
                              (float)(ifc.total_transfer[2][1] - ifc.total_transfer[2][0]))*1000;
      float speed_outgoing = ((float)(ifc.total_transfer[1][1] - ifc.total_transfer[1][0]) /
                              (float)(ifc.total_transfer[2][1] - ifc.total_transfer[2][0]))*1000;

      out_[("nw_"+name+"_speed_incoming").c_str()] = std::trunc( speed_incoming * 8 / btokb * 100 ) / 100;
      out_[("nw_"+name+"_speed_outgoing").c_str()] = std::trunc( speed_outgoing * 8 / btokb * 100 ) / 100;
      out_[("nw_"+name+"_speed_total").c_str()]    = std::trunc( ((speed_incoming + speed_outgoing) * 8) / btokb * 100 ) / 100;

      // flip matrix for speed and usage calculations
      for (int i = 0; i < 3; i++) {
        ifc.total_transfer[i][0] = ifc.total_transfer[i][1];
      }
    }

    out_["nw_public_ip"] = network::getPublicIPAddress();

  }

  void monitor::get_ram_values() {

    // RAM
    memory::get_ram(ram_);
    out_["ram_total"] = std::trunc( (float)ram_[0] / kbtogb * 100 ) / 100;
    out_["ram_available"] = std::trunc( (float)ram_[1] / kbtogb * 100) / 100;
    out_["ram_used"] = std::trunc( (float)(ram_[0] - ram_[1]) / kbtogb * 100 ) / 100;
    out_["ram_usage"] = std::trunc( (float)((ram_[0] - ram_[1]) * 100) / (float)ram_[0] * 100 ) / 100;
    out_["ram_swaptotal"] = std::trunc( (float)ram_[2] / (float)kbtogb * 100 ) / 100;
    out_["ram_swapfree"] = std::trunc( (float)ram_[3] / kbtogb * 100 ) / 100;
    out_["ram_swapused"] = std::trunc( (float)(ram_[2] - ram_[3]) / kbtogb * 100) / 100;
    out_["ram_swapusage"] = (ram_[2] == 0) ? 0 : std::trunc( (float)((ram_[2] - ram_[3]) *100) / (double)ram_[2] * 100 ) / 100;

  }


}