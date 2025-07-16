#include <nlohmann/json.hpp>

class ThingerMonitorBackup {

protected:

    ThingerMonitorBackup(thinger::monitor::Config& config, const std::string& name, const std::string& tag)
      : config_(config), name_(name), tag_(tag)
    {}

    thinger::monitor::Config config() const { return config_; }
    std::string name() const { return name_; }
    std::string tag() const { return tag_; }

private:

    thinger::monitor::Config config_;
    std::string name_;
    std::string tag_;

public:

  virtual nlohmann::json backup() = 0;
  virtual nlohmann::json upload() = 0;
  virtual nlohmann::json clean() = 0;

    virtual ~ThingerMonitorBackup() = default;

};
