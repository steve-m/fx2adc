/*
 * fx2adc - acquire data from Cypress FX2 + AD9288 based USB scopes
 *
 * Copyright (C) 2024 by Steve Markgraf <steve@steve-m.de>
 *
 * portions based on librtlsdr:
 * Copyright (C) 2012-2014 by Steve Markgraf <steve@steve-m.de>
 * Copyright (C) 2012 by Dimitri Stolnikov <horiz0n@gmx.net>
 *
 * portions based on libsigrok:
 * Copyright (C) 2015 Christer Ekholm <christerekholm@gmail.com>
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

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#ifdef _MSC_VER
#include <windows.h>
#define usleep(t) Sleep((t)/1000)
#else
#include <unistd.h>
#endif

#include <inttypes.h>
#include <libusb.h>
#include <math.h>
#include <fx2adc_i2c.h>
#include <ezusb.h>
#include <si5351.h>
#include <fx2adc.h>

/*
 * All libusb callback functions should be marked with the LIBUSB_CALL macro
 * to ensure that they are compiled with the same calling convention as libusb.
 *
 * If the macro isn't available in older libusb versions, we simply define it.
 */
#ifndef LIBUSB_CALL
#define LIBUSB_CALL
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#ifndef ARRAY_AND_SIZE
#define ARRAY_AND_SIZE(a) (a), ARRAY_SIZE(a)
#endif

/* Static definitions of structs ending with an all-zero entry are a
 * problem when compiling with -Wmissing-field-initializers: GCC
 * suppresses the warning only with { 0 }, clang wants { } */
#ifdef __clang__
#define ALL_ZERO { }
#else
#define ALL_ZERO { 0 }
#endif

#define VDIV_VALUES \
	{ 1, 1 }, \
	{ 500, 1000 }, \
	{ 250, 1000 }, \
	{ 100, 1000 },

#define VDIV_VALUES_PSO2020 \
	{ 10, 1 }, \
	{ 5, 1 }, \
	{ 2, 1 }, \
	{ 1, 1 }, \
	{ 500, 1000 }, \
	{ 200, 1000 }, \
	{ 100, 1000 }, \
	{ 50, 1000 },

#define VDIV_VALUES_INSTRUSTAR \
	{ 128, 100 }, \
	{ 705, 1000 }, \
	{ 288, 1000 }, \
	{ 140, 1000 }, \
	{ 576, 10000 }, \
	{ 176, 10000 },

#define VDIV_REG \
	1, 2, 5, 10, 11, 12, 13, 14

#define VDIV_MULTIPLIER		10

#define FX2LAFW_EP_IN		0x86
#define USB_INTERFACE		0
#define USB_CONFIGURATION	1

enum control_requests {
	VDIV_CH1_REG   = 0xe0,
	VDIV_CH2_REG   = 0xe1,
	SAMPLERATE_REG = 0xe2,
	TRIGGER_REG    = 0xe3,
	CHANNELS_REG   = 0xe4,
	COUPLING_REG   = 0xe5,
	CALIB_PULSE_REG = 0xe6,
	USE_EXTERNAL_CLK = 0xe7,
	I2C_WRITE_CMD   = 0xe8,
	I2C_READ_CMD   = 0xe9,
};

enum couplings {
	COUPLING_AC = 0,
	COUPLING_DC,
};

enum clk_sources {
	CLK_INTERNAL = 0,
	CLK_EXTERNAL,
};

#define 	SR_KHZ(n)   ((n) * (uint64_t)(1000ULL))
#define 	SR_MHZ(n)   ((n) * (uint64_t)(1000000ULL))

#define DEFAULT_SAMPLERATE	SR_MHZ(30)

#define NUM_CHANNELS		2

#define SAMPLERATE_REGS \
	48, 30, 24, 16, 8, 4, 1, 50, 20, 10,

#define SAMPLERATE_VALUES \
	SR_MHZ(48), SR_MHZ(30), SR_MHZ(24), \
	SR_MHZ(16), SR_MHZ(8), SR_MHZ(4), \
	SR_MHZ(1), SR_KHZ(500), SR_KHZ(200), \
	SR_KHZ(100),

enum fx2adc_async_status {
	FX2ADC_INACTIVE = 0,
	FX2ADC_CANCELING,
	FX2ADC_RUNNING
};

