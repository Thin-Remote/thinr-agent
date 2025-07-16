
#include "../restore.h"

#include <filesystem>
#include <fstream>

#include "../../utils/date.h"
#include "../../utils/aws.h"
#include "../../utils/tar.h"
#include "../../utils/docker.h"

#include "./utils.h"
#include "../../utils/utils.h"

namespace fs = std::filesystem;

class PlatformRestore : public ThingerMonitorRestore {

public:

    PlatformRestore(thinger::monitor::Config& config, const std::string& hostname, const std::string& tag)
      : ThingerMonitorRestore(config,hostname,tag) {

        backups_folder = this->config().get_data_path()+"/backups";
        storage = this->config().get_storage();
        bucket = this->config().get_bucket(storage);
        region = this->config().get_region(storage);
        access_key = this->config().get_access_key(storage);
        secret_key = this->config().get_secret_key(storage);

        archive_name = this->name()+"_"+this->tag();
        if ( utils::version::is_current_version_newer("1.0.0") )
          archive_name += ".tar";
        else
          archive_name += ".tar.gz";

    }

    nlohmann::json restore() override {

        nlohmann::json data;
        data["operation"] = {};
        data["operation"]["create_backups_folder"] = create_backups_folder();
        if (!data["operation"]["create_backups_folder"]["status"].get<bool>()) {
            data["status"] = false;
            return data;
        }

        // Create archive
        archive = utils::tar::read::create_archive(backups_folder + "/" + archive_name);

        // List files
        std::vector<std::string> files;

        try {

          files = utils::tar::read::list_files( archive );
          data["operation"]["list_files"]["status"] = true;

        } catch ( const utils::tar::error& e ) {
          data["operation"]["list_files"]["status"] = false;
          data["operation"]["list_files"]["error"] = e.what();
        }

      // Reopen archive
      utils::tar::read::close_archive( archive );
      archive = utils::tar::read::create_archive(backups_folder + "/" + archive_name);

        /*data["operation"]["decompress_backup"] = decompress_backup();
        if (!data["operation"]["decompress_backup"]["status"].get<bool>()) {
            data["status"] = false;
            return data;
        }*/

        // Loop over files and do what needs to be done with each of them, extracting them as neccesary
        for(const std::string& file : files) {
          if ( file.starts_with("thinger") )
            data["operation"]["restore_thinger"] = restore_thinger(file);
          else if ( file.starts_with("mongodb") )
            data["operation"]["restore_mongodb"] = restore_mongodb(file);
          else if ( file.starts_with("influxdb") )
            data["operation"]["restore_influxdb"] = restore_influxdb(file);

          // Reopen archive
          utils::tar::read::close_archive( archive );
          archive = utils::tar::read::create_archive(backups_folder + "/" + archive_name);
        }

        data["operation"]["restart_platform"] = restart_platform();

        data["status"] = true;
        for (auto& element : data["operation"]) {
            if (!element["status"].get<bool>()) {
                data["status"] = false;
                break;
            }
        }

        return data;
    }

    nlohmann::json download() override {

        nlohmann::json data;

        data["operation"]["create_backups_folder"] = create_backups_folder();
        if (!data["operation"]["create_backups_folder"]["status"].get<bool>()) {
            data["status"] = false;
            return data;
        }

        if ( storage == "S3" ) {
            data["operation"]["download_s3"] = {};
            if (AWS::download_from_s3(backups_folder + "/" + archive_name, bucket, region, access_key, secret_key)) {
                data["operation"]["download_s3"]["status"] = true;
            } else {
                data["operation"]["download_s3"]["status"] = false;
                data["operation"]["download_s3"]["error"].push_back("Failed downloading from S3");
            }
        }

        data["status"] = true;
        for (auto& element : data["operation"]) {
            if (!element["status"].get<bool>()) {
                data["status"] = false;
                break;
            }
        }

        return data;
    }

    nlohmann::json clean() override {

        nlohmann::json data;

        clean_restore() ? data["status"] = true : data["status"] = false;

        return data;
    }

private:

    std::string backups_folder;
    std::string storage;
    std::string bucket;
    std::string region;
    std::string access_key;
    std::string secret_key;
    std::string archive_name;

    struct archive *archive;

    [[nodiscard]] nlohmann::json create_backups_folder() const {
        nlohmann::json data;

        data["status"] = true;
        if ( !std::filesystem::exists(backups_folder+"/"+tag())
          && !std::filesystem::create_directories(backups_folder+"/"+tag()) ) {

            data["status"] = false;
            data["error"] = "Failed to create backup directory";
        }

        return data;
    }

