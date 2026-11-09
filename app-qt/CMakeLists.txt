cmake_minimum_required(VERSION 3.16)

project(RayPowerMeter VERSION 0.1 LANGUAGES CXX)

# C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable Qt's automatic MOC/UIC/RCC handling
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_AUTOUIC_SEARCH_PATHS
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find Qt 6 and the modules we use
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Network
    SerialPort
    Multimedia
)

# --------------------------------------------------------------------
# App constants
# --------------------------------------------------------------------
set(APP_VERSION        "0.1.0")
set(APP_RELEASE        "Alpha")
set(APP_RELEASE_DATE   "2025-02-10")
set(APP_NAME           "RayPowerMeter")
set(APP_COMPANY        "RFStateSide")
set(APP_DOMAIN         "rfstateside.com")

# --------------------------------------------------------------------
# Sources (keep these lists clean; Qt Creator tends to behave better)
# --------------------------------------------------------------------
set(RAY_POWER_METER_SOURCES
    src/app/main.cpp
    src/app/mainwindow.cpp

    src/comm/RayCommManager.cpp
    src/comm/RaySerialCommWorker.cpp
    src/comm/RayHttpCommWorker.cpp

    src/widgets/forwardrefpanel.cpp
    src/widgets/powerbarwidget.cpp

    src/app/dialogsetup.cpp
    src/core/AppSettings.cpp
    src/core/helper.cpp
    src/app/dialoghardwareconfig.cpp
    src/core/RuntimeDataModel.cpp
    src/controllers/LayoutManager.cpp
    src/widgets/widgetbase.cpp
    src/widgets/auxvoltagewidget.cpp
    src/app/dialogprotectionwarning.cpp
    src/widgets/gainwidget.cpp
    src/widgets/powerbarvertwidget.cpp
    src/core/defaultlayoutprovider.cpp
    src/widgets/spacerwidget.cpp
    src/widgets/miniscopewidget.cpp
    src/widgets/scopeplotwidget.cpp
    src/widgets/swrvsfreqplotwidget.cpp
    src/core/rigctldclient.cpp
    src/widgets/spectrumplotwidget.cpp
    src/core/spectrumfft.cpp
    src/widgets/auxsensorwidget.cpp

    src/property/IWidgetPropertyAdapter.cpp
    src/property/GainWidgetPropertyAdapter.cpp
    src/property/AuxSensorWidgetPropertyAdapter.cpp
    src/property/AuxVoltageWidgetPropertyAdapter.cpp
    src/property/ForwardRefPanelPropertyAdapter.cpp
    src/property/PowerBarPropertyAdapter.cpp
    src/property/MiniScopeWidgetPropertyAdapter.cpp
    src/property/SpacerWidgetPropertyAdapter.cpp
    src/widgets/freqwidget.cpp
    src/property/FreqWidgetPropertyAdapter.cpp
    src/widgets/auxdigitalwidget.cpp
    src/property/AuxDigitalWidgetPropertyAdapter.cpp

    src/core/Updater.cpp
    src/widgets/analogpowermeterwidget.cpp
    src/property/AnalogPowerMeterPropertyAdapter.cpp

    src/app/instanceselectordialog.cpp
    src/app/customsplashscreen.cpp
    src/app/dialogabout.cpp
    src/app/dialogviewlogfile.cpp
    src/core/logger.cpp
)
set(RAY_POWER_METER_HEADERS
    src/app/mainwindow.h

    src/core/RayTelemetry.h
    src/core/CouplerProfile.h
    src/core/AppSettings.h
    src/core/RayCouplerConfig.h
    src/core/RayDeviceConfig.h
    src/core/RayAuxConfig.h
    src/core/RuntimeDataModel.h
    src/core/rigctldclient.h
    src/core/spectrumfft.h
    src/core/Updater.h
    src/core/helper.h
    src/core/logger.h

    src/common/Constants.h

    src/comm/RayCommManager.h
    src/comm/RayCommWorkerBase.h
    src/comm/RaySerialCommWorker.h
    src/comm/RayHttpCommWorker.h

    src/widgets/forwardrefpanel.h
    src/widgets/powerbarwidget.h
    src/widgets/widgetbase.h
    src/widgets/auxvoltagewidget.h
    src/widgets/gainwidget.h
    src/widgets/powerbarvertwidget.h
    src/widgets/spacerwidget.h
    src/widgets/miniscopewidget.h
    src/widgets/scopeplotwidget.h
    src/widgets/swrvsfreqplotwidget.h
    src/widgets/spectrumplotwidget.h
    src/widgets/auxsensorwidget.h
    src/widgets/freqwidget.h
    src/widgets/auxdigitalwidget.h
    src/widgets/analogpowermeterwidget.h

    src/app/dialogsetup.h
    src/app/dialoghardwareconfig.h
    src/app/dialogprotectionwarning.h
    src/app/instanceselectordialog.h
    src/app/customsplashscreen.h
    src/app/dialogabout.h
    src/app/dialogviewlogfile.h
)

set(RAY_POWER_METER_FORMS
    ui/mainwindow.ui
    ui/dialogsetup.ui
    ui/DialogHardwareConfig.ui
    ui/dialogprotectionwarning.ui
    ui/dialogabout.ui
    ui/dialogviewlogfile.ui
)

set(RAY_POWER_METER_RESOURCES
    Resources.qrc
)

# --------------------------------------------------------------------
# Create target (NO huge inline list here)
# --------------------------------------------------------------------
qt_add_executable(RayPowerMeter
    src/widgets/amplifieranalyzerplotwidget.h src/widgets/amplifieranalyzerplotwidget.cpp
    src/widgets/amplifieranalyzertablemodel.h src/widgets/amplifieranalyzertablemodel.cpp
    src/widgets/amplifieranalyzerwidget.h src/widgets/amplifieranalyzerwidget.cpp
    src/core/tonegenerator.h src/core/tonegenerator.cpp
    src/controllers/LayoutManager.h)

# Add all sources after the target is created (Qt Creator tends to append here)
target_sources(RayPowerMeter PRIVATE
    ${RAY_POWER_METER_SOURCES}
    ${RAY_POWER_METER_HEADERS}
    ${RAY_POWER_METER_FORMS}
    ${RAY_POWER_METER_RESOURCES}
)

# Let #include "core/..." and "comm/..." work
target_include_directories(RayPowerMeter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# App constants as target compile definitions (not global)
target_compile_definitions(RayPowerMeter PRIVATE
    APP_VERSION="${APP_VERSION}"
    APP_RELEASE="${APP_RELEASE}"
    APP_RELEASE_DATE="${APP_RELEASE_DATE}"
    APP_NAME="${APP_NAME}"
    APP_COMPANY="${APP_COMPANY}"
    APP_DOMAIN="${APP_DOMAIN}"
)

# Link against Qt
target_link_libraries(RayPowerMeter PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::SerialPort
    Qt6::Multimedia
)

# Bundle / Windows EXE properties (cross-platform)
if(APPLE)
    set_target_properties(RayPowerMeter PROPERTIES MACOSX_BUNDLE TRUE)
elseif(WIN32)
    set_target_properties(RayPowerMeter PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
