cmake_minimum_required(VERSION 4.2)
project(AutoTextInput)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(AutoTextInput main.cpp)
target_link_libraries(AutoTextInput PRIVATE Qt6::Widgets)

# --- Post-build: automatically deploy Qt DLLs and the matching MinGW runtime DLLs ---

# Find windeployqt next to the Qt installation CMake already located
get_target_property(_qt6_qmake_location Qt6::qmake IMPORTED_LOCATION)
get_filename_component(_qt_bin_dir "${_qt6_qmake_location}" DIRECTORY)
find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")

if(WINDEPLOYQT_EXECUTABLE)
    add_custom_command(TARGET AutoTextInput POST_BUILD
        COMMAND "${WINDEPLOYQT_EXECUTABLE}" "$<TARGET_FILE:AutoTextInput>"
        COMMENT "Running windeployqt to copy required Qt DLLs next to the exe"
    )
else()
    message(WARNING "windeployqt not found - Qt DLLs will not be copied automatically")
endif()

# Find the MinGW bin folder belonging to the SAME compiler that builds this target.
# This matters because windeployqt copies runtime DLLs from Qt's own bundled MinGW,
# which can be a different version than the one actually compiling this project -
# causing STATUS_ENTRYPOINT_NOT_FOUND crashes at runtime. Copying from the real
# compiler's folder afterward guarantees they match.
get_filename_component(MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)

foreach(_dll libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll)
    add_custom_command(TARGET AutoTextInput POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MINGW_BIN_DIR}/${_dll}"
            "$<TARGET_FILE_DIR:AutoTextInput>"
        COMMENT "Copying matching MinGW runtime DLL: ${_dll}"
    )
endforeach()