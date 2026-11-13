cmake_minimum_required(VERSION 3.25)
project(DJRH_VST3 VERSION 1.0.0 DESCRIPTION "DJ RH Audio Zone VST3 Instrument")

set(SMTG_ENABLE_VSTGUI_SUPPORT ON CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_TESTS OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)

set(SDK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../external/vst3sdk")
add_subdirectory("${SDK_ROOT}" "${CMAKE_BINARY_DIR}/vst3sdk")

set(plugin_sources
    src/processor.cpp
    src/processor.h
    src/controller.cpp
    src/controller.h
    src/factory.cpp
)

smtg_add_vst3plugin(DJRH_Audio_Zone
    PACKAGE_NAME "DJ RH Audio Zone"
    SOURCES_LIST ${plugin_sources}
)

target_compile_features(DJRH_Audio_Zone PRIVATE cxx_std_17)
target_include_directories(DJRH_Audio_Zone PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
target_link_libraries(DJRH_Audio_Zone PRIVATE sdk vstgui_support)

smtg_target_add_plugin_resources(DJRH_Audio_Zone
    RESOURCES
        resource/editor.uidesc
)
