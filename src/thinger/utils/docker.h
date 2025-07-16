#pragma once

#include <filesystem>
#include <vector>
#include <httplib.h>
#include <spdlog/spdlog.h>

#include "http_status.h"

#include <nlohmann/json.hpp>

// Support from API 1.41
namespace Docker {

    namespace Container {

        bool inspect(const std::string& container_id, const std::string& dest_path) {

            LOG_INFO(fmt::format("[_DOCKER] Inspecting container: '{0}' and saving result to: '{1}'", container_id, dest_path+"/"+container_id+".json"));

            httplib::Client cli("/var/run/docker.sock");
            cli.set_address_family(AF_UNIX);
            cli.set_default_headers({ { "Host", "localhost" } });

            std::ofstream file(dest_path+"/"+container_id+".json");

            auto res = cli.Get(("/containers/"+container_id+"/json").c_str());

            if ( res.error() != httplib::Error::Success ) {
                LOG_ERROR(fmt::format("[_DOCKER] Request error: {0}", to_string(res.error())));
                return false;
            }

            auto res_json = nlohmann::json::parse(res->body);
            file << std::setw(4) << res_json << std::endl;
            if ( ! file ) {
                LOG_ERROR("[_DOCKER] There was an error saving result to filesystem");
                return false;
            }

            if (res->status == 200)
                LOG_INFO("[_DOCKER] Succesfully retrieved information");
            else {
                LOG_WARNING(fmt::format("[_DOCKER] Could not retrieve information. Status Code: {0}", res->status));

                LOG_LEVEL(1, res->body);
                if ( res.error() != httplib::Error::Success )
                    LOG_LEVEL(1, to_string(res.error()));
            }

            return HttpStatus::isSuccessful(res->status);
        }

        // Used only for plugins
        bool create_from_inspect(const std::string& source_path, const std::string& network_id = "") {

            nlohmann::json inspect_json;

            LOG_INFO(fmt::format("[_DOCKER] Reading docker inspect json from: '{0}'", source_path));

            std::filesystem::path file(source_path);
            if (std::filesystem::exists(file)) {
                std::ifstream inspect_file(source_path);
                inspect_file >> inspect_json;
            }

            httplib::Client cli("/var/run/docker.sock");
            cli.set_address_family(AF_UNIX);
            cli.set_default_headers({ { "Host", "localhost" } });
            cli.set_read_timeout(600, 0); // 10 minutes for commands to execute

            // TODO: move to its own function
            LOG_INFO(fmt::format("[_DOCKER] Downloading image: '{0}'", inspect_json["Config"]["Image"].get<std::string>()));
            auto res = cli.Post(("/images/create?fromImage="+inspect_json["Config"]["Image"].get<std::string>()).c_str());
            if ( res.error() == httplib::Error::Read ) {
                LOG_ERROR("[_DOCKER] Error: Timeout waiting for image to download");
                return false;
            } else if ( res.error() != httplib::Error::Success ) {
                LOG_ERROR(fmt::format("[_DOCKER] Request error: {0}", to_string(res.error())));
                return false;
            }

            if (res->status == 200)
                LOG_INFO("[_DOCKER] Image downloaded succesfully");
            else {
                LOG_ERROR("[_DOCKER] An error occurred while downloading the image");
                LOG_LEVEL(1, "[_DOCKER] Error description: %s", res->body);
            }

            nlohmann::json body = inspect_json["Config"];
            body["HostConfig"] = inspect_json["HostConfig"];
            body["NetworkingConfig"]["EndpointsConfig"] = inspect_json["NetworkSettings"]["Networks"];
            if (network_id != "") {
                for (auto& el : body["NetworkingConfig"]["EndpointsConfig"].items()) { // Only one network is expected
                    body["NetworkingConfig"]["EndpointsConfig"][el.key()]["NetworkId"] = network_id;
                }
            }

            LOG_INFO(fmt::format("[_DOCKER] Creating container: '{0}'", inspect_json["Name"].get<std::string>()));

            res = cli.Post(("/containers/create?name="+inspect_json["Name"].get<std::string>()).c_str(),
              body.dump(), "application/json");

            if ( res.error() == httplib::Error::Read ) {
                LOG_ERROR("[_DOCKER] Error: Timeout waiting for command to execute");
                return false;
            } else if ( res.error() != httplib::Error::Success ) {
                LOG_ERROR(fmt::format("[_DOCKER] Request error: {0}", to_string(res.error())));
                return false;
            }

            if (res->status == 201)
                LOG_INFO("[_DOCKER] Succesfully executed");
            else {
                LOG_ERROR("[_DOCKER] An error occurred while executing");
                LOG_LEVEL(1, "[_DOCKER] Error description: %s", res->body);
            }

            return HttpStatus::isSuccessful(res->status);
        }

