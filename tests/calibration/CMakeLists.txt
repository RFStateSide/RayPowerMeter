cmake_minimum_required(VERSION 3.16)
project(RayPowerMeterCalibrationTests LANGUAGES CXX)

set(ARDUINOJSON_SRC "" CACHE PATH "Path to the ArduinoJson 7.4.2 src directory")
if(NOT EXISTS "${ARDUINOJSON_SRC}/ArduinoJson.h")
    message(FATAL_ERROR "Set ARDUINOJSON_SRC to the ArduinoJson src directory")
endif()

set(FIRMWARE "${CMAKE_CURRENT_SOURCE_DIR}/../../firmware/app")
add_executable(calibration_tests
    calibration_tests.cpp
    "${FIRMWARE}/core/CouplerCalibration.cpp"
    "${FIRMWARE}/core/eepromprefs.cpp"
    "${FIRMWARE}/coupler/rfpowersensor.cpp"
)
target_compile_features(calibration_tests PRIVATE cxx_std_17)
target_include_directories(calibration_tests PRIVATE . "${FIRMWARE}" "${ARDUINOJSON_SRC}")
target_compile_definitions(calibration_tests PRIVATE APP_NAME="RayPowerMeter" APP_VERSION="test")

enable_testing()
add_test(NAME calibration COMMAND calibration_tests)

foreach(profile IN ITEMS default lab)
    add_executable(release_defaults_${profile} release_defaults_tests.cpp)
    target_compile_features(release_defaults_${profile} PRIVATE cxx_std_17)
    target_include_directories(release_defaults_${profile} PRIVATE "${FIRMWARE}")
    add_test(NAME release_defaults_${profile} COMMAND release_defaults_${profile})
endforeach()
target_compile_definitions(release_defaults_default PRIVATE EXPECTED_LAB_NETWORK=0)
target_compile_definitions(release_defaults_lab PRIVATE EXPECTED_LAB_NETWORK=1 RAYPM_ENABLE_LAB_NETWORK=1)
