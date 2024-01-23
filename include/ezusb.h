#ifndef EZUSB_H
#define EZUSB_H

int ezusb_reset(struct libusb_device_handle *hdl, int set_clear);
int ezusb_install_firmware(libusb_device_handle *hdl, const char *name);
int ezusb_upload_firmware(libusb_device *dev, int configuration, const char *name);

#endif /* EZUSB_H */