        bool exec(const std::string container_id, const std::string command) {

            std::string exec_id = "";

            // POST request: https://docs.docker.com/engine/api/v1.41/#operation/ContainerExec
            // To exec a command in a container, you first need to create an exec instance, then start it.

            LOG_INFO(fmt::format("[_DOCKER] Executing command: '{0}' in container '{1}'", command, container_id));

            // Create exec instance
            httplib::Client cli("/var/run/docker.sock");
            cli.set_address_family(AF_UNIX);
            cli.set_default_headers({ { "Host", "localhost" } });
            cli.set_read_timeout(600, 0); // 10 minutes for commands to execute

            std::stringstream ss(command);

            std::vector<std::string> cmd;
            while(ss.good()) {
                std::string word;
                ss >> word;
                cmd.push_back(word);
            }

            nlohmann::json body = {
              {"AttachStdin", false},
              {"AttachStdout", false},
              {"AttachStderr", true}, // will keep the request blocked until it finishes. Not ideal
              {"Tty", false},
              {"Cmd", cmd}
            };

            auto res = cli.Post(("/containers/"+container_id+"/exec").c_str(), body.dump(), "application/json");

            auto res_json = nlohmann::json::parse(res->body);

            exec_id = res_json["Id"].get<std::string>();

            // Start exec instance
            body = {
              {"Detach", false}, // Does not seem to work!! block until end of command
              {"Tty", false}
            };

            res = cli.Post(("/exec/"+exec_id+"/start").c_str(), body.dump(), "application/json");

            if ( res.error() == httplib::Error::Read ) {
                LOG_ERROR("[_DOCKER] Error: Timeout waiting for command to execute");
                return false;
            } else if ( res.error() != httplib::Error::Success ) {
                LOG_ERROR(fmt::format("[_DOCKER] Request error: {0}", to_string(res.error())));
                return false;
            }

            if (!HttpStatus::isSuccessful(res->status)) {
                LOG_ERROR("[_DOCKER] An error occurred while executing");
                return false;
            }

            // Inspect exec instance
            res = cli.Get(("/exec/"+exec_id+"/json").c_str());

            res_json = nlohmann::json::parse(res->body);

            if (!HttpStatus::isSuccessful(res->status) || res_json["ExitCode"].get<int>() != 0) {
                LOG_ERROR("[_DOCKER] An error occurred while executing");
                return false;
            }

            LOG_INFO("[_DOCKER] Succesfully executed");

            return true;
        }

        bool restart(const std::string container_id) {

            LOG_INFO(fmt::format("[_DOCKER] Restarting container: '{0}'", container_id));

            httplib::Client cli("/var/run/docker.sock");
            cli.set_address_family(AF_UNIX);
            cli.set_default_headers({ { "Host", "localhost" } });
            cli.set_read_timeout(600, 0); // 10 minutes for commands to execute

            auto res = cli.Post(("/containers/"+container_id+"/restart").c_str(), "t=0", "application/x-www-form-urlencoded");

            if ( res.error() != httplib::Error::Success ) {
                LOG_ERROR(fmt::format("[_DOCKER] Request error: {0}", to_string(res.error())));
                return false;
            }

            if (res->status == 204)
                LOG_INFO("[_DOCKER] Succesfully restarted");
            else
                LOG_WARNING("[_DOCKER] Could not be restarted");

            return HttpStatus::isSuccessful(res->status);
        }