typedef struct fx2adc_devinfo {
	/* VID/PID after cold boot */
	uint16_t orig_vid;
	uint16_t orig_pid;
	/* VID/PID after firmware upload */
	uint16_t fw_vid;
	uint16_t fw_pid;
	uint16_t fw_prod_ver;
	const char *vendor;
	const char *model;
	const char *firmware;
	bool has_coupling;
	const uint64_t (*vdivs)[2];
	const uint32_t vdivs_size;
	bool ch1_bitreversed;
} fx2adc_devinfo_t;

struct fx2adc_dev {
	libusb_context *ctx;
	struct libusb_device_handle *devh;
	const fx2adc_devinfo_t *devinfo;
	uint32_t xfer_buf_num;
	uint32_t xfer_buf_len;
	struct libusb_transfer **xfer;
	unsigned char **xfer_buf;
	fx2adc_read_cb_t cb;
	void *cb_ctx;
	enum fx2adc_async_status async_status;
	int async_cancel;
	int use_zerocopy;

	uint32_t rate; /* Hz */
	uint32_t vdiv; /* mV */

	/* status */
	bool clockgen_present;
	int dev_lost;
	int driver_active;
	unsigned int xfer_errors;
	char manufact[256];
	char product[256];
};

static const uint64_t vdivs[][2] = {
	VDIV_VALUES
};

static const uint64_t vdivs_pso2020[][2] = {
	VDIV_VALUES_PSO2020
};

static const uint64_t vdivs_instrustar[][2] = {
	VDIV_VALUES_INSTRUSTAR
};

static const uint64_t samplerates[] = {
	SAMPLERATE_VALUES
};

static const uint8_t vdiv_reg[] = { VDIV_REG };

static const fx2adc_devinfo_t dev_profiles[] = {
	{
		/* Windows: "Hantek6022BE DRIVER 1": 04b4:6022 */
		0x04b4, 0x6022, 0x1d50, 0x608e, 0x0001,
		"Hantek", "6022BE", "fx2lafw-hantek-6022be.fw",
		false, ARRAY_AND_SIZE(vdivs), false,
	},
	{
		/* Windows: "Hantek6022BE DRIVER 2": 04b5:6022 */
		0x04b5, 0x6022, 0x1d50, 0x608e, 0x0001,
		"Hantek", "6022BE", "fx2lafw-hantek-6022be.fw",
		false, ARRAY_AND_SIZE(vdivs), false,
	},
	{
		0x04b4, 0x2020, 0x1d50, 0x608e, 0x0001,
		"Voltcraft", "DSO2020",  "fx2lafw-hantek-6022be.fw",
		false, ARRAY_AND_SIZE(vdivs), false,
	},
	{
		0x8102, 0x8102, 0x1d50, 0x608e, 0x0002,
		"Sainsmart", "DDS120", "fx2lafw-sainsmart-dds120.fw",
		true, ARRAY_AND_SIZE(vdivs), false,
	},
	{
		/* Windows: "Hantek6022BL DRIVER 1": 04b4:602a */
		0x04b4, 0x602a, 0x1d50, 0x608e, 0x0003,
		"Hantek", "6022BL", "fx2lafw-hantek-6022bl.fw",
		false, ARRAY_AND_SIZE(vdivs), false,
	},
	{
		/* Windows: "Hantek6022BL DRIVER 2": 04b5:602a */
		0x04b5, 0x602a, 0x1d50, 0x608e, 0x0003,
		"Hantek", "6022BL", "fx2lafw-hantek-6022bl.fw",
		false, ARRAY_AND_SIZE(vdivs), false,
	},
	{
		0xd4a2, 0x5660, 0x1d50, 0x608e, 0x0004,
		"YiXingDianZi", "MDSO", "fx2lafw-yixingdianzi-mdso.fw",
		false, ARRAY_AND_SIZE(vdivs), false,
	},
	{
		/*"InstrustarISDS205": d4a2:5661 */
		0xd4a2, 0x5661, 0x1d50, 0x608e, 0x0005,
		"Instrustar", "ISDS205B", "fx2lafw-instrustar-isds205b.fw",
		true, ARRAY_AND_SIZE(vdivs_instrustar), false,
	},
	{
		0x04b4, 0x6023, 0x1d50, 0x608e, 0x0006,
		"Hantek", "PSO2020", "fx2lafw-hantek-pso2020.fw",
		true, ARRAY_AND_SIZE(vdivs_pso2020), true,
	},
	ALL_ZERO
};

#define DEFAULT_BUF_NUMBER	15
#define DEFAULT_BUF_LENGTH	(16 * 32 * 512)

#define CTRL_TIMEOUT	300
#define BULK_TIMEOUT	0

