
#ifndef THINGER_MONITOR_CONFIG_HPP
#define THINGER_MONITOR_CONFIG_HPP

#include "nlohmann/json.hpp"

#include <map>
#include <iostream>
#include <fstream>
#include <random>
#include <regex>

#include "thinger/iotmp/core/pson.h"
#include "thinger/util/json_decoder.hpp"

constexpr std::string_view DF_CONFIG_PATH = "/etc/thinger_io/thinger_monitor.json";

namespace thinger::monitor::config {

    template< typename T >
    T get(nlohmann::json j, const nlohmann::json::json_pointer& p, T fallback) {

        try {
            return j.at(p);
        } catch (nlohmann::json::out_of_range const&) {
            return fallback;
        }
    }

}

namespace thinger::monitor::utils {

    bool is_placeholder(const std::string& value);

    std::string generate_credentials(std::size_t length);

}

namespace thinger::monitor {

    class Config {
    public:

        const std::vector<std::string> remote_properties = {"resources","backups","storage"};

        Config() {
            path_ = std::string(DF_CONFIG_PATH);
            load_config();
        }

        explicit Config(std::string_view path) : path_(path) {
            load_config();
        }

        bool update(std::string const& property, protoson::pson& data) {

          nlohmann::json remote_config;
          protoson::json_decoder::to_json(data, remote_config);

          if (remote_config != config_remote_[property]) {
                config_remote_[property] = remote_config;

                return true;
            }
            return false;
        }

        /* Setters */
        void set_path(std::string_view path) {
            path_ = path;
            load_config();
        }

        void set_url(std::string_view url) {
            config_local_["server"]["url"] = url;
        }

        void set_user(std::string_view user) {
            config_local_["server"]["user"] = user;
        }

        void set_ssl(bool ssl) {
            config_local_["server"]["ssl"] = ssl;
        }

        void set_device() {
            // Check if device name exists, if it's not set it to hostname
            if (this->get_id().empty()) {
                std::string hostname;
                std::ifstream hostinfo ("/etc/hostname", std::ifstream::in);
                hostinfo >> hostname;

                if (this->get_name().empty())
                    config_local_["device"]["name"] = hostname;

                // device_id can't use some chars
                std::ranges::replace(hostname.begin(), hostname.end(),'.','_');
                std::ranges::replace(hostname.begin(), hostname.end(),'-','_');

                // device_id has a max of 32 chars
                std::string device_id = hostname.substr(0,32);
                if ( device_id.length() == 32 &&
                     device_id.find('_') != std::string::npos &&
                     ! hostname.substr(32).starts_with('_') )
                {
                  size_t pos = device_id.find_last_of('_');
                  device_id = hostname.substr(0,pos);
                }
                config_local_["device"]["id"] = device_id;
            }
            if (this->get_credentials().empty()) {
                config_local_["device"]["credentials"] = utils::generate_credentials(16);
            }
            save_config();
        }

        /* Getters */
        [[nodiscard]] std::string get_url() const {
            return config::get(config_local_, "/server/url"_json_pointer, std::string(""));
        }

        [[nodiscard]] std::string get_user() const {

            return config::get(config_local_, "/server/user"_json_pointer, std::string(""));
        }

        [[nodiscard]] std::string get_id() const {
            const std::string id = config::get(config_local_, "/device/id"_json_pointer, std::string(""));
            return utils::is_placeholder(id) ? "" : id;
        }

        [[nodiscard]] std::string get_name() const {
            const std::string name = config::get(config_local_, "/device/name"_json_pointer, std::string(""));
            return utils::is_placeholder(name) ? "" : name;
        }

        [[nodiscard]] std::string get_credentials() const {
            const std::string credentials = config::get(config_local_, "/device/credentials"_json_pointer, std::string(""));
            return utils::is_placeholder(credentials) ? "" : credentials;
        }

        [[nodiscard]] bool get_ssl() const {
            return config::get(config_local_, "/server/ssl"_json_pointer, true);
        }

        [[nodiscard]] bool get_defaults() const {
            return config::get(config_remote_, "/resources/defaults"_json_pointer, false);
        }

        [[nodiscard]] nlohmann::json get_filesystems() const {
            return config::get(config_remote_, "/resources/filesystems"_json_pointer, nlohmann::json({}));
        }

        [[nodiscard]] nlohmann::json get_drives() const {
            return config::get(config_remote_, "/resources/drives"_json_pointer, nlohmann::json({}));
        }

        [[nodiscard]] nlohmann::json get_interfaces() const {
          return config::get(config_remote_, "/resources/interfaces"_json_pointer, nlohmann::json({}));
        }

        [[nodiscard]] std::string get_svr_host() const {
          return config::get(config_remote_, "/resources/server/host"_json_pointer, std::string("0.0.0.0"));
        }

        [[nodiscard]] unsigned short get_svr_port() const {
          return config::get(config_remote_, "/resources/server/port"_json_pointer, (unsigned short) 7890);
        }

        [[nodiscard]] std::string get_storage() const {
            return config::get(config_remote_, "/backups/storage"_json_pointer, std::string(""));
        }

        [[nodiscard]] std::string get_data_path() const {
            return config::get(config_remote_, "/backups/data_path"_json_pointer, std::string("/data"));
        }

        [[nodiscard]] std::string get_compose_path() const {
            return config::get(config_remote_, "/backups/compose_path"_json_pointer, std::string("/root/"));
        }

        [[nodiscard]] std::string get_bucket(std::string const& st) const {
            auto jp = nlohmann::json::json_pointer("/storage/"+st+"/bucket");
            return config::get(config_remote_, jp, std::string(""));
        }

        [[nodiscard]] std::string get_region(std::string const& st) const {
            auto jp = nlohmann::json::json_pointer("/storage/"+st+"/region");
            return config::get(config_remote_, jp, std::string(""));
        }

        [[nodiscard]] std::string get_access_key(std::string const& st) const {
            auto jp = nlohmann::json::json_pointer("/storage/"+st+"/access_key");
            return config::get(config_remote_, jp, std::string(""));
        }

        [[nodiscard]] std::string get_secret_key(std::string const& st) const {
            auto jp = nlohmann::json::json_pointer("/storage/"+st+"/secret_key");
            return config::get(config_remote_, jp, std::string(""));
        }

        [[nodiscard]] std::string get_backup() const {
            return config::get(config_remote_, "/backups/system"_json_pointer, std::string(""));
        }

        [[nodiscard]] protoson::pson get(std::string const& property) const {

            protoson::pson p;

            // only remote properties in json
            if (std::ranges::find(remote_properties.begin(), remote_properties.end(), property) == remote_properties.end())
                return p;

            auto jp = nlohmann::json::json_pointer("/"+property);

            nlohmann::json j = config::get(config_remote_, jp, nlohmann::json({}));
            protoson::json_decoder::parse(j, p);

            return p;
        }

    private:
        nlohmann::json config_local_ ;
        nlohmann::json config_remote_;

        std::string path_;

        void load_config() {

            std::filesystem::path f(path_);

            if (std::filesystem::exists(f)) {
                std::ifstream config_file(path_);
                config_file >> config_local_;
            }
        }

        void save_config() const {

            if (std::filesystem::path f(path_); !std::filesystem::exists(f)) {
                std::filesystem::create_directories(f.parent_path());
            }

            std::ofstream file(path_);
            file << std::setw(2) << config_local_ << std::endl;
        }

    };
}

#endif //THINGER_MONITOR_CONFIG_HPP
