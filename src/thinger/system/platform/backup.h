
#include "../backup.h"

#include <filesystem>
#include <fstream>

#include "../../utils/aws.h"
#include "../../utils/docker.h"
#include "../../utils/tar.h"

#include "./utils.h"

namespace fs = std::filesystem;

class PlatformBackup : public ThingerMonitorBackup {

public:

  PlatformBackup(thinger::monitor::Config& config, const std::string& hostname, const std::string& tag)
      : ThingerMonitorBackup(config,hostname, tag) {

    backups_folder = this->config().get_data_path()+"/backups";
    storage = this->config().get_storage();
    bucket = this->config().get_bucket(storage);
    region = this->config().get_region(storage);
    access_key = this->config().get_access_key(storage);
    secret_key = this->config().get_secret_key(storage);

    file_to_upload = this->name()+"_"+this->tag()+".tar";

  }

  nlohmann::json backup() override {


    nlohmann::json data;
    data["operation"] = {};
    data["operation"]["create_backups_folder"] = create_backups_folder();
    if (!data["operation"]["create_backups_folder"]["status"].get<bool>()) {
      data["status"] = false;
      return data;
    }

    archive = utils::tar::write::create_archive(backups_folder + "/" + name() + "_" + tag() + ".tar");

    data["operation"]["backup_thinger"]   = backup_thinger();
    data["operation"]["backup_mongodb"]   = backup_mongodb();
    data["operation"]["backup_influxdb"]  = backup_influxdb();

    // Set global status value
    data["status"] = true;
    for (auto& element : data["operation"]) {
      if (!element["status"].get<bool>()) {
        data["status"] = false;
        break;
      }
    }

    // Close global archive
    utils::tar::write::close_archive( archive );

    return data;
  }

