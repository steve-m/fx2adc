# Copyright 2012-2020 Osmocom Project
#
# This file is part of fx2adc
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.


########################################################################
# Project setup
########################################################################
cmake_minimum_required(VERSION 3.7.2)

# workaround for https://gitlab.kitware.com/cmake/cmake/issues/16967
if(${CMAKE_VERSION} VERSION_LESS "3.12.0")
    project(fx2adc)
else()
    project(fx2adc C)
endif()

#select the release build type by default to get optimization flags
if(NOT CMAKE_BUILD_TYPE)
   set(CMAKE_BUILD_TYPE "Release")
   message(STATUS "Build type not specified: defaulting to release.")
endif(NOT CMAKE_BUILD_TYPE)

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake/Modules)

include(GNUInstallDirs)
include(GenerateExportHeader)
include(CMakePackageConfigHelpers)

# Set the version information here
set(VERSION_INFO_MAJOR_VERSION 0) # increment major on api compatibility changes
set(VERSION_INFO_MINOR_VERSION 1) # increment minor on feature-level changes
set(VERSION_INFO_PATCH_VERSION 0) # increment patch for bug fixes and docs
include(Version) # setup version info

########################################################################
# Compiler specific setup
########################################################################
if(CMAKE_COMPILER_IS_GNUCC AND NOT WIN32)
    ADD_DEFINITIONS(-Wall)
    ADD_DEFINITIONS(-Wextra)
    ADD_DEFINITIONS(-Wno-unused-parameter)
    ADD_DEFINITIONS(-Wno-unused)
    ADD_DEFINITIONS(-Wsign-compare)
    #http://gcc.gnu.org/wiki/Visibility
    add_definitions(-fvisibility=hidden)
elseif(MSVC14 OR MSVC14)
#pthread-w32 issue, timespec is now part of time.h
    ADD_DEFINITIONS(-D_TIMESPEC_DEFINED)
endif()

########################################################################
# Find build dependencies
########################################################################
find_package(Threads)
find_package(PkgConfig)

if(PKG_CONFIG_FOUND)
    pkg_check_modules(LIBUSB libusb-1.0 IMPORTED_TARGET)
    if(LIBUSB_LINK_LIBRARIES)
        set(LIBUSB_LIBRARIES "${LIBUSB_LINK_LIBRARIES}")
    endif()
else()
    set(LIBUSB_LIBRARIES "" CACHE STRING "manual libusb path")
    set(LIBUSB_INCLUDE_DIRS "" CACHE STRING "manual libusb includepath")
endif()

if(MSVC)
    set(THREADS_PTHREADS_LIBRARY "" CACHE STRING "manual pthread-win32 path")
    set(THREADS_PTHREADS_INCLUDE_DIR "" CACHE STRING "manual pthread-win32 includepath")
else()
    set(THREADS_PTHREADS_LIBRARY "" CACHE INTERNAL "manual pthread-win32 path")
    set(THREADS_PTHREADS_INCLUDE_DIR "" CACHE INTERNAL "manual pthread-win32 includepath")
endif()

if(PKG_CONFIG_FOUND AND NOT LIBUSB_FOUND)
    message(FATAL_ERROR "LibUSB 1.0 required to compile fx2adc")
endif()
if(NOT THREADS_FOUND)
    message(FATAL_ERROR "pthreads(-win32) required to compile fx2adc")
endif()