        bool start(const std::string container_id) {

            LOG_INFO(fmt::format("[_DOCKER] Starting container: '{0}'", container_id));

            httplib::Client cli("/var/run/docker.sock");
            cli.set_address_family(AF_UNIX);
            cli.set_default_headers({ { "Host", "localhost" } });
            cli.set_read_timeout(600, 0); // 10 minutes for commands to execute

            auto res = cli.Post(("/containers/"+container_id+"/start").c_str(), "t=0", "application/x-www-form-urlencoded");

            if ( res.error() != httplib::Error::Success ) {
                LOG_ERROR(fmt::format("[_DOCKER] Request error: {0}", to_string(res.error())));
                return false;
            }

            if (res->status == 204)
                LOG_INFO("[_DOCKER] Succesfully restarted");
            else
                LOG_WARNING("[_DOCKER] Could not be restarted");

            return HttpStatus::isSuccessful(res->status);
        }

        bool stop(const std::string container_id) {

            LOG_INFO(fmt::format("[_DOCKER] Stopping container: '{0}'", container_id));

            httplib::Client cli("/var/run/docker.sock");
            cli.set_address_family(AF_UNIX);
            cli.set_default_headers({ { "Host", "localhost" } });
            cli.set_read_timeout(600, 0); // 10 minutes for commands to execute

            auto res = cli.Post(("/containers/"+container_id+"/stop").c_str(), "t=0", "application/x-www-form-urlencoded");

            if ( res.error() != httplib::Error::Success ) {
                LOG_ERROR(fmt::format("[_DOCKER] Request error: {0}", to_string(res.error())));
                return false;
            }

            if (res->status == 204 || res->status == 304)
                LOG_INFO("[_DOCKER] Succesfully stopped");
            else
                LOG_WARNING("[_DOCKER] Could not be stopped");

            return HttpStatus::isSuccessful(res->status);
        }

        // It seems that the path has to be relative to the access point
        bool copy_from_container(const std::string container_id, const std::string source_path, const std::string dest_path) {

            LOG_INFO(fmt::format("[_DOCKER] Copying from container: '{0}' path: '{1}' to host path: '{2}'", container_id, source_path, dest_path));

            // https://docs.docker.com/engine/api/v1.41/#operation/ContainerArchive
            httplib::Client cli("/var/run/docker.sock");
            cli.set_address_family(AF_UNIX);
            cli.set_default_headers({ { "Host", "localhost" } });

            std::ofstream file(dest_path); // must have tar extension

            auto res = cli.Get(("/containers/"+container_id+"/archive?path="+source_path).c_str(),
              [&](const char *data, size_t data_length) {
                file.write(data, data_length);
                return true;
              });

            if ( res.error() != httplib::Error::Success ) {
                LOG_ERROR(fmt::format("[_DOCKER] Request error: {0}", to_string(res.error())));
                return false;
            }

            if (res->status == 200)
                LOG_INFO("[_DOCKER] Succesfully copied");
            else
                LOG_WARNING("[_DOCKER] Could not be copied");

            return HttpStatus::isSuccessful(res->status);
        }

        bool copy_to_container(const std::string container_id, const std::string source_path, const std::string dest_path) {

            LOG_INFO(fmt::format("[_DOCKER] Copying from host path: '{0}' to container '{1}' path: '{2}'", source_path, container_id, dest_path));

            // https://docs.docker.com/engine/api/v1.41/#operation/PutContainerArchive
            httplib::Client cli("/var/run/docker.sock");
            cli.set_address_family(AF_UNIX);
            cli.set_write_timeout(600, 0); // 10 minutes
            cli.set_default_headers({ { "Host", "localhost" } });

            size_t buffer_size = 10<<20; // 10 Megabyte
            char *buffer = new char[buffer_size];
            std::ifstream file(source_path);

            /* Without content provider
            std::stringstream body;
            body << file.rdbuf();

            auto res = cli.Put(("/containers/"+container_id+"/archive?path="+dest_path).c_str(),
              body.str(), "application/octec-stream");

            */

            unsigned int file_size = std::filesystem::file_size(source_path);
            auto res = cli.Put(("/containers/"+container_id+"/archive?path="+dest_path).c_str(),
              std::filesystem::file_size(source_path),
              [&](size_t offset, size_t length, httplib::DataSink &sink) {

                  file.read(buffer, buffer_size);
                  size_t count = file.gcount();
                  if (!count) {
                      return false;
                  }
                  //sink.write(buffer+offset, length);
                  sink.write(buffer, std::min(length, count*sizeof(char)));
                  return true;
              },
              "application/octet-stream"
            );

            if ( res.error() != httplib::Error::Success ) {
                LOG_ERROR(fmt::format("[_DOCKER] Request error: {0}", to_string(res.error())));
                return false;
            }

            if (res->status == 200)
                LOG_INFO("[_DOCKER] Succesfully copied");
            else
                LOG_WARNING("[_DOCKER] Could not be copied");

            return HttpStatus::isSuccessful(res->status);
        }
    }

