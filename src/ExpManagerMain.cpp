#include "PwmSensor.hpp"
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
#include <systemd/sd-event.h>

static constexpr bool DEBUG = false;

void createFanSensors(
    boost::asio::io_service& io,
    sdbusplus::asio::object_server& objectServer,
    boost::container::flat_map<std::string, std::unique_ptr<TachSensor>>&
        tachSensors,
    boost::container::flat_map<std::string, std::unique_ptr<PwmSensor>>&
        pwmSensors,
    std::shared_ptr<sdbusplus::asio::connection>& dbusConnection)
{


    const std::string interfacePath = "/xyz/openbmc_project/inventory/system/chassis/0";
    const std::string baseType = "xyz.openbmc_project.Configuration.I2CFan";

    std::string sensorName = "System_Fan01";
    std::string path = "/etc/sensor";

    if (!std::filesystem::exists(path)) {
        std::ofstream ofs(path);
        ofs << sensorName << "=4890\n";
        ofs.close();
    }

    constexpr double defaultMaxReading = 25000;
    constexpr double defaultMinReading = 0;
    auto limits = std::make_pair(defaultMinReading, defaultMaxReading);

    std::vector<thresholds::Threshold> sensorThresholds;
    auto t = thresholds::Threshold(thresholds::Level::CRITICAL,
                                   thresholds::Direction::LOW, 1000);
    sensorThresholds.emplace_back(t);

    std::unique_ptr<PresenceSensor> presenceSensor(nullptr);

    std::optional<RedundancySensor>* redundancy = nullptr;


    tachSensors[sensorName] = std::make_unique<TachSensor>(
                    path, baseType, objectServer, dbusConnection,
                    std::move(presenceSensor), redundancy, io, sensorName,
                    std::move(sensorThresholds), interfacePath, limits);

                // only add new elements
                //const std::string& sysPath = pwm.string();
                //const std::string& pwmName =
                //    "Pwm_" + sysPath.substr(sysPath.find_last_of("pwm") + 1);
                //pwmSensors.insert(
                //    std::pair<std::string, std::unique_ptr<PwmSensor>>(
                //        sysPath, std::make_unique<PwmSensor>(
                //                     pwmName, sysPath, dbusConnection,
                //                     objectServer, *path, "Fan")));
}

int main()
{
    boost::asio::io_service io;
    auto systemBus = std::make_shared<sdbusplus::asio::connection>(io);
    systemBus->request_name("xyz.openbmc_project.ExpManager");
    sdbusplus::asio::object_server objectServer(systemBus);

    boost::container::flat_map<std::string, std::unique_ptr<TachSensor>>
        tachSensors;
    boost::container::flat_map<std::string, std::unique_ptr<PwmSensor>>
        pwmSensors;

    io.post([&]() {
        createFanSensors(io, objectServer, tachSensors, pwmSensors, systemBus);
    });


    io.run();
}
