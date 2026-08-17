cmake_minimum_required(VERSION 3.16)

project(ADV-Explorer
    VERSION 0.2.4
    DESCRIPTION "Interactive analysis of acoustic Doppler velocimetry (ADV) measurements"
    HOMEPAGE_URL "https://github.com/sschwindt/ADV-Explorer"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent PrintSupport Network Sql)
# QXlsx links Qt6::GuiPrivate but never looks it up itself; request it here so
# the target exists on Qt installations that do not pull it in implicitly
find_package(Qt6 COMPONENTS GuiPrivate QUIET)

# ---------------------------------------------------------------------------
# Third-party: QXlsx (MIT) for reading/writing xlsx workbooks
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(QXlsx
    GIT_REPOSITORY https://github.com/QtExcel/QXlsx.git
    GIT_TAG        v1.4.9
    SOURCE_SUBDIR  QXlsx
)
FetchContent_MakeAvailable(QXlsx)

# QXlsx v1.4.9 defines QT_NO_CAST_FROM_ASCII but still contains bare C-string
# literals (e.g. xlsxworkbook.cpp), which breaks the build; drop the definition.
get_target_property(_qxlsx_defs QXlsx COMPILE_DEFINITIONS)
if(_qxlsx_defs)
    list(REMOVE_ITEM _qxlsx_defs QT_NO_CAST_FROM_ASCII)
    set_target_properties(QXlsx PROPERTIES COMPILE_DEFINITIONS "${_qxlsx_defs}")
endif()

# ---------------------------------------------------------------------------
# Third-party: QCustomPlot (GPLv3, vendored) for scientific plotting
# ---------------------------------------------------------------------------
add_library(qcustomplot STATIC
    third_party/qcustomplot/qcustomplot.cpp
)
target_include_directories(qcustomplot PUBLIC third_party/qcustomplot)
target_link_libraries(qcustomplot PUBLIC Qt6::Widgets Qt6::PrintSupport)

# ---------------------------------------------------------------------------
# Third-party: KissFFT (BSD, vendored) for spectra (dissipation estimate)
# ---------------------------------------------------------------------------
add_library(kissfft STATIC
    third_party/kissfft/kiss_fft.c
    third_party/kissfft/kiss_fftr.c
)
target_include_directories(kissfft PUBLIC third_party/kissfft)
target_compile_definitions(kissfft PUBLIC kiss_fft_scalar=double)

# ---------------------------------------------------------------------------
# Third-party: miniz (MIT, vendored) to unpack SonTek FlowTracker2 .ft archives
# ---------------------------------------------------------------------------
add_library(miniz STATIC
    third_party/miniz/miniz.c
)
target_include_directories(miniz PUBLIC third_party/miniz)
target_compile_definitions(miniz PUBLIC
    MINIZ_NO_STDIO
    MINIZ_NO_TIME
    MINIZ_NO_ARCHIVE_WRITING_APIS
    # Qt already exports the zlib symbols; keep miniz on its own mz_ prefix
    MINIZ_NO_ZLIB_COMPATIBLE_NAMES
)

# ---------------------------------------------------------------------------
# Core library (no GUI dependencies except QtCore/Concurrent)
# ---------------------------------------------------------------------------
add_library(advcore STATIC
    src/core/AdvData.cpp
    src/core/Crs.cpp
    src/core/ExampleProject.cpp
    # sample measurements for Help > Load example; see src/examples.qrc
    src/examples.qrc
    src/core/FormatRegistry.cpp
    src/core/GeoPointImport.cpp
    src/core/FlowTrackerCsvReader.cpp
    src/core/FlowTrackerReader.cpp
    src/core/VnaReader.cpp
    src/core/CsvReader.cpp
    src/core/FlowStats.cpp
    src/core/Despike.cpp
    src/core/Rotation.cpp
    src/core/MeasurementPoint.cpp
    src/core/ProjectModel.cpp
    src/core/StatsLabels.cpp
    src/core/Project.cpp
    src/core/ProfileStatsExport.cpp
    src/core/ZipArchive.cpp
)
target_include_directories(advcore PUBLIC src)
target_link_libraries(advcore
    PUBLIC Qt6::Core Qt6::Gui Qt6::Concurrent
    PRIVATE Qt6::Sql
    PRIVATE kissfft miniz QXlsx::QXlsx
)

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
set(APP_SOURCES
    src/main.cpp
    src/gui/MainWindow.cpp
    src/gui/SiteView.h
    src/gui/FlumeView.cpp
    src/gui/MapView.cpp
    src/gui/CrsDialog.cpp
    src/gui/FlowTrackerImportWizard.cpp
    src/gui/GuidedTour.cpp
    src/gui/PointWizard.cpp
    src/gui/ImportWizard.cpp
    src/gui/PlotFrame.cpp
    src/gui/ProfileFrame.cpp
    src/gui/SeriesStyleDialog.cpp
    src/gui/PlotOptionsDialog.cpp
    src/gui/AboutDialog.cpp
    src/resources.qrc
)

if(WIN32)
    list(APPEND APP_SOURCES src/app.rc)
endif()

add_executable(adv-explorer WIN32 ${APP_SOURCES})
target_link_libraries(adv-explorer PRIVATE advcore qcustomplot Qt6::Widgets Qt6::Network)
target_compile_definitions(adv-explorer PRIVATE
    ADV_EXPLORER_VERSION="${PROJECT_VERSION}"
    ADV_EXPLORER_URL="${PROJECT_HOMEPAGE_URL}"
)

install(TARGETS adv-explorer RUNTIME DESTINATION bin)
# ship the profile-stats template next to the executable (also inside AppImages)
install(FILES templates/ADV-profiles.xlsx DESTINATION bin/templates)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()
find_package(Qt6 COMPONENTS Test QUIET)
if(Qt6Test_FOUND)
    add_subdirectory(tests)
endif()
