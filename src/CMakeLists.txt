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
# Setup shared library variant
########################################################################
add_library(fx2adc SHARED libfx2adc.c ezusb.c si5351.c)
target_link_libraries(fx2adc ${LIBUSB_LIBRARIES} ${THREADS_PTHREADS_LIBRARY})
target_include_directories(fx2adc PUBLIC
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>  # <prefix>/include
  ${LIBUSB_INCLUDE_DIRS}
  ${THREADS_PTHREADS_INCLUDE_DIR}
  )
set_target_properties(fx2adc PROPERTIES DEFINE_SYMBOL "fx2adc_EXPORTS")
set_target_properties(fx2adc PROPERTIES OUTPUT_NAME fx2adc)
set_target_properties(fx2adc PROPERTIES SOVERSION ${MAJOR_VERSION})
set_target_properties(fx2adc PROPERTIES VERSION ${LIBVER})
generate_export_header(fx2adc)

########################################################################
# Setup static library variant
########################################################################
add_library(fx2adc_static STATIC libfx2adc.c ezusb.c si5351.c)
target_link_libraries(fx2adc_static m ${LIBUSB_LIBRARIES} ${THREADS_PTHREADS_LIBRARY})
target_include_directories(fx2adc_static PUBLIC
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>  # <prefix>/include
  ${LIBUSB_INCLUDE_DIRS}
  ${THREADS_PTHREADS_INCLUDE_DIR}
  )
set_property(TARGET fx2adc_static APPEND PROPERTY COMPILE_DEFINITIONS "fx2adc_STATIC" )
set_property(TARGET fx2adc_static PROPERTY POSITION_INDEPENDENT_CODE ON)
if(NOT WIN32)
# Force same library filename for static and shared variants of the library
set_target_properties(fx2adc_static PROPERTIES OUTPUT_NAME fx2adc)
endif()
generate_export_header(fx2adc_static)

########################################################################
# Set up Windows DLL resource files
########################################################################
IF(MSVC)
    include(${CMAKE_SOURCE_DIR}/cmake/Modules/Version.cmake)

    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/fx2adc.rc.in
        ${CMAKE_CURRENT_BINARY_DIR}/fx2adc.rc
    @ONLY)
  target_sources(fx2adc PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/fx2adc.rc)
  target_sources(fx2adc_static PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/fx2adc.rc)
ENDIF(MSVC)

########################################################################
# Setup libraries used in executables
########################################################################
if(WIN32)
add_library(libgetopt_static STATIC
    getopt/getopt.c
)
target_link_libraries(
    fx2adc
)
endif()

########################################################################
# Build utility
########################################################################
add_executable(fx2adc_file fx2adc_file.c)
add_executable(fx2adc_tcp fx2adc_tcp.c)
add_executable(fx2adc_test fx2adc_test.c)
set(INSTALL_TARGETS fx2adc fx2adc_static fx2adc_file fx2adc_tcp fx2adc_test)

target_link_libraries(fx2adc_file fx2adc
    ${LIBUSB_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
)
target_link_libraries(fx2adc_tcp fx2adc
    ${LIBUSB_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
)
target_link_libraries(fx2adc_test fx2adc
    ${LIBUSB_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
)
if(UNIX)
if(APPLE OR CMAKE_SYSTEM MATCHES "OpenBSD")
    target_link_libraries(fx2adc_test m)
else()
    target_link_libraries(fx2adc_test m rt)
endif()
endif()

if(WIN32)
target_link_libraries(fx2adc_file libgetopt_static)
target_link_libraries(fx2adc_tcp ws2_32 libgetopt_static)
target_link_libraries(fx2adc_test libgetopt_static)
set_property(TARGET fx2adc_file APPEND PROPERTY COMPILE_DEFINITIONS "fx2adc_STATIC" )
set_property(TARGET fx2adc_tcp APPEND PROPERTY COMPILE_DEFINITIONS "fx2adc_STATIC" )
set_property(TARGET fx2adc_test APPEND PROPERTY COMPILE_DEFINITIONS "fx2adc_STATIC" )
endif()
########################################################################
# Install built library files & utilities
########################################################################
install(TARGETS fx2adc EXPORT FX2ADC-export
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} # .so/.dylib file
  )
install(TARGETS fx2adc_static EXPORT FX2ADC-export
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} # .so/.dylib file
  )
install(TARGETS fx2adc_file fx2adc_tcp fx2adc_test
  DESTINATION ${CMAKE_INSTALL_BINDIR}
  )
