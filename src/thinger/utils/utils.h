
#ifndef THINGER_MONITOR_UTILS_H
#define THINGER_MONITOR_UTILS_H

#include <string>
#include <vector>
#include <sstream>

#define STR_(x) #x
#define STR(x) STR_(x)

#define VERSION STR(BUILD_VERSION) // BUILD_VERSION must always be defined

namespace utils::version {

  bool is_current_version_newer(std::string_view version) {

    std::vector<int> v1, v2;
    std::stringstream ss1(VERSION), ss2(version.data());
    std::string token;

    // Parse version 1 and store components in v1
    while (std::getline(ss1, token, '.')) {
      try {
        v1.push_back(std::stoi(token));
      } catch ( std::invalid_argument& e ) {
        LOG_WARNING(fmt::format("[_UTILS] Build version ({0}) does not follow semver. Assuming version is newer than {1}", VERSION, version));
        return true;
      }
    }

    // Parse version 2 and store components in v2
    while (std::getline(ss2, token, '.')) {
      v2.push_back(std::stoi(token));
    }

    // Compare each component
    for (size_t i = 0; i < v1.size() && i < v2.size(); ++i) {
      if (v1[i] < v2[i]) {
        return false; // Version 1 is older in this component
      }
      else if (v1[i] > v2[i]) {
        return true; // Version 1 is newer in this component
      }
    }

    // If we reached this point, one version has extra components
    // If version 1 has more components, it is newer
    return v1.size() > v2.size();

  }

}

#endif //THINGER_MONITOR_UTILS_H
