#ifndef THINGER_MONITOR_CRYPTO_H
#define THINGER_MONITOR_CRYPTO_H
//#pragma once

#include <sstream>
#include <iomanip>
#include <memory>
#include <cstring>

// base64
#include <string>
#include <cassert>
#include <limits>
#include <stdexcept>
#include <cctype>

// hash
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/params.h>
#include <openssl/err.h>
#include <openssl/core_names.h>

namespace Crypto {

    std::string to_hex(const std::string &string) {

        auto len = string.length();
        auto hash = (const unsigned char*)string.c_str();

        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (int i = 0; i < len; i++) {
            ss << std::hex << std::setw(2)  << (unsigned int)hash[i];
        }

        return ss.str();
    }

    namespace base64 {
        /*
         * Code from https://stackoverflow.com/a/5291537/10268962 without modification
         * Protected by CC BY-SA 3.0 (https://creativecommons.org/licenses/by-sa/3.0/)
         */

        const char b64_table[65] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        const char reverse_table[128] = {
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,
            52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
            64,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
            15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,
            64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
            41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64
        };

        ::std::string encode(const ::std::string_view& bindata)
        {
            using ::std::string;
            using ::std::numeric_limits;

            if (bindata.size() > (numeric_limits<string::size_type>::max() / 4u) * 3u) {
                throw ::std::length_error("Converting too large a string to base64.");
            }

            const ::std::size_t binlen = bindata.size();
            // Use = signs so the end is properly padded.
            string retval((((binlen + 2) / 3) * 4), '=');
            ::std::size_t outpos = 0;
            int bits_collected = 0;
            unsigned int accumulator = 0;
            const auto binend = bindata.end();

            for (auto i = bindata.begin(); i != binend; ++i) {
                accumulator = (accumulator << 8) | (*i & 0xffu);
                bits_collected += 8;
                while (bits_collected >= 6) {
                    bits_collected -= 6;
                    retval[outpos] = b64_table[(accumulator >> bits_collected) & 0x3fu];
                    outpos++;
                }
            }
            if (bits_collected > 0) { // Any trailing bits that are missing.
                assert(bits_collected < 6);
                accumulator <<= 6 - bits_collected;
                retval[outpos] = b64_table[accumulator & 0x3fu];
                outpos++;
            }
            assert(outpos >= (retval.size() - 2));
            assert(outpos <= retval.size());
            return retval;
        }

        static ::std::string decode(const ::std::string_view& ascdata)
        {
            using ::std::string;
            string retval;
            const auto last = ascdata.end();
            int bits_collected = 0;
            unsigned int accumulator = 0;

            for (auto i = ascdata.begin(); i != last; ++i) {
                const int c = *i;
                if (::std::isspace(c) || c == '=') {
                    // Skip whitespace and padding. Be liberal in what you accept.
                    continue;
                }
                if ((c > 127) || (c < 0) || (reverse_table[c] > 63)) {
                    throw ::std::invalid_argument("This contains characters not legal in a base64 encoded string.");
                }
                accumulator = (accumulator << 6) | reverse_table[c];
                bits_collected += 6;
                if (bits_collected >= 8) {
                    bits_collected -= 8;
                    retval += static_cast<char>((accumulator >> bits_collected) & 0xffu);
                }
            }
            return retval;
        }
    };

    namespace hash {

        std::string hmac(const std::string& key, const std::string& msg, const std::string& digest) {

            EVP_MAC *mac = EVP_MAC_fetch(nullptr, "HMAC", nullptr);
            EVP_MAC_CTX *ctx = nullptr;

            unsigned char hash[64];
            size_t final_l;

            OSSL_PARAM params[2];
            size_t params_n = 0;

            auto digest_c = std::make_unique<char[]>(digest.size() + 1);
            memcpy(digest_c.get(), digest.c_str(), digest.size() + 1);
            params[params_n++] = OSSL_PARAM_construct_utf8_string("digest", digest_c.get(), 0);
            params[params_n] = OSSL_PARAM_construct_end();

            ctx = EVP_MAC_CTX_new(mac);
            EVP_MAC_init(ctx, (const unsigned char *)&key[0], key.length(), params);

            EVP_MAC_update(ctx, (unsigned char*)&msg[0], msg.length());

            EVP_MAC_final(ctx, hash, &final_l, sizeof(hash));

            EVP_MAC_CTX_free(ctx);
            EVP_MAC_free(mac);

            std::stringstream ss;
            ss << std::setfill('0');
            for (int i = 0; i < final_l; i++) {
              ss  << hash[i];
            }

            return ss.str();
          }

        std::string hmac_sha1(const std::string& key, const std::string& msg) {

            return hmac(key, msg, "SHA1");

        }

        std::string hmac_sha256(std::basic_string<char> key, std::basic_string<char> msg) {

            return hmac(key, msg, "SHA256");

        }


        std::string sha256(const char* str, const size_t length) {

            EVP_MD *sha256 = nullptr;
            EVP_MD_CTX *ctx = nullptr;

            unsigned char *hash = nullptr;
            unsigned int hash_length = 0;

            ctx = EVP_MD_CTX_new();
            sha256 = EVP_MD_fetch(nullptr, "SHA256", nullptr);
            EVP_DigestInit_ex(ctx, sha256, nullptr);
            EVP_DigestUpdate(ctx, str, length);
            hash = (unsigned char *) OPENSSL_malloc(EVP_MD_get_size(sha256));
            EVP_DigestFinal_ex(ctx, hash, &hash_length);
            EVP_MD_free(sha256);

            std::stringstream ss;
            for(int i = 0; i < hash_length; i++)
            {
                ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
            }
            return ss.str();
        }

        std::string sha256(const std::string& str) {
            return sha256(str.c_str(), str.length());
        }
    };

};

#endif // THINGER_MONITOR_CRYPTO_H