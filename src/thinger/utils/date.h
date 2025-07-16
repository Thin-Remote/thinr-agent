#pragma once

#include <chrono>
#include <ctime>   // localtime
#include <sstream> // stringstream
#include <iomanip> // put_time
#include <algorithm>

class Date {

public:

    // Instantiates a new Date object with the current time and date
    Date() {

        auto now = std::chrono::system_clock::now();
        date = std::chrono::system_clock::to_time_t(now);
    }

    std::string to_iso8601(const char del = '-', const bool extended = false, const std::string_view& tmz = "local") {

        std::string format =
            std::string("%Y").append(std::string(1,del))+
            std::string("%m").append(std::string(1,del))+
            std::string("%d");

        std::stringstream ss;
        if (extended)
            format =
                std::string("%Y").append(std::string(1,del))+
                std::string("%m").append(std::string(1,del))+
                "%dT%H%M%SZ";

        std::erase(format, '\0'); //remove '\0' from string

        if (tmz == "utc" || tmz == "gmt") {
            gmtime_r(&date, &time); // Compliant
            ss << std::put_time(&time, format.c_str());
        } else { // (timezone == "local")
            localtime_r(&date, &time); // Compliant
            ss << std::put_time(&time, format.c_str());
        }

        return ss.str();
    }

    std::string to_rfc5322() {

        std::stringstream ss;
        localtime_r(&date, &time); // Compliant
        ss << std::put_time(&time, "%a, %d %b %Y %T %z");

        return ss.str();

    }

private:
    time_t date;
    struct tm time{};

};