static int fx2adc_write_control(fx2adc_dev_t *dev, enum control_requests req, uint8_t value)
{
	int r;

	if ((r = libusb_control_transfer(dev->devh,
			LIBUSB_REQUEST_TYPE_VENDOR, (uint8_t)req,
			0, 0, &value, 1, CTRL_TIMEOUT)) <= 0) {
		fprintf(stderr, "Control transfer failed: 0x%x: %s\n", req,
			libusb_error_name(r));
		return r;
	}

	return 0;
}

static int fx2adc_read_control(fx2adc_dev_t *dev, enum control_requests req, uint8_t *data, size_t len)
{
	size_t r;

	r = libusb_control_transfer(dev->devh, LIBUSB_REQUEST_TYPE_VENDOR |
		LIBUSB_ENDPOINT_IN, (uint8_t)req, 0, 0,
		data, len, CTRL_TIMEOUT);

	if (r < len) {
		fprintf(stderr, "Control transfer failed: 0x%x: %s\n", req,
			libusb_error_name(r));
		return r;
	}

	return 0;
}

static int fx2adc_i2c_write(fx2adc_dev_t *dev, uint8_t i2c_addr, uint8_t *buffer, uint16_t len)
{
	int r;
	uint16_t wValue = i2c_addr;
	uint16_t wLength = len;

	if ((r = libusb_control_transfer(dev->devh,
			LIBUSB_REQUEST_TYPE_VENDOR, (uint8_t)I2C_WRITE_CMD,
			wValue, 0, buffer, wLength, CTRL_TIMEOUT)) <= 0) {
		fprintf(stderr, "I2C write failed: 0x%x: %s\n", i2c_addr,
			libusb_error_name(r));
		return r;
	}

	return 0;
}


static int fx2adc_i2c_read(fx2adc_dev_t *dev, uint8_t i2c_addr, uint8_t *buffer, uint16_t len)
{
	int r;
	uint16_t wValue = i2c_addr;
	uint16_t wLength = len;

	r = libusb_control_transfer(dev->devh, LIBUSB_REQUEST_TYPE_VENDOR |
			LIBUSB_ENDPOINT_IN, (uint8_t)I2C_READ_CMD,
			wValue, 0, buffer, wLength, CTRL_TIMEOUT);

	return r;
}

int fx2adc_i2c_write_fn(void *dev, uint8_t addr, uint8_t *buf, uint16_t len)
{
	if (dev)
		return fx2adc_i2c_write(((fx2adc_dev_t *)dev), addr, buf, len);

	return -1;
}

int fx2adc_i2c_read_fn(void *dev, uint8_t addr, uint8_t *buf, uint16_t len)
{
	if (dev)
		return fx2adc_i2c_read(((fx2adc_dev_t *)dev), addr, buf, len);

	return -1;
}

int fx2adc_get_usb_strings(fx2adc_dev_t *dev, char *manufact, char *product,
			    char *serial)
{
	struct libusb_device_descriptor dd;
	libusb_device *device = NULL;
	const int buf_max = 256;
	int r = 0;

	if (!dev || !dev->devh)
		return -1;

	device = libusb_get_device(dev->devh);

	r = libusb_get_device_descriptor(device, &dd);
	if (r < 0)
		return -1;

	if (manufact) {
		memset(manufact, 0, buf_max);
		libusb_get_string_descriptor_ascii(dev->devh, dd.iManufacturer,
						   (unsigned char *)manufact,
						   buf_max);
	}

	if (product) {
		memset(product, 0, buf_max);
		libusb_get_string_descriptor_ascii(dev->devh, dd.iProduct,
						   (unsigned char *)product,
						   buf_max);
	}

	if (serial) {
		memset(serial, 0, buf_max);
		libusb_get_string_descriptor_ascii(dev->devh, dd.iSerialNumber,
						   (unsigned char *)serial,
						   buf_max);
	}

	return 0;
}

int fx2adc_set_vdiv(fx2adc_dev_t *dev, int channel, int vdiv)
{
	int32_t sample_clock, error, last_error = INT32_MAX;
	int millivolts = 0, closest_result = 0;
	uint8_t closest_index = 0;
	uint8_t cmd = (channel == 2) ? VDIV_CH2_REG : VDIV_CH1_REG;

	for (uint32_t i = 0; i < dev->devinfo->vdivs_size; i++) {
		millivolts = (dev->devinfo->vdivs[i][0] * 1000) / dev->devinfo->vdivs[i][1];

		error = millivolts - vdiv;

		if (abs(error) < last_error) {
			closest_index = i;
			last_error = abs(error);
			closest_result = millivolts;
		}
	}

	if (closest_result != vdiv) {
		fprintf(stderr, "Voltage divider %d mV not supported by hardware,"
			"using closest match: %d mV\n",
			vdiv, closest_result);
	}

	dev->vdiv = millivolts;

	return fx2adc_write_control(dev, cmd, vdiv_reg[closest_index]);
}