    namespace Network { // TODO: could this be done with abstract classes?

        bool inspect(const std::string& network_id, const std::string& dest_path) {

            LOG_INFO(fmt::format("[_DOCKER] Inspecting network: '{0}' and saving result to: '{1}'", network_id, dest_path+"/"+network_id+"-network.json"));

            httplib::Client cli("/var/run/docker.sock");
            cli.set_address_family(AF_UNIX);
            cli.set_default_headers({ { "Host", "localhost" } });

            std::ofstream file(dest_path+"/"+network_id+"-network.json");

            auto res = cli.Get(("/networks/"+network_id).c_str());

            if ( res.error() != httplib::Error::Success ) {
                LOG_ERROR(fmt::format("[_DOCKER] Request error: {0}", to_string(res.error())));
                return false;
            }

            auto res_json = nlohmann::json::parse(res->body);
            file << std::setw(4) << res_json << std::endl;
            if ( ! file ) {
                LOG_ERROR("[_DOCKER] There was an error saving result to filesystem");
                return false;
            }

            if (res->status == 200)
                LOG_INFO("[_DOCKER] Succesfully retrieved information");
            else {
                LOG_WARNING(fmt::format("[_DOCKER] Could not retrieve information. Status Code: {0}", res->status));

                LOG_LEVEL(1, res->body);
                if ( res.error() != httplib::Error::Success )
                    LOG_LEVEL(1, to_string(res.error()));
            }

            return HttpStatus::isSuccessful(res->status);
        }

        std::string create_from_inspect(const std::string source_path) {

            nlohmann::json inspect_json;

            spdlog::info("[_DOCKER] Reading network inspect json from: '{0}'", source_path);

            std::filesystem::path file(source_path);
            if (std::filesystem::exists(file)) {
                std::ifstream inspect_file(source_path);
                inspect_file >> inspect_json;
            }

            // Clean json
            inspect_json.erase("Id");
            inspect_json.erase("Created");
            inspect_json.erase("Scope");
            inspect_json.erase("Scope");
            inspect_json.erase("Containers");
            inspect_json.erase("IPAM");

            spdlog::info("[_DOCKER] Creating network: '{0}'", inspect_json["Name"].get<std::string>());

            httplib::Client cli("/var/run/docker.sock");
            cli.set_address_family(AF_UNIX);
            cli.set_default_headers({ { "Host", "localhost" } });
            cli.set_read_timeout(600, 0); // 10 minutes for commands to execute

            auto res = cli.Post("/networks/create", inspect_json.dump(), "application/json");

            if ( res.error() == httplib::Error::Read ) {
                LOG_ERROR("[_DOCKER] Error: Timeout waiting for command to execute");
                return "";
            } else if ( res.error() != httplib::Error::Success ) {
                LOG_ERROR(fmt::format("[_DOCKER] Request error: {0}", to_string(res.error())));
                return "";
            }

            if (res->status == 201)
                spdlog::info("[_DOCKER] Succesfully executed");
            else {
                LOG_ERROR(fmt::format("[_DOCKER] An error occurred while executing"));
                //LOG_LEVEL(1, "[_DOCKER] Error description: {0}", res->body);
                LOG_LEVEL(1, "[_DOCKER] Error description: %s", res->body);
            }

            auto res_json = nlohmann::json::parse(res->body);

            return res_json["Id"].get<std::string>();
        }
    }

};
