zephyr_include_directories(${APPLICATION_SOURCE_DIR}/include)

if(CONFIG_ZMK_DISPLAY AND CONFIG_USB_DEVICE_STACK)
  target_sources(app PRIVATE src/usb_display_power.c)
endif()
