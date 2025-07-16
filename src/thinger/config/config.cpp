#include "config.hpp"

namespace thinger::monitor::utils {

  bool is_placeholder(const std::string& value) {
    return std::regex_match(value, std::regex("(<.*>)"));
  }

  std::string generate_credentials(std::size_t length) {

    const std::string CHARACTERS = "0123456789abcdefghijklmnopqrstuvwxyz!@#$%^&*()ABCDEFGHIJKLMNOPQRSTUVWXYZ";

    std::random_device random_device;
    std::mt19937 generator(random_device());
    std::uniform_int_distribution distribution(0, static_cast<int>(CHARACTERS.size()) - 1);

    std::string random_string;

    for (std::size_t i = 0; i < length; ++i) {
      random_string += CHARACTERS[distribution(generator)];
    }

    return random_string;
  }

}
