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

static std::shared_ptr<sdbusplus::asio::dbus_interface>
    createInterface(sdbusplus::asio::object_server& objServer,
                    const std::string& path, const std::string& interface)
{
    auto ptr = objServer.add_interface(path, interface);
    return ptr;
}

void setSystemInfo(sdbusplus::asio::object_server& objServer)
{
    /* configuration sample
    "Name": "WFP Baseboard",
    "Probe": "xyz.openbmc_project.FruDevice({'PRODUCT_PRODUCT_NAME': '.*WFT'})",
    "Type": "Board",
    "xyz.openbmc_project.Inventory.Decorator.Asset": {
        "Manufacturer": "$BOARD_MANUFACTURER",
        "Model": "$BOARD_PRODUCT_NAME",
        "PartNumber": "$BOARD_PART_NUMBER",
        "SerialNumber": "$BOARD_SERIAL_NUMBER"
    },
    "xyz.openbmc_project.Inventory.Decorator.AssetTag": {
        "AssetTag": "$PRODUCT_ASSET_TAG"
    },
    "xyz.openbmc_project.Inventory.Item.Board.Motherboard": {
        "ProductId": 123
    },
    "xyz.openbmc_project.Inventory.Item.System": {}
*/

    std::string Name = "WFP Baseboard";
    std::string boardType = "Board" // or Chassis

    std::string boardKey = Name;// boardPair.value()["Name"];
    std::string boardKeyOrig = Name; //boardPair.value()["Name"];
    std::string boardtypeLower = boost::algorithm::to_lower_copy(boardType);

    std::regex_replace(boardKey.begin(), boardKey.begin(), boardKey.end(),
                           ILLEGAL_DBUS_MEMBER_REGEX, "_");
    std::string boardName = "/xyz/openbmc_project/inventory/system/" +
                                boardtypeLower + "/" + boardKey;

    std::shared_ptr<sdbusplus::asio::dbus_interface> inventoryIface =
            createInterface(objServer, boardName,
                            "xyz.openbmc_project.Inventory.Item");

    std::shared_ptr<sdbusplus::asio::dbus_interface> boardIface =
            createInterface(objServer, boardName,
                            "xyz.openbmc_project.Inventory.Item." + boardType);

    boardIface->register_property("PartNumber", std::string("1234"));
    boardIface->register_property("SerialNumber", std::string("ABCD"));
    boardIface->initialize();

    std::shared_ptr<sdbusplus::asio::dbus_interface> assetIface =
                    createInterface(objServer, boardName, "xyz.openbmc_project.Inventory.Decorator.Asset");
    assetIface->register_property("Manufacturer", std::string("Quantum"));
    assetIface->register_property("Model", std::string("M1234"));
    assetIface->register_property("PartNumber", std::string("P5678"));
    assetIface->register_property("SerialNumber", std::string("SABCE"));
    assetIface->initialize();

    std::shared_ptr<sdbusplus::asio::dbus_interface> atagIface =
                    createInterface(objServer, boardName, "xyz.openbmc_project.Inventory.Decorator.AssetTag");
    atagIface->register_property("AssetTag", std::string("AT001"));
    atagIface->initialize();

    std::shared_ptr<sdbusplus::asio::dbus_interface> sysIface =
                    createInterface(objServer, boardName, "xyz.openbmc_project.Inventory.Item.System");
    sysIface->register_property("System", std::string("DSS2U12"));
    sysIface->initialize();

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
        setSystemInfo(objectServer);
        createFanSensors(io, objectServer, tachSensors, pwmSensors, systemBus);
    });


    io.run();
}