    [[nodiscard]] nlohmann::json restore_thinger( std::string_view file ) {
      nlohmann::json data;
      data["status"] = true;

      // Remove installation directories from within docker to avoid permission issues
      if ( getuid() != 0 && ! Docker::Container::exec("thinger", "bash -c \"rm -rf /data/users /data/certificates && chown 1000:1000 /data\"" ) ) {
        data["status"] = false;
        data["error"].push_back("Failed removing thinger installed directories");
        return data;
      }

      if ( !Docker::Container::stop("thinger") ) {
        data["status"]  = false;
        data["error"].push_back("Failed stopping thinger container");
        return data;
      }

      // Change path to backups folder
      auto initial = std::filesystem::current_path(); //getting path
      std::filesystem::current_path(backups_folder + "/" + tag());

      // Extract thinger archive from main archive
      if ( ! utils::tar::read::extract_file(archive, file) ) {
      //if ( !Tar::extract(backups_folder+"/"+tag()+"/thinger-"+tag()+".tar")) {
        data["status"]  = false;
        data["error"].push_back("Failed extracting thinger backup from main archive");
        return data;
      }

      // TODO: for version previous 1.0.0
      // Extract contents of thinger archive
      auto thinger_archive = utils::tar::read::create_archive( file );

      std::filesystem::current_path(config().get_data_path() + "/thinger/");

      if ( ! utils::tar::read::extract( thinger_archive ) ) {
        data["status"] = false;
        data["error"].push_back("Failed extracting thinger backup into data directory");
      }

      // Go to initial to current dir and remove temp data
      std::filesystem::current_path( initial );
      std::filesystem::remove( backups_folder + "/" + tag() + "/" + file.data() );

      data["plugins"] = restore_plugins();

      // TODO: container is stopped at this point Remove thinger directories permission within docker
      /*if ( getuid() != 0 && ! Docker::Container::exec("thinger", "chown 0:0 /data" ) ) {
        data["status"] = false;
        data["error"].push_back("Failed restoring permissions of thinger data directory");
        return data;
      }*/

      data["status"] = data["plugins"]["status"].get<bool>() && data["status"].get<bool>();

      return data;
    }

    [[nodiscard]] nlohmann::json restore_mongodb(std::string_view file) {

      nlohmann::json data;

      // Give mongodb directories permission within docker
      if ( getuid() != 0 && ! Docker::Container::exec("mongodb", "chown 1000:1000 /data/db" ) ) {
        data["status"] = false;
        data["error"].push_back("Failed restoring permissions of mongodb data directory");
        return data;
      }

      // Change path to mongodb data dir
      auto initial = std::filesystem::current_path(); //getting path
      std::filesystem::current_path( config().get_data_path() + "/mongodb/" );

      // Extract mongodb dump from main archive
      // TODO: differentiate between tar (old) and gzip (new)
      if ( ! utils::tar::read::extract_file(archive, file) ) {
        data["status"]  = false;
        data["error"].push_back("Failed extracting mongodb backup from main archive");
        return data;
      }

      // get mongodb root password
      std::ifstream compose (config().get_compose_path()+"/docker-compose.yml", std::ifstream::in);
      std::string line;

      std::string mongo_password;

      while(std::getline(compose,line,'\n')) {
        if (line.find("- MONGO_INITDB_ROOT_PASSWORD") != std::string::npos) {
          auto first_del = line.find('=');
          auto last_del = line.find('\n');
          mongo_password = line.substr(first_del+1, last_del - first_del-1);
        }
      }

      Docker::Container::exec("mongodb", "chown 999:999 /data/db/");

      if ( !Docker::Container::exec("mongodb", "mongorestore --gzip --archive=/data/db/" + std::string(file.data()) + " -u thinger -p " + mongo_password) ) {
        data["status"] = false;
        data["error"].push_back("Failed restoring mongodb backup");
        return data;
      }

      Docker::Container::exec("mongodb", "chown 1000:1000 /data/db/");

      // Remove file
      std::filesystem::remove( file );

      data["status"] = true;

      // Remove mongodb directories permission within docker
      if ( getuid() != 0 && ! Docker::Container::exec("mongodb", "chown 999:999 /data/db" ) ) {
        data["status"] = false;
        data["error"].push_back("Failed restoring permissions of mongodb data directory");
        return data;
      }

      // Go to initial to current dir and remove temp data
      std::filesystem::current_path( initial );

      return data;
    }

