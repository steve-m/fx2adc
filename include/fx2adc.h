/*
 * fx2adc - acquire data from Cypress FX2 + AD9288 based USB scopes
 *
 * Copyright (C) 2024 by Steve Markgraf <steve@steve-m.de>
 *
 * based on librtlsdr:
 * Copyright (C) 2012-2024 by Steve Markgraf <steve@steve-m.de>
 * Copyright (C) 2012 by Dimitri Stolnikov <horiz0n@gmx.net>
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

#ifndef __FX2ADC_H
#define __FX2ADC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <fx2adc_export.h>

typedef struct fx2adc_dev fx2adc_dev_t;

FX2ADC_API uint32_t fx2adc_get_device_count(void);

FX2ADC_API const char* fx2adc_get_device_name(uint32_t index);

/*!
 * Get USB device strings.
 *
 * NOTE: The string arguments must provide space for up to 256 bytes.
 *
 * \param index the device index
 * \param manufact manufacturer name, may be NULL
 * \param product product name, may be NULL
 * \param serial serial number, may be NULL
 * \return 0 on success
 */
FX2ADC_API int fx2adc_get_device_usb_strings(uint32_t index,
					     char *manufact,
					     char *product,
					     char *serial);

/*!
 * Get device index by USB serial string descriptor.
 *
 * \param serial serial string of the device
 * \return device index of first device where the name matched
 * \return -1 if name is NULL
 * \return -2 if no devices were found at all
 * \return -3 if devices were found, but none with matching name
 */
FX2ADC_API int fx2adc_get_index_by_serial(const char *serial);

FX2ADC_API int fx2adc_open(fx2adc_dev_t **dev, uint32_t index);

FX2ADC_API int fx2adc_close(fx2adc_dev_t *dev);

/* configuration functions */

/*!
 * Get USB device strings.
 *
 * NOTE: The string arguments must provide space for up to 256 bytes.
 *
 * \param dev the device handle given by fx2adc_open()
 * \param manufact manufacturer name, may be NULL
 * \param product product name, may be NULL
 * \param serial serial number, may be NULL
 * \return 0 on success
 */
FX2ADC_API int fx2adc_get_usb_strings(fx2adc_dev_t *dev, char *manufact,
				      char *product, char *serial);

/*!
 * Set the voltage divider for a channel
 *
 * \param dev the device handle given by fx2adc_open()
 * \param channel which channel should be configured, 1 or 2
 * \param vdiv desired voltage per divsion in mV
 * \return 0 on success, -EINVAL on invalid rate
 */
FX2ADC_API int fx2adc_set_vdiv(fx2adc_dev_t *dev, int channel, int vdiv);

/*!
 * Get actual voltage divider the device is configured to.
 *
 * \param dev the device handle given by fx2adc_open()
 * \return 0 on error, voltage per division in mV otherwise
 */
FX2ADC_API int fx2adc_get_vdiv(fx2adc_dev_t *dev);

/*!
 * Set the sample rate for the device
 *
 * \param dev the device handle given by fx2adc_open()
 * \param samp_rate the sample rate to be set
 * \param ext_clock if true, use the IFCLK input insteafd of internal clock source
 *		    if a Si5351 is connected, it will be configured
 * \return 0 on success, -EINVAL on invalid rate
 */
FX2ADC_API int fx2adc_set_sample_rate(fx2adc_dev_t *dev, uint32_t rate, bool ext_clock);

/*!
 * Get actual sample rate the device is configured to.
 *
 * \param dev the device handle given by fx2adc_open()
 * \return 0 on error, sample rate in Hz otherwise
 */
FX2ADC_API uint32_t fx2adc_get_sample_rate(fx2adc_dev_t *dev);

/* streaming functions */

typedef void(*fx2adc_read_cb_t)(unsigned char *buf, uint32_t len, void *ctx);

/*!
 * Read samples from the device asynchronously. This function will block until
 * it is being canceled using fx2adc_cancel_async()
 *
 * \param dev the device handle given by fx2adc_open()
 * \param cb callback function to return received samples
 * \param ctx user specific context to pass via the callback function
 * \param buf_num optional buffer count, buf_num * buf_len = overall buffer size
 *		  set to 0 for default buffer count (15)
 * \param buf_len optional buffer length, must be multiple of 512,
 *		  should be a multiple of 16384 (URB size), set to 0
 *		  for default buffer length (16 * 32 * 512)
 * \return 0 on success
 */
FX2ADC_API int fx2adc_read(fx2adc_dev_t *dev,
				 fx2adc_read_cb_t cb,
				 void *ctx,
				 uint32_t buf_num,
				 uint32_t buf_len);

/*!
 * Cancel all pending asynchronous operations on the device.
 *
 * \param dev the device handle given by fx2adc_open()
 * \return 0 on success
 */
FX2ADC_API int fx2adc_cancel_async(fx2adc_dev_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* __FX2ADC_H */
