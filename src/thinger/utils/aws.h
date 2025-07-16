#pragma once

#include <chrono>
#include <filesystem>
#include <spdlog/spdlog.h>

#include <httplib.h>

#include "date.h"
#include "crypto.h"
#include "xml.h"
#include "http_status.h"

#include "aws/s3.h"

#include <nlohmann/json.hpp>

//#define CPPHTTPLIB_SEND_FLAGS 0

namespace AWS {

    bool multipart_upload_to_s3(const std::string& file_path, std::string& bucket, std::string& region, std::string& access_key, std::string& secret_key) {

        auto mpu = S3::MultipartUpload(bucket, region, access_key, secret_key, file_path);

        return mpu.upload();
    }

    bool upload_to_s3(const std::string& file_path, const std::string& bucket, const std::string& region, const std::string& access_key, const std::string& secret_key) {

        const std::string content_type = "application/x-compressed-tar";

        httplib::Client cli("https://"+bucket+".s3-"+region+".amazonaws.com");
        cli.set_write_timeout(120, 0); // 5 seconds

        auto date = Date();
        auto date_string = date.to_rfc5322();
        std::string filename = std::filesystem::path(file_path).filename();

        const std::string signature_string = "PUT\n\n"+content_type+","+content_type+"\n"+date_string+"\n/"+bucket+"/"+filename;
        const std::string signature_hash = Crypto::base64::encode(Crypto::hash::hmac_sha1(secret_key, signature_string));

        httplib::Headers headers = {
            { "Host", bucket+".s3.amazonaws.com" },
            { "Date", date_string}, // Ex: Mon, 03 Jan 2022 12:46:34 +0100
            { "Content-Type", content_type},
            { "Authorization", "AWS "+access_key+":"+signature_hash}
        };

        std::ifstream file(file_path);

        // Without content provider, for big files send by multipart
        std::stringstream body;
        body << file.rdbuf();

        LOG_INFO(fmt::format("[____AWS] Uploading file {0} to {1} bucket", filename, bucket));
        auto res = cli.Put("/"+filename, headers, body.str(), content_type);

        return HttpStatus::isSuccessful(res->status);

    }

    bool download_from_s3(const std::string& file_path, const std::string& bucket, const std::string& region, const std::string& access_key, const std::string& secret_key) {

        const std::string content_type = "application/x-compressed-tar";

        httplib::Client cli("https://"+bucket+".s3-"+region+".amazonaws.com");

        auto date = Date();
        auto date_string = date.to_rfc5322();
        std::string filename = std::filesystem::path(file_path).filename();

        const std::string signature_string = "GET\n\n"+content_type+"\n"+date_string+"\n/"+bucket+"/"+filename;

        const std::string signature_hash = Crypto::base64::encode(Crypto::hash::hmac_sha1(
            const_cast<std::string &>(secret_key), const_cast<std::string &>(signature_string)));

        httplib::Headers headers = {
            { "Host", bucket+".s3.amazonaws.com" },
            { "Date", date_string}, // Ex: Mon, 03 Jan 2022 12:46:34 +0100
            { "Content-Type", content_type},
            { "Authorization", "AWS "+access_key+":"+signature_hash}
        };

        std::ofstream file(file_path);

        LOG_INFO(fmt::format("[____AWS] Downloading file {0} from {1} bucket", filename, bucket));

        cli.set_default_headers(headers);
        auto res = cli.Get("/"+filename,
          [&file](const char *data, size_t data_length) {
            file.write(data, static_cast<int>(data_length));
            file.flush(); // synchronize with underlying storage
            return true;
          });

        return HttpStatus::isSuccessful(res->status);
    }

}