int fx2adc_get_vdiv(fx2adc_dev_t *dev)
{
	if (!dev)
		return 0;

	return dev->vdiv;
}

int fx2adc_set_sample_rate(fx2adc_dev_t *dev, uint32_t samp_rate, bool ext_clock)
{
	int r = 0;
	const uint64_t samplerate_values[] = {SAMPLERATE_VALUES};
	const uint8_t samplerate_regs[] = {SAMPLERATE_REGS};
	uint32_t i;
	int32_t error, last_error = INT32_MAX;
	uint8_t closest_index = 0;

	if (!dev)
		return -1;

	if (ext_clock) {
		fprintf(stderr, "Using external clock source\n");
		if (dev->clockgen_present) {
			si5351_SetupCLK0(samp_rate, SI5351_DRIVE_STRENGTH_8MA);
			si5351_EnableOutputs(1);
		}
		fx2adc_write_control(dev, USE_EXTERNAL_CLK, 1);
		r = fx2adc_write_control(dev, SAMPLERATE_REG, 0);
		dev->rate = samp_rate;
	} else {
		//fx2adc_write_control(dev, USE_EXTERNAL_CLK, 0);

		for (i = 0; i < ARRAY_SIZE(samplerate_values); i++) {
			error = (int32_t)samplerate_values[i] - samp_rate;

			if (abs(error) < last_error) {
				closest_index = i;
				last_error = abs(error);
			}
		}

		if (samplerate_values[closest_index] != samp_rate) {
			fprintf(stderr, "Sample rate  %d not supported by internal clock,"
					"using closest match: %ld\n",
					samp_rate, samplerate_values[closest_index]);
		}

		r = fx2adc_write_control(dev, SAMPLERATE_REG, samplerate_regs[closest_index]);
		dev->rate = samplerate_values[closest_index];
	}

	return r;
}

uint32_t fx2adc_get_sample_rate(fx2adc_dev_t *dev)
{
	if (!dev)
		return 0;

	return dev->rate;
}

static const fx2adc_devinfo_t *find_known_device(uint16_t vid, uint16_t pid, uint16_t prod_ver, bool *configured)
{
	unsigned int i;
	const fx2adc_devinfo_t *device = NULL;

	for (i = 0; i < sizeof(dev_profiles)/sizeof(fx2adc_devinfo_t); i++ ) {
		if (dev_profiles[i].orig_vid == vid
		 && dev_profiles[i].orig_pid == pid) {
			/* Device matches the pre-firmware profile */
			device = &dev_profiles[i];
			if (configured)
				*configured = false;
			break;
		} else if (dev_profiles[i].fw_vid == vid
			&& dev_profiles[i].fw_pid == pid
			&& dev_profiles[i].fw_prod_ver == prod_ver) {
			/* Device matches the post-firmware profile */
			device = &dev_profiles[i];
			if (configured)
				*configured = true;
			break;
		}
	}

	return device;
}

uint32_t fx2adc_get_device_count(void)
{
	int i,r;
	libusb_context *ctx;
	libusb_device **list;
	uint32_t device_count = 0;
	struct libusb_device_descriptor dd;
	ssize_t cnt;

	r = libusb_init(&ctx);
	if(r < 0)
		return 0;

	cnt = libusb_get_device_list(ctx, &list);

	for (i = 0; i < cnt; i++) {
		libusb_get_device_descriptor(list[i], &dd);

		if (find_known_device(dd.idVendor, dd.idProduct, dd.bcdDevice, NULL))
			device_count++;
	}

	libusb_free_device_list(list, 1);

	libusb_exit(ctx);

	return device_count;
}

const char *fx2adc_get_device_name(uint32_t index)
{
	int i,r;
	libusb_context *ctx;
	libusb_device **list;
	struct libusb_device_descriptor dd;
	const fx2adc_devinfo_t *device = NULL;
	uint32_t device_count = 0;
	ssize_t cnt;

	r = libusb_init(&ctx);
	if(r < 0)
		return "";

	cnt = libusb_get_device_list(ctx, &list);

	for (i = 0; i < cnt; i++) {
		libusb_get_device_descriptor(list[i], &dd);

		device = find_known_device(dd.idVendor, dd.idProduct, dd.bcdDevice, NULL);

		if (device) {
			device_count++;

			if (index == device_count - 1)
				break;
		}
	}

	libusb_free_device_list(list, 1);
	libusb_exit(ctx);

	if (device)
		return device->model;
	else
		return "";
}