  nlohmann::json upload() override  {

    nlohmann::json data;

    if ( storage == "S3" ) {
      data["operation"]["upload_s3"] = {};
      if (AWS::multipart_upload_to_s3(backups_folder + "/" + file_to_upload, bucket, region, access_key,
                                      secret_key)) {
        data["operation"]["upload_s3"]["status"] = true;
      } else {
        data["operation"]["upload_s3"]["status"] = false;
        data["operation"]["upload_s3"]["error"].push_back("Failed uploading to S3");
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

    clean_backup() ? data["status"] = true : data["status"] = false;

    return data;
  }

private:

  std::string backups_folder;
  std::string storage;
  std::string bucket;
  std::string region;
  std::string access_key;
  std::string secret_key;

  struct archive *archive;

  std::string file_to_upload;

  [[nodiscard]] nlohmann::json create_backups_folder() const {
    nlohmann::json data;

    std::filesystem::remove_all(backups_folder+"/"+tag());
    if (!std::filesystem::create_directories(backups_folder+"/"+tag())) {
      data["status"]  = false;
      data["error"].push_back("Failed to create backup directory");
    } else
      data["status"]  = true;

    return data;
  }

  [[nodiscard]] nlohmann::json backup_thinger() {
    nlohmann::json data;

    // Create archive
    auto thinger_archive_filename = "thinger_" + tag() + ".tar.gz";
    auto thinger_archive = utils::tar::write::create_archive(backups_folder + "/" + tag() + "/" + thinger_archive_filename);

    // Change path to thinger folder
    auto initial = std::filesystem::current_path(); //getting path
    std::filesystem::current_path(config().get_data_path() + "/thinger/");

    // With tar creation instead of copying to folder we maintain ownership and permissions
    data["status"] = true;
    if (std::filesystem::exists("users")) {

      // Add users
      utils::tar::write::add_directory(thinger_archive, "users");
    }

    // Add certificates
    utils::tar::write::add_directory(thinger_archive, "certificates");

    // Get plugins
    data["plugins"]  = backup_plugins();
    if ( data["plugins"]["status"] )
      data["status"] = data["status"] && data["plugins"]["status"];

    // Add plugins to thinger archive
    std::filesystem::current_path( backups_folder + "/" + tag() );
    if ( std::filesystem::exists("plugins") ) {
      utils::tar::write::add_directory(thinger_archive, "plugins");
      std::filesystem::remove_all("plugins");
    }

    utils::tar::write::close_archive(thinger_archive);

    // Add backup to main archive and remove file
    std::filesystem::current_path( backups_folder + "/" + tag() );
    utils::tar::write::add_entry( archive, thinger_archive_filename);
    std::filesystem::remove(thinger_archive_filename);

    // Go to initial to current dir
    std::filesystem::current_path( initial );

    return data;
  }

  [[nodiscard]] nlohmann::json backup_mongodb() {

    nlohmann::json data;

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

    /* For future reference: pipe mongodbdump to a tar
    auto entry = utils::tar::write::create_entry(archive, "mongodbdump_"+tag());
    std::function<void(const char*, size_t, uint64_t, uint64_t)> stdf_write_data_block_to_entry = [this, &entry](const char* data, size_t data_length, uint64_t offset, uint64_t total_length) {
      archive* a = archive;
      archive_entry* e = entry;
      utils::tar::write::write_data_block_to_entry(a, e, data, data_length, offset, total_length);
    };
    if ( ! Docker::Container::exec("mongodb", "mongodump -u thinger -p "+mongo_password+" --archive --gzip", true, stdf_write_data_block_to_entry) ) {
    */

    /* For future reference: how to save the mongodb dump into a file
     Execute backup in mongodb container with the shared volume as output
    std::ofstream backup_file(backups_folder+"/"+tag()+"/mongodbdump-"+tag()+".gz", std::ios::binary);
    if ( ! Docker::Container::exec("mongodb", "mongodump -u thinger -p "+mongo_password+" --archive --gzip", true, backup_file) ) {
    */

    auto mongodbdump_filename = "mongodbdump_" + tag() + ".gz";

    // Execute backup in mongodb container directly into the global archive
    if ( ! Docker::Container::exec("mongodb",
                                   "mongodump -u thinger -p "+mongo_password+" --archive=/data/db/"+mongodbdump_filename+" --gzip" ) ) {
      data["status"]  = false;
      data["error"].push_back("Failed executing mongodb backup");
      return data;
    }

    // Change path to mongodb data folder
    auto initial = std::filesystem::current_path(); //getting path
    std::filesystem::current_path(config().get_data_path() + "/mongodb/");

    // Add backup to main archive and remove file
    utils::tar::write::add_entry( archive, mongodbdump_filename);
    // Removing file in docker container to avoid permission issues
    if ( ! Docker::Container::exec("mongodb", "rm -f /data/db/"+mongodbdump_filename ) ) {
      data["status"]  = false;
      data["error"].push_back("Failed removing mongodb backup");
      return data;
    }

    // Go to initial to current dir
    std::filesystem::current_path( initial );

    data["status"] = true;

    return data;
  }

  [[nodiscard]] nlohmann::json backup_influxdb() {
    nlohmann::json data;

    // Create archive
    auto influxdb2dump_filename = "influxdb2dump_" + tag();
    auto influxdb2dump_archive_filename = influxdb2dump_filename + ".tar";
    auto influxdb2dump_archive = utils::tar::write::create_archive(backups_folder + "/" + tag() + "/" + influxdb2dump_archive_filename);

    // get influx token
    std::ifstream compose(config().get_compose_path() + "/docker-compose.yml", std::ifstream::in);
    std::string line;

    std::string influx_token;

    while (std::getline(compose, line, '\n')) {
      if (line.find("- DOCKER_INFLUXDB_INIT_ADMIN_TOKEN") != std::string::npos) {
        auto first_del = line.find('=');
        auto last_del = line.find('\n');
        influx_token = line.substr(first_del + 1, last_del - first_del - 1);
      }
    }


    if (!Docker::Container::exec("influxdb2",
                                 "influx backup /var/lib/influxdb2/" + influxdb2dump_filename + " -t " + influx_token)) {
      data["status"] = false;
      data["error"].push_back("Failed executing influxdb2 backup");
      return data;
    }

    // Give influxdb directories permission within docker
    if ( getuid() != 0 && ! Docker::Container::exec("influxdb2", "chown -R 1000:1000 /var/lib/influxdb2/"+influxdb2dump_filename ) ) {
      data["status"] = false;
      data["error"].push_back("Failed restoring permissions of mongodb data directory");
      return data;
    }

    // Change path to influxdb2 data folder
    auto initial = std::filesystem::current_path(); //getting path
    std::filesystem::current_path(config().get_data_path() + "/influxdb2/");

    // Add backup to influxdb2 archive and remove file
    utils::tar::write::add_directory( influxdb2dump_archive, influxdb2dump_filename );
    utils::tar::write::close_archive( influxdb2dump_archive );

    // Removing file in docker container to avoid permission issues
    if ( ! Docker::Container::exec("influxdb2", "rm -rf /var/lib/influxdb2/"+influxdb2dump_filename ) ) {
      data["status"]  = false;
      data["error"].push_back("Failed removing mongodb backup");
      return data;
    }

    // Add influxdb2 archive to main archive and remove file
    std::filesystem::current_path( backups_folder + "/" + tag() );
    utils::tar::write::add_entry( archive, influxdb2dump_archive_filename );
    std::filesystem::remove( influxdb2dump_archive_filename );

    // Go to initial to current dir
    std::filesystem::current_path( initial );

    data["status"] = true;

    return data;

  }

  [[nodiscard]] nlohmann::json backup_plugins() {

    nlohmann::json data;

    // Change path to thinger folder
    auto initial = std::filesystem::current_path(); //getting path
    std::filesystem::current_path( config().get_data_path() + "/thinger" );

    // Users folders exists when there are plugins installed
    if ( ! std::filesystem::exists("users") ) {
      data["status"] = true;
      data["msg"].push_back("Platform has no users folder");
      return data;
    }

    std::string plugins_bk_path = backups_folder + "/" + tag() + "/plugins";

    // Create plugins directory in backups folder
    if ( ! std::filesystem::create_directories( plugins_bk_path ) ) {
      data["status"]  = false;
      data["error"].push_back("Failed creating plugins directory in backup folder");
      return data;
    }

    // Iterate over uses
    for (const auto & p1 : fs::directory_iterator( "users" ) ) { // users

      if (! std::filesystem::exists(p1.path().string()+"/plugins/")) continue; // user has no plugins

      std::string user = p1.path().filename().string();

      // backups networks
      if ( Docker::Network::inspect( user, plugins_bk_path ) ) {
        data["msg"].push_back("Backed up "+user+" docker network");
      } else {
        data["error"].push_back("Failed backing up "+user+" docker network");
      }

      // backup plugins
      for (const auto & p2 : fs::directory_iterator(p1.path().string()+"/plugins/")) { // plugins
        std::string container_name = user + "-" + p2.path().filename().string();

        // Parse plugin.json to check if plugins are docker
        nlohmann::json j;
        std::filesystem::path f(p2.path().string() + "/files/plugin.json");

        if (std::filesystem::exists(f)) {
          std::ifstream config_file(f.string());
          config_file >> j;
        }

        if ( thinger::monitor::config::get(j, "/task/type"_json_pointer, std::string("")) == "docker" ) {

          if ( Docker::Container::inspect( container_name, plugins_bk_path ) ) {
            data["msg"].push_back("Backed up " + container_name + " docker container");
          } else {
            data["error"].push_back("Failed backing up " + container_name + " docker container");
          }

        } else {
          data["msg"].push_back("Ignored  " + container_name + " backup as it has no container associated");
        }
      }
    }

    if (data.contains("error"))
      data["status"] = false;
    else
      data["status"] = true;

    // set current path to initial
    std::filesystem::current_path( initial );

    return data;
  }

  [[nodiscard]] bool clean_backup() const {
    std::filesystem::remove_all(backups_folder+"/"+tag());
    std::filesystem::remove_all(backups_folder+"/"+file_to_upload);

    return true;
  }

};