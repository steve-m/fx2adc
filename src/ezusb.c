/*
 * Helper functions for the Cypress EZ-USB / FX2 series chips.
 *
 * Originally part of the of the libsigrok project, adapted for fx2adc
 *
 * Copyright (C) 2010-2012 Bert Vermeulen <bert@biot.com>
 *
 * SPDX-License-Identifier: GPL-3.0+
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <libusb.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/stat.h>

#define FW_CHUNKSIZE (4 * 1024)

// TODO move that to common header file
#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

const char *fw_pathlist[] = { "",
			      "firmware/",
			      "../firmware/",
			      "../../firmware/",
			       "/usr/share/fx2adc-firmware/",
			      "/usr/share/sigrok-firmware/", };

static void *firmware_file_load(const char *name, size_t *size, off_t max_size)
{
	char *filename;
	FILE *file;
	bool file_found = false;
	size_t length, n_read;
	unsigned char *buf = NULL;
	struct stat file_stat;

	for (size_t i = 0; i < ARRAY_SIZE(fw_pathlist); i++) {
		filename = calloc(strlen(fw_pathlist[i]) + strlen(name) + 1, 1);
		strcat(filename, fw_pathlist[i]);
		strcat(filename, name);

		//fprintf(stderr, "Trying %s\n", filename);

		if (!stat(filename, &file_stat)) {
			fprintf(stderr, "Using %s\n", filename);
			file_found = true;
			break;
		}

		free(filename);
	}

	if (!file_found) {
		fprintf(stderr, "Could not find firmware file '%s'!\n", filename);
		return NULL;
	}

	if (file_stat.st_size > max_size) {
		fprintf(stderr, "Firmware file too large, aborting!");
		return NULL;
	}

	length = file_stat.st_size;
	*size = length;

	file = fopen(filename, "rb");

	if (!file) {
		fprintf(stderr, "Attempt to open '%s' failed: %d\n", filename, errno);
		free(filename);
		return NULL;
	}

	free(filename);

	buf = malloc(length);

	n_read = fread(buf, 1, length, file);

	if (n_read != length && ferror(file)) {
		fprintf(stderr, "Failed to read firmware file\n");
		free(buf);
		return NULL;
	}

	return buf;
}

int ezusb_reset(struct libusb_device_handle *hdl, int set_clear)
{
	int ret;
	unsigned char buf[1];

	buf[0] = set_clear ? 1 : 0;
	ret = libusb_control_transfer(hdl, LIBUSB_REQUEST_TYPE_VENDOR, 0xa0,
				      0xe600, 0x0000, buf, 1, 100);
	if (ret < 0)
		fprintf(stderr, "Unable to send control request: %s.",
				libusb_error_name(ret));

	return ret;
}

int ezusb_install_firmware(libusb_device_handle *hdl, const char *name)
{
	unsigned char *firmware;
	size_t length, offset, chunksize;
	int ret, result;

	/* Max size is 64 kiB since the value field of the setup packet,
	 * which holds the firmware offset, is only 16 bit wide.
	 */
	firmware = firmware_file_load(name, &length, 1 << 16);
	if (!firmware)
		return -1;

	result = 0;
	offset = 0;
	while (offset < length) {
		chunksize = MIN(length - offset, FW_CHUNKSIZE);

		ret = libusb_control_transfer(hdl, LIBUSB_REQUEST_TYPE_VENDOR |
					      LIBUSB_ENDPOINT_OUT, 0xa0, offset,
					      0x0000, firmware + offset,
					      chunksize, 100);
		if (ret < 0) {
			fprintf(stderr, "Unable to send firmware to device: %s.\n",
					libusb_error_name(ret));
			free(firmware);
			return -1;
		}

		offset += chunksize;
	}
	free(firmware);

	fprintf(stderr, "Firmware upload done.\n");

	return result;
}

int ezusb_upload_firmware(libusb_device *dev, int configuration, const char *name)
{
	struct libusb_device_handle *hdl;
	int ret;

	if ((ret = libusb_open(dev, &hdl)) < 0) {
		fprintf(stderr, "failed to open device: %s.\n", libusb_error_name(ret));
		return -1;
	}

/*
 * The libusb Darwin backend is broken: it can report a kernel driver being
 * active, but detaching it always returns an error.
 */
#if !defined(__APPLE__)
	if (libusb_kernel_driver_active(hdl, 0) == 1) {
		if ((ret = libusb_detach_kernel_driver(hdl, 0)) < 0) {
			fprintf(stderr, "failed to detach kernel driver: %s",
					libusb_error_name(ret));
			return -1;
		}
	}
#endif

	if ((ret = libusb_set_configuration(hdl, configuration)) < 0) {
		fprintf(stderr, "Unable to set configuration: %s",
				libusb_error_name(ret));
		return -1;
	}

	if ((ezusb_reset(hdl, 1)) < 0)
		return -1;

	if (ezusb_install_firmware(hdl, name) < 0)
		return -1;

	if ((ezusb_reset(hdl, 0)) < 0)
		return -1;

	libusb_close(hdl);

	return 0;
}
