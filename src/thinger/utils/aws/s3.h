
#include <httplib.h>
#include <spdlog/spdlog.h>

#include <utility>

#include "../date.h"
#include "../crypto.h"
#include "../http_status.h"

class S3 {

public:

    class AWSV4 {
    // Handles the construction of the signature AWS Signature Version 4 (SigV4)

        public:

        AWSV4(std::string  access_key, std::string  secret_key, std::string  region, std::string  service) :
            access_key_(std::move(access_key)),
            secret_key_(std::move(secret_key)),
            region_(std::move(region)),
            service_(std::move(service))
        {}

        std::string get_auth_header(Date date, std::string const& canonical_request) {

            std::string signed_string = sign_string(date, canonical_request);

            std::string auth_header = std::string("AWS4-HMAC-SHA256 ")+
                "Credential="+access_key_+"/"+date.to_iso8601('\0',false,"utc")+"/"+region_+"/"+service_+"/aws4_request,"+
                "SignedHeaders=host;x-amz-content-sha256;x-amz-date,"+
                "Signature="+signed_string;

            return auth_header;
        }

        private:

        std::string access_key_;
        std::string secret_key_;
        std::string region_;
        std::string service_;

        [[nodiscard]] std::string generate_key(Date date) const {

            std::string kDate = Crypto::hash::hmac_sha256(std::string("AWS4"+secret_key_), date.to_iso8601('\0',false,"utc"));
            std::string kRegion = Crypto::hash::hmac_sha256(kDate, region_);
            std::string kService = Crypto::hash::hmac_sha256(kRegion, service_);
            std::string kSigning = Crypto::hash::hmac_sha256(kService, "aws4_request");

            return kSigning;
        }

        std::string generate_string_to_sign(Date& date, std::string const& canonical_request) const {

            std::string string_to_sign =

                std::string("AWS4-HMAC-SHA256\n")+
                date.to_iso8601('\0',true,"utc")+"\n"+
                date.to_iso8601('\0',false,"utc")+"/"+region_+"/s3/aws4_request\n"+
                Crypto::hash::sha256(canonical_request);

            return string_to_sign;
        }

        std::string sign_string(Date date, std::string const& canonical_request) {

            std::string key = generate_key(date);
            std::string string_to_sign = generate_string_to_sign(date, canonical_request);

            return Crypto::to_hex(Crypto::hash::hmac_sha256(key, string_to_sign));

        }

    };

    class MultipartUpload {

        public:

        MultipartUpload(std::string& bucket, std::string& region, std::string& access_key, std::string& secret_key, std::string  file_path) :
            bucket_(bucket),
            region_(region),
            access_key_(access_key),
            secret_key_(secret_key),
            file_path_(std::move(file_path))
        {
            url = bucket+".s3-"+region+".amazonaws.com";
            awsv4 = std::make_unique<AWSV4>(access_key, secret_key, region, "s3");

            // Initialize http client
            cli = std::make_unique<httplib::Client>("https://"+url);
            httplib::Headers headers = {
                { "Host", bucket_+".s3-"+region_+".amazonaws.com" },
            };
            cli->set_default_headers(headers);
            cli->set_write_timeout(30, 0); // 30 seconds
            cli->set_read_timeout(30, 0); // 30 seconds
            cli->set_keep_alive(true);

            // Set class attributes
            // open file and calculate number of parts
            std::ifstream file(file_path_);
            auto file_size = std::filesystem::file_size(file_path_);
            parts = file_size / buffer_size;
            if (file_size % buffer_size != 0) parts = parts+1;

            // TODO: request list of parts to S3 and form XML body to complete from the response
            parts_list = std::unique_ptr<MPUPart[]>{new MPUPart[parts+1]};

            filename = std::filesystem::path(file_path_).filename();

        }

        /**
            Handles the full lifecycle of the multipart upload. Initiate Upload, Upload Part, Complete Upload, Abort Upload, List Parts.
            @return Boolean indicating if the upload operation was successful
        */
        bool upload() {

            if (!initiate_upload()) {
                return false;
            }

            // Upload parts
            if (!upload_parts()) {
                abort_upload();
                return false;
            }

            if (!complete_upload()) {
                abort_upload();
                return false;
            }

            return true;

        }

        ~MultipartUpload() = default;

        private:
        std::string bucket_;
        std::string region_;
        std::string access_key_;
        std::string secret_key_;
        std::string file_path_;
        std::string filename;
        unsigned int parts;

        const std::string content_type = "application/x-compressed-tar";
        const size_t buffer_size = 10<<20; // 10 Megabytes -> with a max of 10.000 parts allows a file of up to 100GiB

        std::string upload_id;

        std::string url;
        std::unique_ptr<AWSV4> awsv4{};
        std::unique_ptr<httplib::Client> cli{};

        struct MPUPart {
            unsigned int part_id{};
            std::string ETag;
        };
        std::unique_ptr<MPUPart[]> parts_list; // TODO: request list of parts to S3 and form XML body to complete from the response