int fx2adc_get_device_usb_strings(uint32_t index, char *manufact,
				   char *product, char *serial)
{
	int r = -2;
	int i;
	libusb_context *ctx;
	libusb_device **list;
	struct libusb_device_descriptor dd;
	const fx2adc_devinfo_t *device = NULL;
	fx2adc_dev_t devt;
	uint32_t device_count = 0;
	ssize_t cnt;

	r = libusb_init(&ctx);
	if(r < 0)
		return r;

	cnt = libusb_get_device_list(ctx, &list);

	for (i = 0; i < cnt; i++) {
		libusb_get_device_descriptor(list[i], &dd);

		device = find_known_device(dd.idVendor, dd.idProduct, dd.bcdDevice, NULL);

		if (device) {
			device_count++;

			if (index == device_count - 1) {
				r = libusb_open(list[i], &devt.devh);
				if (!r) {
					r = fx2adc_get_usb_strings(&devt,
								   manufact,
								   product,
								   serial);
					libusb_close(devt.devh);
				}
				break;
			}
		}
	}

	libusb_free_device_list(list, 1);
	libusb_exit(ctx);

	return r;
}

int fx2adc_get_index_by_serial(const char *serial)
{
	int i, cnt, r;
	char str[256];

	if (!serial)
		return -1;

	cnt = fx2adc_get_device_count();

	if (!cnt)
		return -2;

	for (i = 0; i < cnt; i++) {
		r = fx2adc_get_device_usb_strings(i, NULL, NULL, str);
		if (!r && !strcmp(serial, str))
			return i;
	}

	return -3;
}

void fx2adc_init_hardware(fx2adc_dev_t *dev)
{
	int r;
	uint8_t vdiv_index = vdiv_reg[dev->devinfo->vdivs_size - 1];

	/* for now, only use one channel */
	fx2adc_write_control(dev, CHANNELS_REG, 1);

	/* write smallest possible voltage range as default */
	fx2adc_write_control(dev, VDIV_CH1_REG, vdiv_reg[dev->devinfo->vdivs_size - 1]);
	dev->vdiv = (dev->devinfo->vdivs[vdiv_index][0] * 1000) / dev->devinfo->vdivs[vdiv_index][1];

	/* select AC coupling if available on hardware */
	if (dev->devinfo->has_coupling)
		fx2adc_write_control(dev, COUPLING_REG, 1);

	r = si5351_Init(dev, 0);

	if (r < 0) {
		dev->clockgen_present = false;
	} else {
		dev->clockgen_present = true;
		fprintf(stderr, "Found external clock generator: Si5351 via I2C\n");
	}
}

