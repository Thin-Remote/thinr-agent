#include "config/config.hpp"

#include <httplib.h>

#include <unistd.h>
#include <thread>
#include <future>

#include <nlohmann/json.hpp>
#include "utils/date.h"

#include "system/platform/backup.h"
#include "system/platform/restore.h"

#include "monitor/monitor.hpp"

namespace fs = std::filesystem;

#if OPEN_SSL
  #define CPPHTTPLIB_OPENSSL_SUPPORT
#endif

namespace thinger::monitor {

    class Client {

    public:

        Client(thinger::iotmp::client& client, Config& config) :
            resources_{
              {"backup", client["backup"]},
              {"restore", client["restore"]},
            },
            monitor_(client, config, server_),
            config_(config)
        {
          //monitor_.initialize(client, config, server_);

            /*if ( ! config_.get_backup().empty() ) {
              resources_.insert( {"backup", client["backup"]});
              resources_.insert( {"restore", client["restore"]});
            }*/

            //if ( ! config_.get_backup().empty() ) {
              resources_.at("backup") = [this, &client](iotmp::input &in, iotmp::output &out) {

                if (!config_.get_backup().empty()) {

                  std::string tag = in["tag"];
                  std::string endpoint = in["endpoint"];

                  auto today = Date();
                  in["tag"] = today.to_iso8601();
                  in["endpoint"] = "backup_finished";

                  if (f1.future.valid() && f1.future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                    if (f1.task == "backup")
                      out["status"] = "Already executing";
                    else
                      out["status"] = ("Executing: " + f1.task).c_str();
                    return;
                  }

                  // future from a packaged_task
                  std::packaged_task<void(std::string, std::string)> task(
                      [this, &client](const std::string &task_tag, const std::string &task_endpoint) {

                        std::unique_ptr<ThingerMonitorBackup> backup{}; // as nullptr

                        nlohmann::json data;
                        data["device"] = config_.get_id();
                        data["hostname"] = monitor_.get<std::string>("hostname");
                        data["backup"] = {};
                        data["backup"]["operations"] = {};

                        // Add new possible options for backup systems
                        if (config_.get_backup() == "platform") {
                          backup = std::make_unique<PlatformBackup>(config_, data["hostname"] , task_tag);
                        }

                        data["backup"]["operations"]["backup"] = backup->backup();
                        data["backup"]["operations"]["upload"] = backup->upload();
                        data["backup"]["operations"]["clean"] = backup->clean();

                        data["backup"]["status"] = true;
                        for (auto &element: data["backup"]["operations"]) {
                          if (!element["status"].get<bool>()) {
                            data["backup"]["status"] = false;
                            break;
                          }
                        }

                        //LOG_LEVEL(1, "[_BACKUP] Backup status: {0}", data.dump());
                        LOG_LEVEL(1, "[_BACKUP] Backup status: %s", data.dump());
                        if (!task_endpoint.empty()) {
                          protoson::pson payload;
                          protoson::json_decoder::parse(data, payload);
                          client.call_endpoint(task_endpoint.c_str(), payload);
                        }

                      });

                  if (!tag.empty()) {
                    out["status"] = "Launched";
                    f1.task = "backup";
                    f1.future = task.get_future();  // get a future
                    std::thread thread(std::move(task), tag, endpoint);
                    thread.detach();
                  }

                  out["status"] = "Ready to be launched";

                } else {
                  out["status"] = "ERROR";
                  out["error"] = "Can't launch backup. Set backups property.";
                }
              };

              resources_.at("restore") = [this, &client](iotmp::input &in, iotmp::output &out) {

                if (!config_.get_backup().empty()) {

                  std::string tag = in["tag"];
                  std::string endpoint = in["endpoint"];

                  auto today = Date();
                  in["tag"] = today.to_iso8601();
                  in["endpoint"] = "restore_finished";

                  if (f1.future.valid() && f1.future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                    if (f1.task == "restore")
                      out["status"] = "Already executing";
                    else
                      out["status"] = ("Executing: " + f1.task).c_str();
                    return;
                  }

                  // TODO: Catch exception in thread
                  std::packaged_task<void(std::string, std::string)> task(
                      [this, &client](const std::string &task_tag, const std::string &task_endpoint) {

                        std::unique_ptr<ThingerMonitorRestore> restore{};

                        nlohmann::json data;
                        data["device"] = config_.get_id();
                        data["hostname"] = monitor_.get<std::string>("hostname");
                        data["restore"] = {};
                        data["restore"]["operations"] = {};

                        // Add new possible options for backup systems
                        if (config_.get_backup() == "platform") {
                          restore = std::make_unique<PlatformRestore>(config_, data["hostname"] , task_tag);
                        }

                        LOG_INFO("[___RSTR] Downloading backup");
                        data["restore"]["operations"]["download"] = restore->download();
                        LOG_INFO("[___RSTR] Restoring backup");
                        data["restore"]["operations"]["restore"] = restore->restore();
                        LOG_INFO("[___RSTR] Cleaning backup temporary files");
                        data["restore"]["operations"]["clean"] = restore->clean();
                        //LOG_LEVEL(1, "[___RSTR] Restore status: {0}", data.dump());
                        LOG_LEVEL(1, "[___RSTR] Restore status: %s", data.dump());
                        if (!task_endpoint.empty()) {
                          protoson::pson payload;
                          protoson::json_decoder::parse(data, payload);
                          client.call_endpoint(task_endpoint.c_str(), payload);
                        }

                      });

                  if (!tag.empty()) {
                    out["status"] = "Launched";
                    f1.task = "restore";
                    f1.future = task.get_future();
                    std::thread thread(std::move(task), tag, endpoint);
                    thread.detach();
                  }

                  out["status"] = "Ready to be launched";

                } else {
                  out["status"] = "ERROR";
                  out["error"] = "Can't launch restore. Set backups property.";
                }
              };

            start_local_server();

    }

    void stop_local_server() {

          if ( server_.is_running() && server_.is_valid() ) {
            THINGER_LOG("Stopping local server");
            server_.stop();
            svr_thread.join();
          }

    }

    void start_local_server() {
          stop_local_server();
          THINGER_LOG("Creating local server in %s and port %hd", config_.get_svr_host(), config_.get_svr_port());
          svr_thread = std::thread([&]() { server_.listen(config_.get_svr_host(), config_.get_svr_port()); });
    }

    virtual ~Client() {
        THINGER_LOG("stopping monitoring client");
        stop_local_server();
    }

    // Recreates and fills up structures
    void reload_configuration(std::string_view const& property) {

          if ( "resources" == property ) {
            monitor_.reload();
          }

          start_local_server();

    }

private:

    std::unordered_map<std::string, iotmp::iotmp_resource&> resources_;

    // local server for resources
    httplib::Server server_;
    std::thread svr_thread;

    thinger::monitor::monitor monitor_;

    Config& config_;

    struct future_task {
        std::string task;
        std::future<void> future;
    };
    future_task f1; // f1 used for blocking backup/restore/update/update_distro

    };

}