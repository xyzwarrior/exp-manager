cmake_minimum_required (VERSION 2.8.10 FATAL_ERROR)
set (BUILD_SHARED_LIBRARIES OFF)
include (ExternalProject)
set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
set (
    CMAKE_CXX_FLAGS
    "${CMAKE_CXX_FLAGS} -lstdc++fs \
    -Werror \
    -Wall \
    -Wextra \
    -Wnon-virtual-dtor \
    -Wold-style-cast \
    -Wcast-align \
    -Wunused \
    -Woverloaded-virtual \
    -Wpedantic \
    -Wmisleading-indentation \
    -Wduplicated-cond \
    -Wduplicated-branches \
    -Wlogical-op \
    -Wnull-dereference \
    -Wuseless-cast \
    -Wdouble-promotion \
    -Wformat=2 \
    -Wno-sign-compare \
    -Wno-reorder \
"
)
# todo: get rid of nos, add the below:
#  -Wshadow \
#  -Wconversion \

set (CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake ${CMAKE_MODULE_PATH})

option (YOCTO "Enable Building in Yocto" OFF)
option (HUNTER_ENABLED "Enable hunter package pulling" OFF)

include ("cmake/HunterGate.cmake")

huntergate (URL "https://github.com/ruslo/hunter/archive/v0.18.64.tar.gz" SHA1
            "baf9c8cc4f65306f0e442b5419967b4c4c04589a")

project (expmanager CXX)

set (EXTERNAL_PACKAGES Boost sdbusplus-project nlohmann-json)
set (SENSOR_LINK_LIBS -lsystemd stdc++fs sdbusplus)

if (NOT YOCTO)
    set (DISABLE_NVME ON) # todo allow this to build out of tree
    option (ENABLE_TEST "Enable Google Test" OFF)

    externalproject_add (
        Boost URL
        https://dl.bintray.com/boostorg/release/1.66.0/source/boost_1_66_0.tar.gz
        URL_MD5 d275cd85b00022313c171f602db59fc5 SOURCE_DIR
        "${CMAKE_BINARY_DIR}/boost-src" BINARY_DIR
        "${CMAKE_BINARY_DIR}/boost-build" CONFIGURE_COMMAND "" BUILD_COMMAND ""
        INSTALL_COMMAND ""
    )
    include_directories (SYSTEM ${CMAKE_BINARY_DIR}/boost-src)
    set (CMAKE_PREFIX_PATH ${CMAKE_BINARY_DIR}/boost-src ${CMAKE_PREFIX_PATH})

    # requires apt install autoconf-archive and autoconf
    externalproject_add (sdbusplus-project PREFIX
                         ${CMAKE_BINARY_DIR}/sdbusplus-project GIT_REPOSITORY
                         https://github.com/openbmc/sdbusplus.git SOURCE_DIR
                         ${CMAKE_BINARY_DIR}/sdbusplus-src BINARY_DIR
                         ${CMAKE_BINARY_DIR}/sdbusplus-build CONFIGURE_COMMAND
                         "" BUILD_COMMAND cd ${CMAKE_BINARY_DIR}/sdbusplus-src
                         && ./bootstrap.sh && ./configure --enable-transaction
                         && make -j libsdbusplus.la INSTALL_COMMAND ""
                         LOG_DOWNLOAD ON)
    include_directories (SYSTEM ${CMAKE_BINARY_DIR}/sdbusplus-src)
    link_directories (${CMAKE_BINARY_DIR}/sdbusplus-src/.libs)

    externalproject_add (nlohmann-json PREFIX
                         ${CMAKE_CURRENT_BINARY_DIR}/nlohmann-json
                         GIT_REPOSITORY https://github.com/nlohmann/json.git
                         SOURCE_DIR ${CMAKE_BINARY_DIR}/nlohmann-json-src
                         BINARY_DIR ${CMAKE_BINARY_DIR}/nlohmann-json-build
                         CONFIGURE_COMMAND "" BUILD_COMMAND "" INSTALL_COMMAND
                         "" LOG_DOWNLOAD ON)
    include_directories (SYSTEM ${CMAKE_BINARY_DIR}/nlohmann-json-src/include)

    if (ENABLE_TEST)
        option (HUNTER_ENABLED "Enable hunter package pulling" ON)
        hunter_add_package (GTest)

        find_package (GTest CONFIG REQUIRED)

        enable_testing ()

        add_executable (runTachTests tests/test_TachSensor.cpp ${FAN_SRC_FILES})
        add_test (NAME test_fansensor COMMAND runTachTests)
        target_link_libraries (runTachTests GTest::main GTest::gtest pthread
                               ${DBUS_LIBRARIES} stdc++fs)
        add_dependencies (runTachTests nlohmann-json)

        add_executable (runHwmonTempTests tests/test_HwmonTempSensor.cpp
                        ${HWMON_TEMP_SRC_FILES})
        add_test (NAME test_hwmontempsensor COMMAND runHwmonTempTests)
        target_link_libraries (runHwmonTempTests GTest::main GTest::gtest
                               pthread ${DBUS_LIBRARIES} stdc++fs)
        add_dependencies (runHwmonTempTests nlohmann-json)
    endif ()

endif ()

add_definitions (-DBOOST_ERROR_CODE_HEADER_ONLY)
add_definitions (-DBOOST_SYSTEM_NO_DEPRECATED)
add_definitions (-DBOOST_ALL_NO_LIB)
add_definitions (-DBOOST_NO_RTTI)
add_definitions (-DBOOST_NO_TYPEID)
add_definitions (-DBOOST_ASIO_DISABLE_THREADS)

link_directories (${EXTERNAL_INSTALL_LOCATION}/lib)

# phosphor-dbus-interfaces
find_package(PkgConfig REQUIRED)
pkg_check_modules(DBUSINTERFACE phosphor-dbus-interfaces REQUIRED)
include_directories(${DBUSINTERFACE_INCLUDE_DIRS})
link_directories(${DBUSINTERFACE_LIBRARY_DIRS})

include_directories (${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable (expmanager
	src/ExpManagerMain.cpp
    src/TachSensor.cpp
    src/PwmSensor.cpp
    src/Utils.cpp
    src/Thresholds.cpp
)

add_dependencies (expmanager sdbusplus-project)
target_link_libraries (expmanager ${SENSOR_LINK_LIBS})
target_link_libraries (expmanager gpiodcxx)
target_link_libraries (expmanager ${PHOSPHOR_DBUS_})
target_link_libraries (expmanager i2c)

if (NOT YOCTO)
    add_dependencies (expmanager ${EXTERNAL_PACKAGES})
endif ()

set (SERVICE_FILE_SRC_DIR ${PROJECT_SOURCE_DIR}/service_files)
set (SERVICE_FILE_INSTALL_DIR /lib/systemd/system/)

install (TARGETS expmanager DESTINATION bin)
install (FILES
                 ${SERVICE_FILE_SRC_DIR}/xyz.openbmc_project.expmanager.service
                 DESTINATION ${SERVICE_FILE_INSTALL_DIR})

