cmake_minimum_required(VERSION 3.16)
project(vnxkey_daemon VERSION 26.06.004 LANGUAGES CXX)

# Yêu cầu C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type mặc định là Debug
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# Release: strip binary để giảm kích thước
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s")
endif()

# ============================================================
# Tìm các thư viện hệ thống
# ============================================================

find_package(PkgConfig REQUIRED)

# libevdev: đọc sự kiện từ /dev/input/eventX
# Cài bằng: sudo apt install libevdev-dev
pkg_check_modules(LIBEVDEV libevdev)

if(NOT LIBEVDEV_FOUND)
  message(WARNING "libevdev not found. Install: sudo apt install libevdev-dev")
  message(WARNING "Keyboard interception (Phase 3) will be DISABLED.")
endif()

# nlohmann/json (optional)
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
  pkg_check_modules(NLOHMANN_JSON nlohmann_json)
  if(NOT NLOHMANN_JSON_FOUND)
    message(STATUS "nlohmann_json not found (optional). Install: sudo apt install nlohmann-json3-dev")
  endif()
endif()

# ============================================================
# Unikey Core
# ============================================================
file(GLOB UNIKEY_SOURCES "third_party/unikey/src/*.cpp")
add_library(unikey_core STATIC ${UNIKEY_SOURCES})
target_include_directories(unikey_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/unikey/src)
# Unikey core may have some warnings, let's ignore them
target_compile_options(unikey_core PRIVATE -w)

# ============================================================
# Source files
# ============================================================

set(DAEMON_SOURCES
    src/main.cpp
    src/engine/viet_engine.cpp
    src/ipc/config_watcher.cpp
)

if(LIBEVDEV_FOUND)
  list(APPEND DAEMON_SOURCES
      src/input/evdev_handler.cpp
      src/output/uinput_handler.cpp
  )
  add_compile_definitions(HAVE_LIBEVDEV)
endif()

# ============================================================
# Target chính: vnxkey-daemon
# ============================================================

add_executable(vnxkey-daemon ${DAEMON_SOURCES})

target_include_directories(vnxkey-daemon PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/unikey/src
)

target_link_libraries(vnxkey-daemon PRIVATE unikey_core)

if(LIBEVDEV_FOUND)
  target_include_directories(vnxkey-daemon PRIVATE ${LIBEVDEV_INCLUDE_DIRS})
  target_link_libraries(vnxkey-daemon PRIVATE ${LIBEVDEV_LIBRARIES})
endif()

if(nlohmann_json_FOUND)
  target_link_libraries(vnxkey-daemon PRIVATE nlohmann_json::nlohmann_json)
endif()

target_compile_options(vnxkey-daemon PRIVATE
    -Wall -Wextra -Wpedantic -Wshadow -Wno-unused-parameter
)

# Release: tối ưu tốc độ
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  target_compile_options(vnxkey-daemon PRIVATE -O2 -DNDEBUG)
endif()

# ============================================================
# Target phụ: vnxkey-engine-test
# ============================================================

add_executable(vnxkey-engine-test
    src/engine/viet_engine.cpp
    tests/engine_test.cpp
)

target_include_directories(vnxkey-engine-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/unikey/src
)

target_link_libraries(vnxkey-engine-test PRIVATE unikey_core)

target_compile_options(vnxkey-engine-test PRIVATE -Wall -Wextra)

# ============================================================
# Install rules (FHS-compliant cho .deb packaging)
#
# Khi đóng gói: cmake -DCMAKE_INSTALL_PREFIX=/usr ..
# Khi install thủ công: cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
# ============================================================

include(GNUInstallDirs)

# Daemon binary → /usr/lib/vnxkey/vnxkey-daemon
# (không đặt trong /usr/bin vì daemon cần chạy qua systemd với root,
#  không nên user chạy thẳng)
install(TARGETS vnxkey-daemon
    RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}/vnxkey
)

# Systemd service → /lib/systemd/system/vnxkey.service
# (trên Debian/Ubuntu modern dùng /lib/systemd, không phải /etc/systemd)
install(FILES systemd/vnxkey.service
    DESTINATION lib/systemd/system
)

# ============================================================
# CPack - Tạo .deb package từ CMake
# Alternative method: dùng build-release.sh script (recommended)
# Hoặc: cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && cpack -G DEB
# ============================================================

set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "vnxkey")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_RELEASE "1")
set(CPACK_PACKAGE_ARCHITECTURE "amd64")
set(CPACK_PACKAGE_MAINTAINER "VNXKey Team <maintainer@vnxkey.dev>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "VNXKey - Vietnamese Input Method for Linux")
set(CPACK_PACKAGE_DESCRIPTION
    "Kernel-level Vietnamese input method daemon.\n"
    "Intercepts keyboard events via evdev and outputs Vietnamese characters.\n"
    "No IBus/Fcitx5 required. Works on both X11 and Wayland.")

set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "libevdev2 (>= 1.0), libc6 (>= 2.17)")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "vnxkey-gui")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/vnxkey/vnxkey")

# Control scripts (postinst/prerm/postrm)
# Các file này được tạo trong Phase C bên dưới
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_CURRENT_SOURCE_DIR}/../packaging/DEBIAN/postinst"
    "${CMAKE_CURRENT_SOURCE_DIR}/../packaging/DEBIAN/prerm"
    "${CMAKE_CURRENT_SOURCE_DIR}/../packaging/DEBIAN/postrm"
)

include(CPack)

# ============================================================
# Build info
# ============================================================

message(STATUS "")
message(STATUS "=== VNXKey Daemon Build Configuration ===")
message(STATUS "  Version        : ${PROJECT_VERSION}")
message(STATUS "  C++ Standard   : ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type     : ${CMAKE_BUILD_TYPE}")
message(STATUS "  libevdev       : ${LIBEVDEV_FOUND} (version ${LIBEVDEV_VERSION})")
message(STATUS "  Install prefix : ${CMAKE_INSTALL_PREFIX}")
message(STATUS "  Lib dir        : ${CMAKE_INSTALL_LIBDIR}")
message(STATUS "==========================================")
