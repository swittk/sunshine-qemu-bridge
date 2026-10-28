#!/usr/bin/env python3
"""Patch an *isolated* Sunshine checkout to register the opt-in QEMU-D-Bus backend.
Never touch any upstream or live ReiMS checkout.
"""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
sunshine = root / "Sunshine"
assert (sunshine / ".git").exists(), "Expected local Sunshine clone"

def replace_once(path, old, new):
    data = path.read_text()
    if data.count(old) != 1:
        raise RuntimeError(f"expected one exact anchor in {path.name}, found {data.count(old)}: {old[:110]}")
    path.write_text(data.replace(old,new))

misc = sunshine / "src/platform/linux/misc.cpp"
replace_once(misc,
    "      MAX_FLAGS  ///< The maximum number of flags\n",
    "#ifdef SUNSHINE_BUILD_QEMU_DBUS\n      QEMU,  ///< QEMU D-Bus shared-memory capture\n#endif\n      MAX_FLAGS  ///< The maximum number of flags\n")
replace_once(misc,
    "  static std::bitset<source::MAX_FLAGS> sources;\n",
    """  static std::bitset<source::MAX_FLAGS> sources;

#ifdef SUNSHINE_BUILD_QEMU_DBUS
  std::vector<std::string> qemu_dbus_display_names();
  std::shared_ptr<display_t> qemu_dbus_display(mem_type_e hwdevice_type,
    const std::string &display_name, const video::config_t &config);
#endif
""")
replace_once(misc,
    "  std::vector<std::string> display_names(mem_type_e hwdevice_type) {\n",
    """  std::vector<std::string> display_names(mem_type_e hwdevice_type) {
#ifdef SUNSHINE_BUILD_QEMU_DBUS
    if (sources[source::QEMU]) {
      return qemu_dbus_display_names();
    }
#endif
""")
replace_once(misc,
    "  std::shared_ptr<display_t> display(mem_type_e hwdevice_type, const std::string &display_name, const video::config_t &config) {\n",
    """  std::shared_ptr<display_t> display(mem_type_e hwdevice_type, const std::string &display_name, const video::config_t &config) {
#ifdef SUNSHINE_BUILD_QEMU_DBUS
    if (sources[source::QEMU]) {
      return qemu_dbus_display(hwdevice_type, display_name, config);
    }
#endif
""")
replace_once(misc,
    "    std::string selected_capture = config::video.capture;\n",
    """    std::string selected_capture = config::video.capture;
#ifdef SUNSHINE_BUILD_QEMU_DBUS
    if (selected_capture == "qemu" && !qemu_dbus_display_names().empty()) {
      sources[source::QEMU] = true;
    }
#endif
""")
replace_once(misc,
    '    if (!gladLoaderLoadEGL(NULL)) {\n',
    """#ifdef SUNSHINE_BUILD_QEMU_DBUS
    // QEMU CPU frames do not require an EGL display.
    if (!sources[source::QEMU] && !gladLoaderLoadEGL(NULL)) {
#else
    if (!gladLoaderLoadEGL(NULL)) {
#endif
""")

cmake = sunshine / "cmake/compile_definitions/linux.cmake"
marker = "# linux specific compile definitions\n"
insert = """# Experimental source adapter for QEMU Display1.
# This is a source-only build option; default remains OFF.
option(SUNSHINE_ENABLE_QEMU_DBUS "Capture QEMU D-Bus display frames directly" OFF)
if(SUNSHINE_ENABLE_QEMU_DBUS)
    set(QMDP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(QMDP_ENABLE_REAL_QEMU_E2E OFF CACHE BOOL "" FORCE)
    set(QMDP_ENABLE_DMABUF_READBACK OFF CACHE BOOL "" FORCE)
    set(QMDP_BUILD_DBUS ON CACHE BOOL "" FORCE)
    add_subdirectory("${CMAKE_SOURCE_DIR}/../qsm-rd/qsm-direct" "${CMAKE_BINARY_DIR}/qmdp")
    add_library(reims_capture_bridge STATIC "${CMAKE_SOURCE_DIR}/../bridge/src/capture_feed.cpp")
    target_include_directories(reims_capture_bridge PUBLIC "${CMAKE_SOURCE_DIR}/../bridge/src")
    target_link_libraries(reims_capture_bridge PUBLIC qmdp_core)
    include_directories("${CMAKE_SOURCE_DIR}/../bridge/src" "${CMAKE_SOURCE_DIR}/../qsm-rd/qsm-direct/src")
    list(APPEND PLATFORM_LIBRARIES qmdp_dbus reims_capture_bridge)
    list(APPEND PLATFORM_TARGET_FILES "${CMAKE_SOURCE_DIR}/src/platform/linux/qemudbusgrab.cpp")
    add_compile_definitions(SUNSHINE_BUILD_QEMU_DBUS)
endif()

"""
replace_once(cmake,marker,marker+insert)
print("Registered isolated opt-in QEMU D-Bus capture backend")