int fx2adc_open(fx2adc_dev_t **out_dev, uint32_t index)
{
	int r;
	int i;
	libusb_device **list = NULL;
	fx2adc_dev_t *dev = NULL;
	const fx2adc_devinfo_t *devinfo = NULL;
	libusb_device *device = NULL;
	uint32_t device_count = 0;
	struct libusb_device_descriptor dd;
	uint8_t reg;
	ssize_t cnt;
	int wait_for_reenumeration = 0;
	bool is_configured = false;

	dev = malloc(sizeof(fx2adc_dev_t));
	if (NULL == dev)
		return -ENOMEM;

	memset(dev, 0, sizeof(fx2adc_dev_t));

	r = libusb_init(&dev->ctx);
	if (r < 0) {
		free(dev);
		return -1;
	}

	dev->dev_lost = 1;

	do {
		if (list) {
			libusb_free_device_list(list, 1);
			list = NULL;
		}

		cnt = libusb_get_device_list(dev->ctx, &list);

		for (i = 0; i < cnt; i++) {
			device = list[i];

			devinfo = NULL;
			libusb_get_device_descriptor(list[i], &dd);
			devinfo = find_known_device(dd.idVendor, dd.idProduct, dd.bcdDevice, &is_configured);

			if (devinfo) {
				device_count++;
			}

			if (index == device_count - 1)
				break;

			device = NULL;
		}

		if (!device) {
			if (wait_for_reenumeration && (wait_for_reenumeration < 5)) {
				usleep(500000);
				continue;
			}
			r = -1;
			goto err;
		}

		if (!is_configured) {
			device_count = 0;

			if (wait_for_reenumeration > 5) {
				fprintf(stderr, "Loading firmware failed, aborting\n");
				r = -1;
				goto err;
			}

			if (wait_for_reenumeration) {
				wait_for_reenumeration++;
				usleep(500000);
				continue;
			}

			fprintf(stderr, "Device is not configured, loading firmware\n");
			r = ezusb_upload_firmware(device, 1, devinfo->firmware);
			if (r < 0)
				goto err;

			/* wait for re-enumeration */
			wait_for_reenumeration++;
			continue;
		} else
			wait_for_reenumeration = 0;

		r = libusb_open(device, &dev->devh);
		if (r < 0) {
			fprintf(stderr, "usb_open error %d\n", r);
			if(r == LIBUSB_ERROR_ACCESS)
				fprintf(stderr, "Please fix the device permissions, e.g. "
				"by installing the udev rules file fx2adc.rules\n");
			goto err;
		}

		fprintf(stderr, "Opened %s %s\n", devinfo->vendor, devinfo->model);

		if (list) {
			libusb_free_device_list(list, 1);
			list = NULL;
		}

		if (libusb_kernel_driver_active(dev->devh, 0) == 1) {
			dev->driver_active = 1;

#ifdef DETACH_KERNEL_DRIVER
			if (!libusb_detach_kernel_driver(dev->devh, 0)) {
				fprintf(stderr, "Detached kernel driver\n");
			} else {
				fprintf(stderr, "Detaching kernel driver failed!");
				goto err;
			}
#else
		fprintf(stderr, "\nKernel driver is active, or device is "
				"claimed by second instance of libfx2adc."
				"\nIn the first case, please either detach"
				" or blacklist the kernel module,\n"
				" or enable automatic"
				" detaching at compile time.\n\n");
#endif
	}

		r = libusb_claim_interface(dev->devh, 0);
		if (r < 0) {
			fprintf(stderr, "usb_claim_interface error %d\n", r);
			goto err;
		}
	} while (wait_for_reenumeration);



	dev->rate = DEFAULT_SAMPLERATE;
	dev->dev_lost = 0;

	/* Get device manufacturer and product id */
	r = fx2adc_get_usb_strings(dev, dev->manufact, dev->product, NULL);

found:
	//libusb_reset_device(dev->devh);
	dev->devinfo = devinfo;
	*out_dev = dev;

	fx2adc_init_hardware(dev);

	return 0;
err:
	if (dev) {
		if (list)
			libusb_free_device_list(list, 1);
		if (dev->devh)
			libusb_close(dev->devh);

		if (dev->ctx)
			libusb_exit(dev->ctx);

		free(dev);
	}

	return r;
}

int fx2adc_close(fx2adc_dev_t *dev)
{
	if (!dev)
		return -1;

	if(!dev->dev_lost) {

		/* stop sampling */
		fx2adc_write_control(dev, TRIGGER_REG, 0);
		fprintf(stderr, "Stopped sampling.\n");

		if (dev->clockgen_present)
			si5351_EnableOutputs(0);

		/* block until all async operations have been completed (if any) */
		while (FX2ADC_INACTIVE != dev->async_status)
			usleep(1000);
	}

	libusb_release_interface(dev->devh, 0);

#ifdef DETACH_KERNEL_DRIVER
	if (dev->driver_active) {
		if (!libusb_attach_kernel_driver(dev->devh, 0))
			fprintf(stderr, "Reattached kernel driver\n");
		else
			fprintf(stderr, "Reattaching kernel driver failed!\n");
	}
#endif

	libusb_close(dev->devh);
	libusb_exit(dev->ctx);
	free(dev);

	return 0;
}

static inline uint8_t bitrev(uint8_t byte)
{
#if defined(__clang__)
	return __builtin_bitreverse8(byte);
#else
	const uint8_t lut[16] = { 0x0, 0x8, 0x4, 0xc, 0x2, 0xa, 0x6, 0xe,
				  0x1, 0x9, 0x5, 0xd, 0x3, 0xb, 0x7, 0xf };

	return (lut[byte & 0xf] << 4) | lut[byte >> 4];
#endif
}

