include(FindPkgConfig)
pkg_check_modules(LIBUSB libusb-1.0 IMPORTED_TARGET)

get_filename_component(FX2ADC_CMAKE_DIR "${CMAKE_CURRENT_LIST_FILE}" PATH)

if(NOT TARGET fx2adc::fx2adc)
  include("${FX2ADC_CMAKE_DIR}/fx2adcTargets.cmake")
endif()