########################################################################
# Create uninstall target
########################################################################
configure_file(
    ${PROJECT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
@ONLY)

add_custom_target(uninstall
    ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
)

########################################################################
# Install udev rules
########################################################################
option(INSTALL_UDEV_RULES "Install udev rules" ON)
if (INSTALL_UDEV_RULES)
    install (
        FILES fx2adc.rules
        DESTINATION "/etc/udev/rules.d"
        COMPONENT "udev"
        )
else (INSTALL_UDEV_RULES)
    message (STATUS "Udev rules not being installed, install them with -DINSTALL_UDEV_RULES=ON")
endif (INSTALL_UDEV_RULES)

option(INSTALL_FIRMWARE "Install firmware files" ON)
if (INSTALL_FIRMWARE)
    install (
        DIRECTORY firmware/
        DESTINATION "${CMAKE_INSTALL_PREFIX}/share/fx2adc-firmware"
        COMPONENT "firmware"
        FILES_MATCHING PATTERN "*.fw"
        )
else (INSTALL_FIRMWARE)
    message (STATUS "Firmware files not being installed, install them with -DINSTALL_FIRMWARE=ON")
endif (INSTALL_FIRMWARE)

option(DETACH_KERNEL_DRIVER "Detach kernel driver if loaded" OFF)
if (DETACH_KERNEL_DRIVER)
    message (STATUS "Building with kernel driver detaching enabled")
    add_definitions(-DDETACH_KERNEL_DRIVER=1)
else (DETACH_KERNEL_DRIVER)
    message (STATUS "Building with kernel driver detaching disabled, use -DDETACH_KERNEL_DRIVER=ON to enable")
endif (DETACH_KERNEL_DRIVER)

option(ENABLE_ZEROCOPY "Enable usbfs zero-copy support" ON)
if (ENABLE_ZEROCOPY)
    message (STATUS "Building with usbfs zero-copy support enabled")
    add_definitions(-DENABLE_ZEROCOPY=1)
else (ENABLE_ZEROCOPY)
    message (STATUS "Building with usbfs zero-copy support disabled, use -DENABLE_ZEROCOPY=ON to enable")
endif (ENABLE_ZEROCOPY)

########################################################################
# Install public header files
########################################################################
install(FILES
    include/fx2adc.h
    include/fx2adc_export.h
    DESTINATION include
)

########################################################################
# Add subdirectories
########################################################################
add_subdirectory(src)

########################################################################
# Create Pkg Config File
########################################################################
FOREACH(inc ${LIBUSB_INCLUDEDIR})
    LIST(APPEND FX2ADC_PC_CFLAGS "-I${inc}")
ENDFOREACH(inc)

FOREACH(lib ${LIBUSB_LIBRARY_DIRS})
    LIST(APPEND FX2ADC_PC_LIBS "-L${lib}")
ENDFOREACH(lib)

# use space-separation format for the pc file
STRING(REPLACE ";" " " FX2ADC_PC_CFLAGS "${FX2ADC_PC_CFLAGS}")
STRING(REPLACE ";" " " FX2ADC_PC_LIBS "${FX2ADC_PC_LIBS}")

# unset these vars to avoid hard-coded paths to cross environment
IF(CMAKE_CROSSCOMPILING)
    UNSET(FX2ADC_PC_CFLAGS)
    UNSET(FX2ADC_PC_LIBS)
ENDIF(CMAKE_CROSSCOMPILING)

set(prefix "${CMAKE_INSTALL_PREFIX}")
set(exec_prefix \${prefix})
set(includedir \${prefix}/include)
set(libdir \${exec_prefix}/lib)

CONFIGURE_FILE(
    ${CMAKE_CURRENT_SOURCE_DIR}/libfx2adc.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/libfx2adc.pc
@ONLY)

INSTALL(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/libfx2adc.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

########################################################################
# Create CMake Config File
########################################################################
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/fx2adc/fx2adcConfigVersion.cmake"
  VERSION ${VERSION}
  COMPATIBILITY AnyNewerVersion
  )

configure_file(cmake/fx2adcConfig.cmake
  "${CMAKE_CURRENT_BINARY_DIR}/fx2adc/fx2adcConfig.cmake"
  COPYONLY
  )

set(ConfigPackageLocation lib/cmake/fx2adc)
install(EXPORT FX2ADC-export
  FILE fx2adcTargets.cmake
  NAMESPACE fx2adc::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fx2adc/
  )
install(
  FILES
  cmake/fx2adcConfig.cmake
  "${CMAKE_CURRENT_BINARY_DIR}/fx2adc/fx2adcConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fx2adc/
  COMPONENT Devel
  )

########################################################################
# Print Summary
########################################################################
MESSAGE(STATUS "Building for version: ${VERSION} / ${LIBVER}")
MESSAGE(STATUS "Using install prefix: ${CMAKE_INSTALL_PREFIX}")
