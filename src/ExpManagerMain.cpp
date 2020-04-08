#include "TachSensor.hpp"
#include "Utils.hpp"
#include "VariantVisitors.hpp"

#include <array>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/container/flat_set.hpp>
#include <boost/lexical_cast.hpp>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <regex>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>
#include <sdbusplus/bus/match.hpp>
#include <string>
#include <utility>
#include <variant>
#include <vector>

static constexpr bool DEBUG = false;

int main()
{
    boost::asio::io_service io;
    auto systemBus = std::make_shared<sdbusplus::asio::connection>(io);
    systemBus->request_name("xyz.openbmc_project.ExpManager");
    sdbusplus::asio::object_server objectServer(systemBus);

    std::string sensorName = "System_Fan01";
    std::string path = "/etc/sensor";

    if (!std::filesystem::fileexists(path)) {
        std::ofstream ofs(path);
        ofs << sensorName << "=4890\n";
        ofs.close();
    }

    constexpr double defaultMaxReading = 25000;
    constexpr double defaultMinReading = 0;
    auto limits = std::make_pair(defaultMinReading, defaultMaxReading);

    std::vector<thresholds::Threshold> sensor_thresholds;
    auto t = thresholds::Threshold(thresholds::Level::CRITICAL,
                                   thresholds::Direction::LOW, 1000);
    sensor_thresholds.emplace_back(t);
    std::unique_ptr<PresenceSensor> presenceSensor(nullptr);
    std::optional<RedundancySensor>* redundancy = nullptr;
    std::string interfacePath = "/xyz/openbmc_project/inventory/system/chassis/0";
    tachSensors[sensorName] = std::make_unique<TachSensor>(
            path,
            "xyz.openbmc_project.Configuration.I2CFan",
            objectServer, systemBus,
            std::move(presenceSensor),
            redundancy, io, sensorName,
            std::move(sensor_thresholds),
            interfacePath, limits);

    io.run();
}
