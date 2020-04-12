#include "PwmSensor.hpp"
#include "TachSensor.hpp"
#include "Utils.hpp"
#include "VariantVisitors.hpp"
#include "PSUSensor.hpp"

#include <array>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
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

const std::regex ILLEGAL_DBUS_PATH_REGEX("[^A-Za-z0-9_.]");
const std::regex ILLEGAL_DBUS_MEMBER_REGEX("[^A-Za-z0-9_]");
constexpr auto PROPERTY_INTERFACE = "org.freedesktop.DBus.Properties";

static constexpr bool DEBUG = false;

static boost::container::flat_map<std::string, std::unique_ptr<PSUSensor>> psuSensors;

void createPSUSensors(
    boost::asio::io_service& io,
    sdbusplus::asio::object_server& objectServer,
    std::shared_ptr<sdbusplus::asio::connection>& dbusConnection)
{
    std::string sensorName = "psu0";
    std::string sensorPathStr = "/etc/sensor";
    std::string objectType = "xyz.openbmc_project.Sensor";
    std::string sensorType = "power";
    std::ofstream ofs(sensorPathStr, std::ios::app);
    ofs << sensorName << "=110\n";
    ofs.close();

    std::vector<thresholds::Threshold> sensorThresholds;
    auto t = thresholds::Threshold(thresholds::Level::CRITICAL,
                                   thresholds::Direction::LOW, 100);
    sensorThresholds.emplace_back(t);
    const std::string interfacePath = "/xyz/openbmc_project/inventory/system/chassis/0";
    psuSensors[sensorName] = std::make_unique<PSUSensor>(
                sensorPathStr, objectType, objectServer, dbusConnection, io,
                sensorName, std::move(sensorThresholds), interfacePath,
                sensorType,
                1, //factor
                120.0, // maxReading
                100.0, // minReading
                "",
                0);
}

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

    //if (!std::filesystem::exists(path)) {
        std::ofstream ofs(path, std::ios::app);
        ofs << sensorName << "=4890\n";
        ofs.close();
    //}

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

void setProperty(sdbusplus::bus::bus& bus,
                const std::string& service,
                const std::string& path,
                 const std::string& interface, const std::string& property,
                 const std::string& value)
{
    sdbusplus::message::variant<std::string> variantValue = value;

    auto method = bus.new_method_call(service.c_str(), path.c_str(),
                                      PROPERTY_INTERFACE, "Set");

    method.append(interface, property, variantValue);
    bus.call_noreply(method);

    return;
}

void setPowerSupplyInfo(sdbusplus::asio::object_server& objServer)
{
    std::string Name = "Delta DPS-750XB A";
    std::string boardType = "PowerSupply";

    std::string boardKey = Name;
    std::string boardKeyOrig = Name;
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
    assetIface->register_property("Manufacturer", std::string("Delta"));
    assetIface->register_property("Model", std::string("DPS-750XB"));
    assetIface->register_property("PartNumber", std::string("P5678"));
    assetIface->register_property("SerialNumber", std::string("S1234"));
    assetIface->initialize();

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

    std::string Name = "Expander 0";
    std::string boardType = "Board"; // or Chassis

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
    auto bus = sdbusplus::bus::new_default();
    std::string service = "xyz.openbmc_project.Inventory.Manager";
    std::string objPath = "/xyz/openbmc_project/inventory/system";
    std::string objIface = "xyz.openbmc_project.Inventory.Decorator.Asset";

    setProperty(bus, service, objPath, objIface, "Model", std::string("DSS2U12"));
    setProperty(bus, service, objPath, objIface, "Manufacturer",
        std::string("Quantum"));
    setProperty(bus, service, objPath, objIface, "SerialNumber",
        std::string("S1234FEDC"));

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
        setPowerSupplyInfo(objectServer);
        createFanSensors(io, objectServer, tachSensors, pwmSensors, systemBus);
        createPSUSensors(io, objectServer, systemBus);
    });


    io.run();
}
