#pragma once

#ifndef THINR_SERVICE_INSTALLER_FACTORY_HPP
#define THINR_SERVICE_INSTALLER_FACTORY_HPP

#include <memory>
#include <string>
#include "base_service_installer.hpp"

namespace thinr::installer {

class service_installer_factory {
public:
    static std::unique_ptr<base_service_installer> create();

private:
    static std::string detect_init_system();
};

} // namespace thinr::installer

#endif // THINR_SERVICE_INSTALLER_FACTORY_HPP