static void LIBUSB_CALL _libusb_callback(struct libusb_transfer *xfer)
{
	fx2adc_dev_t *dev = (fx2adc_dev_t *)xfer->user_data;

	if (LIBUSB_TRANSFER_COMPLETED == xfer->status) {
		if (dev->cb) {
			if (dev->devinfo->ch1_bitreversed) {
				//FIXME distinguish between single and dual-channel mode

				/* the Hantek PSO2020 has the ADC data lines of
				 * channel 1 connected bit-reversed */
				for (int i = 0; i < xfer->actual_length; i++)
					xfer->buffer[i] = bitrev(xfer->buffer[i]);
			}

			dev->cb(xfer->buffer, xfer->actual_length, dev->cb_ctx);
		}

		libusb_submit_transfer(xfer); /* resubmit transfer */
		dev->xfer_errors = 0;
	} else if (LIBUSB_TRANSFER_CANCELLED != xfer->status) {
#ifndef _WIN32
		if (LIBUSB_TRANSFER_ERROR == xfer->status)
			dev->xfer_errors++;

		if (dev->xfer_errors >= dev->xfer_buf_num ||
		    LIBUSB_TRANSFER_NO_DEVICE == xfer->status) {
#endif
			dev->dev_lost = 1;
			fx2adc_cancel_async(dev);
			fprintf(stderr, "cb transfer status: %d, "
				"canceling...\n", xfer->status);
#ifndef _WIN32
		}
#endif
	}
}

static int _fx2adc_alloc_async_buffers(fx2adc_dev_t *dev)
{
	unsigned int i;

	if (!dev)
		return -1;

	if (!dev->xfer) {
		dev->xfer = malloc(dev->xfer_buf_num *
				   sizeof(struct libusb_transfer *));

		for(i = 0; i < dev->xfer_buf_num; ++i)
			dev->xfer[i] = libusb_alloc_transfer(0);
	}

	if (dev->xfer_buf)
		return -2;

	dev->xfer_buf = malloc(dev->xfer_buf_num * sizeof(unsigned char *));
	memset(dev->xfer_buf, 0, dev->xfer_buf_num * sizeof(unsigned char *));

#if defined(ENABLE_ZEROCOPY) && defined (__linux__) && LIBUSB_API_VERSION >= 0x01000105
	fprintf(stderr, "Allocating %d zero-copy buffers\n", dev->xfer_buf_num);

	dev->use_zerocopy = 1;
	for (i = 0; i < dev->xfer_buf_num; ++i) {
		dev->xfer_buf[i] = libusb_dev_mem_alloc(dev->devh, dev->xfer_buf_len);

		if (dev->xfer_buf[i]) {
			/* Check if Kernel usbfs mmap() bug is present: if the
			 * mapping is correct, the buffers point to memory that
			 * was memset to 0 by the Kernel, otherwise, they point
			 * to random memory. We check if the buffers are zeroed
			 * and otherwise fall back to buffers in userspace.
			 */
			if (dev->xfer_buf[i][0] || memcmp(dev->xfer_buf[i],
							  dev->xfer_buf[i] + 1,
							  dev->xfer_buf_len - 1)) {
				fprintf(stderr, "Detected Kernel usbfs mmap() "
						"bug, falling back to buffers "
						"in userspace\n");
				dev->use_zerocopy = 0;
				break;
			}
		} else {
			fprintf(stderr, "Failed to allocate zero-copy "
					"buffer for transfer %d\nFalling "
					"back to buffers in userspace\n", i);
			dev->use_zerocopy = 0;
			break;
		}
	}

	/* zero-copy buffer allocation failed (partially or completely)
	 * we need to free the buffers again if already allocated */
	if (!dev->use_zerocopy) {
		for (i = 0; i < dev->xfer_buf_num; ++i) {
			if (dev->xfer_buf[i])
				libusb_dev_mem_free(dev->devh,
						    dev->xfer_buf[i],
						    dev->xfer_buf_len);
		}
	}
#endif

	/* no zero-copy available, allocate buffers in userspace */
	if (!dev->use_zerocopy) {
		for (i = 0; i < dev->xfer_buf_num; ++i) {
			dev->xfer_buf[i] = malloc(dev->xfer_buf_len);

			if (!dev->xfer_buf[i])
				return -ENOMEM;
		}
	}

	return 0;
}

static int _fx2adc_free_async_buffers(fx2adc_dev_t *dev)
{
	unsigned int i;

	if (!dev)
		return -1;

	if (dev->xfer) {
		for(i = 0; i < dev->xfer_buf_num; ++i) {
			if (dev->xfer[i]) {
				libusb_free_transfer(dev->xfer[i]);
			}
		}

		free(dev->xfer);
		dev->xfer = NULL;
	}

	if (dev->xfer_buf) {
		for (i = 0; i < dev->xfer_buf_num; ++i) {
			if (dev->xfer_buf[i]) {
				if (dev->use_zerocopy) {
#if defined (__linux__) && LIBUSB_API_VERSION >= 0x01000105
					libusb_dev_mem_free(dev->devh,
							    dev->xfer_buf[i],
							    dev->xfer_buf_len);
#endif
				} else {
					free(dev->xfer_buf[i]);
				}
			}
		}

		free(dev->xfer_buf);
		dev->xfer_buf = NULL;
	}

	return 0;
}