    [[nodiscard]] nlohmann::json restore_influxdb(std::string_view file) {

      nlohmann::json data;

      // Give influxdb directories permission within docker
      if ( getuid() != 0 && ! Docker::Container::exec("influxdb2", "chown 1000:1000 /var/lib/influxdb2/" ) ) {
        data["status"] = false;
        data["error"].push_back("Failed restoring permissions of thinger data directory");
        return data;
      }

      // Change path to influxdb data dir
      auto initial = std::filesystem::current_path(); //getting path
      std::filesystem::current_path(backups_folder + "/" + tag());

      // Extract influxdb dump from main archive
      // TODO: differentiate between tar (old) and gzip (new)
      if ( ! utils::tar::read::extract_file(archive, file) ) {
        data["status"]  = false;
        data["error"].push_back("Failed extracting influxdb2 dump from main archive");
        return data;
      }

      // Extract contents of influxdb archive
      auto influxdb_archive = utils::tar::read::create_archive( file );

      std::filesystem::current_path( config().get_data_path() + "/influxdb2/" );

      if ( ! utils::tar::read::extract( influxdb_archive ) ) {
        data["status"] = false;
        data["error"].push_back("Failed extracting influxdb backup into database directory");
      }

      std::filesystem::remove( backups_folder + "/" + tag() + "/" + file.data() );

      std::string influxdbdump_file = file.data();
      if (influxdbdump_file.length() >= 4 && influxdbdump_file.ends_with(".tar")) {
        // Remove the ".tar" extension
        influxdbdump_file.erase(influxdbdump_file.length() - 4);
      }

      if (!Docker::Container::exec("influxdb2", "influx restore /var/lib/influxdb2/" + influxdbdump_file + " --full")) {
        data["status"]  = false;
        data["error"].push_back("Failed restoring influxdb2 backup");
        return data;
      }

      // Remove file
      std::filesystem::remove( file );

      data["status"] = true;

      // Remove mongodb directories permission within docker
      if ( getuid() != 0 && ! Docker::Container::exec("influxdb2", "chown 0:0 /var/lib/influxdb2" ) ) {
        data["status"] = false;
        data["error"].push_back("Failed restoring permissions of influxdb2 data directory");
        return data;
      }

      // Go to initial to current dir and remove temp data
      std::filesystem::current_path( initial );

      return data;

    }

    [[nodiscard]] nlohmann::json restore_plugins() {

        nlohmann::json data;

        // Executed after restore_thinger
        if ( ! std::filesystem::exists( config().get_data_path() + "/thinger/plugins/" ) ) {
            data["status"] = true;
            data["msg"].push_back("Platform has no users folder");
            return data;
        }

        for (const auto & p1 : fs::directory_iterator(config().get_data_path()+"/thinger/users/")) { // users
            if (! std::filesystem::exists(p1.path().string()+"/plugins/")) continue;

            std::string user = p1.path().filename().string();

            // restore networks
            std::string network_id = Docker::Network::create_from_inspect(config().get_data_path()+"/thinger/plugins/"+user+"-network.json");
            if (network_id.empty()) {
                data["error"].push_back("Failed restoring "+user+" docker network");
            } else {
                data["msg"].push_back("Restored "+user+" docker network");
            }

            // restore plugins
            for (const auto & p2 : fs::directory_iterator(p1.path().string()+"/plugins/")) { // plugins
                std::string container_name = user+"-"+p2.path().filename().string();

                // Parse plugin.json to check if plugins are docker
                nlohmann::json j;
                std::filesystem::path f(p2.path().string() + "/files/plugin.json");

                if (std::filesystem::exists(f)) {
                  std::ifstream config_file(f.string());
                  config_file >> j;
                }

                if ( thinger::monitor::config::get(j, "/task/type"_json_pointer, std::string("")) == "docker" ) {

                    if (Docker::Container::create_from_inspect(config().get_data_path()+"/thinger/plugins/"+container_name+".json", network_id))
                        data["msg"].push_back("Restored "+container_name+" docker container");
                    else
                        data["error"].push_back("Failed restoring "+container_name+" docker container");

                } else {
                    data["msg"].push_back("Ignored  " + container_name + " restore as it has no container associated");
                }

                if (Docker::Container::start(container_name))
                    data["msg"].push_back("Started "+container_name+" docker containeer");
                else
                    data["error"].push_back("Failed starting "+container_name+" docker container");
            }
        }

        // Remove plugins restore folder
        std::filesystem::remove_all(config().get_data_path()+"/thinger/plugins/");

        if (data.contains("error"))
            data["status"] = false;
        else
            data["status"] = true;

        return data;
    }

    [[nodiscard]] bool clean_restore() const {
        std::filesystem::remove_all(backups_folder+"/"+archive_name);
        std::filesystem::remove_all(backups_folder+"/"+tag());
        std::filesystem::remove_all(backups_folder);
        bool status = Docker::Container::exec("mongodb", "rm -rf /dump");

        status = status && Docker::Container::exec("influxdb2", "rm -rf /dump");

        return status;
    }

    [[nodiscard]] nlohmann::json restart_platform() {
        nlohmann::json data;


        if (!Docker::Container::restart("mongodb"))
            data["error"].push_back("Failed restaring mongodb container");
        if (!Docker::Container::restart("influxdb2"))
            data["error"].push_back("Failed restarting influxdb2 container");
        if (!Docker::Container::restart("thinger"))
            data["error"].push_back("Failed restarting thinger container");

        if (data.contains("error"))
            data["status"] = false;
        else
            data["status"] = true;

        return data;
    }

};