        bool initiate_upload() {

            LOG_INFO(fmt::format("[____AWS] Inititiating upload of {0}", file_path_));

            std::string payload;
            auto res = request("POST", "/"+filename,"uploads=",payload, content_type);

            if ( res.error() != httplib::Error::Success || !HttpStatus::isSuccessful(res->status) ) {
                if ( res.error() == httplib::Error::SSLServerVerification ) {
                    LOG_ERROR(fmt::format("[____AWS] Avoid buckets containing periods (dots) in the name {0}", bucket_));
                }
                LOG_ERROR(fmt::format("[____AWS] Failed initiating upload {0}", filename));
                return HttpStatus::isSuccessful(res->status);
            }

            upload_id = XML::get_element_value(res->body, "UploadId");

            LOG_INFO(fmt::format("[____AWS] Upload initiated with upload id {0}", upload_id));
            return HttpStatus::isSuccessful(res->status);
        }

        bool abort_upload() {

            LOG_WARNING(fmt::format("[____AWS] Aborting multipart upload for file: {0}; and upload id: {1}", file_path_, upload_id));

            std::string payload;
            auto res = request("DELETE", "/"+filename,"uploadId="+upload_id,payload,"text/plain");

            if ( res.error() != httplib::Error::Success || !HttpStatus::isSuccessful(res->status) ) {
                LOG_ERROR(fmt::format("[____AWS] Failed aborting upload id {0}", upload_id));
            }

            return HttpStatus::isSuccessful(res->status);
        }

        bool upload_parts() {
            std::string payload_hash;
            std::string canonical_request;
            std::string auth_header;
            httplib::Headers headers;

            std::vector<char> buffer(buffer_size);

            // Open and calculate part size
            std::ifstream file(file_path_, std::ios::binary);

            for (unsigned int i = 1; i <= parts; i++) { // send part by part

                file.read(buffer.data(), buffer_size);
                std::streamsize bytes_read = file.gcount();

                LOG_INFO(fmt::format("[____AWS] Multipart upload of {0}; Uploading part {1} out of {2}", filename, std::to_string(i), std::to_string(parts)));

                auto res = request("PUT","/"+filename,
                    "partNumber="+std::to_string(i)+"&uploadId="+upload_id,buffer.data(),bytes_read,content_type);

                if ( res.error() != httplib::Error::Success || !HttpStatus::isSuccessful(res->status) ) {
                    LOG_ERROR(fmt::format("[____AWS] Failed Multipart upload {0}; Part {1} out of {2}", filename, std::to_string(i), std::to_string(parts)));
                    return HttpStatus::isSuccessful(res->status);
                }

                parts_list[i-1] = MPUPart{i, res->get_header_value("ETag")};

            }

            file.close();
            return true;

        }

        bool complete_upload() {
            // TODO: complete_upload: request list of parts to S3 and form last request with this response

            std::string complete_xml = generate_xml_complete_mpu();

            LOG_INFO(fmt::format("[____AWS] Finishing upload of {0}", file_path_));

            return request("POST","/"+filename,"uploadId="+upload_id,complete_xml, "text/xml");
        }

        httplib::Result request(
          const std::string& method,
          const std::string& path,
          const std::string& query_parameters,
          std::string const& payload,
          const std::string& c_type)
        {
            return request(method,path,query_parameters,payload.c_str(),payload.size(),c_type);
        }

        httplib::Result request(
          const std::string& method,
          const std::string& path,
          const std::string& query_parameters,
          const char *buffer,
          const size_t buffer_size_req,
          const std::string& c_type)
        {
            std::string payload_hash;
            std::string canonical_request;
            std::string auth_header;
            httplib::Headers headers;
            auto date = Date();

            payload_hash = Crypto::hash::sha256(buffer, buffer_size_req);

            canonical_request = generate_canonical_request(method,path,query_parameters,payload_hash,date);
            auth_header = awsv4->get_auth_header(date, canonical_request);

            headers = {
                { "x-amz-content-sha256", payload_hash },
                { "x-amz-date", date.to_iso8601('\0',true,"utc") },
                { "Authorization", auth_header }
            };

            if (method == "POST") {
                return cli->Post(path+"?"+query_parameters, headers, buffer, buffer_size_req, c_type);
            } else if (method == "PUT") {
                return cli->Put(path+"?"+query_parameters, headers, buffer, buffer_size_req, c_type);
            }

            return httplib::Result{nullptr, httplib::Error::Unknown};;

        }

        [[nodiscard]] std::string generate_canonical_request(
          const std::string& method,
          const std::string& path,
          const std::string& query_parameters,
          std::string const& payload_hash,
          Date date) const
        {

            std::string canonical_request =
                method+"\n"+
                path+"\n"+
                query_parameters+"\n"+
                "host:"+url+"\n"+
                "x-amz-content-sha256:"+payload_hash+"\n"+
                "x-amz-date:"+date.to_iso8601('\0',true,"utc")+"\n\n"+
                "host;x-amz-content-sha256;x-amz-date\n"+
                payload_hash;

            return canonical_request;
        }

        [[nodiscard]] std::string generate_xml_complete_mpu() const {

            std::string xml_res = "<CompleteMultipartUpload>\n";
            for (int i=0; i < parts; i++) {
                xml_res += "<Part>\n<PartNumber>"+std::to_string(parts_list[i].part_id)+"</PartNumber>\n";
                xml_res += "<ETag>"+parts_list[i].ETag+"</ETag>\n</Part>";
            }
            xml_res += "\n</CompleteMultipartUpload>";

            return xml_res;
        }

    };

};