int fx2adc_read(fx2adc_dev_t *dev, fx2adc_read_cb_t cb, void *ctx,
			  uint32_t buf_num, uint32_t buf_len)
{
	unsigned int i;
	int r = 0;
	struct timeval tv = { 1, 0 };
	struct timeval zerotv = { 0, 0 };
	enum fx2adc_async_status next_status = FX2ADC_INACTIVE;

	if (!dev)
		return -1;

	if (FX2ADC_INACTIVE != dev->async_status)
		return -2;

	dev->async_status = FX2ADC_RUNNING;
	dev->async_cancel = 0;

	dev->cb = cb;
	dev->cb_ctx = ctx;

	if (buf_num > 0)
		dev->xfer_buf_num = buf_num;
	else
		dev->xfer_buf_num = DEFAULT_BUF_NUMBER;

	if (buf_len > 0 && buf_len % 512 == 0) /* len must be multiple of 512 */
		dev->xfer_buf_len = buf_len;
	else
		dev->xfer_buf_len = DEFAULT_BUF_LENGTH;

	_fx2adc_alloc_async_buffers(dev);

	for(i = 0; i < dev->xfer_buf_num; ++i) {
		libusb_fill_bulk_transfer(dev->xfer[i],
					  dev->devh,
					  FX2LAFW_EP_IN,
					  dev->xfer_buf[i],
					  dev->xfer_buf_len,
					  _libusb_callback,
					  (void *)dev,
					  BULK_TIMEOUT);

		r = libusb_submit_transfer(dev->xfer[i]);
		if (r < 0) {
			fprintf(stderr, "Failed to submit transfer %i\n"
					"Please increase your allowed " 
					"usbfs buffer size with the "
					"following command:\n"
					"echo 0 > /sys/module/usbcore"
					"/parameters/usbfs_memory_mb\n", i);
			dev->async_status = FX2ADC_CANCELING;
			break;
		}
	}

	/* start capture */
	fx2adc_write_control(dev, TRIGGER_REG, 1);

	while (FX2ADC_INACTIVE != dev->async_status) {
		r = libusb_handle_events_timeout_completed(dev->ctx, &tv,
							   &dev->async_cancel);
		if (r < 0) {
			/*fprintf(stderr, "handle_events returned: %d\n", r);*/
			if (r == LIBUSB_ERROR_INTERRUPTED) /* stray signal */
				continue;
			break;
		}

		if (FX2ADC_CANCELING == dev->async_status) {
			next_status = FX2ADC_INACTIVE;

			if (!dev->xfer)
				break;

			for(i = 0; i < dev->xfer_buf_num; ++i) {
				if (!dev->xfer[i])
					continue;

				if (LIBUSB_TRANSFER_CANCELLED !=
						dev->xfer[i]->status) {
					r = libusb_cancel_transfer(dev->xfer[i]);
					/* handle events after canceling
					 * to allow transfer status to
					 * propagate */
#ifdef _WIN32
					Sleep(1);
#endif
					libusb_handle_events_timeout_completed(dev->ctx,
									       &zerotv, NULL);
					if (r < 0)
						continue;

					next_status = FX2ADC_CANCELING;
				}
			}

			if (dev->dev_lost || FX2ADC_INACTIVE == next_status) {
				/* handle any events that still need to
				 * be handled before exiting after we
				 * just cancelled all transfers */
				libusb_handle_events_timeout_completed(dev->ctx,
								       &zerotv, NULL);
				break;
			}
		}
	}

	_fx2adc_free_async_buffers(dev);

	dev->async_status = next_status;

	return r;
}

int fx2adc_cancel_async(fx2adc_dev_t *dev)
{
	if (!dev)
		return -1;

	/* if streaming, try to cancel gracefully */
	if (FX2ADC_RUNNING == dev->async_status) {
		dev->async_status = FX2ADC_CANCELING;
		dev->async_cancel = 1;
		return 0;
	}

	/* if called while in pending state, change the state forcefully */
#if 0
	if (FX2ADC_INACTIVE != dev->async_status) {
		dev->async_status = FX2ADC_INACTIVE;
		return 0;
	}
#endif
	return -2;
}
