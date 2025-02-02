/*
 * fx2adc - acquire data from Cypress FX2 + AD9288 based USB scopes
 * fx2adc_test, test and benchmark tool
 *
 * Copyright (C) 2024 by Steve Markgraf <steve@steve-m.de>
 *
 * based on rtl_test:
 * Copyright (C) 2012-2014 by Steve Markgraf <steve@steve-m.de>
 * Copyright (C) 2012-2014 by Kyle Keen <keenerd@gmail.com>
 * Copyright (C) 2014 by Michael Tatarinov <kukabu@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
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
#include <math.h>

#ifdef __APPLE__
#include <sys/time.h>
#else
#include <time.h>
#endif

#ifndef _WIN32
#include <unistd.h>
#else
#include <windows.h>
#include "getopt/getopt.h"
#endif

#include "fx2adc.h"

#define DEFAULT_SAMPLE_RATE		30000000
#define DEFAULT_BUF_LENGTH		(16 * 16384)
#define MINIMAL_BUF_LENGTH		512
#define MAXIMAL_BUF_LENGTH		(256 * 16384)

#define MHZ(x)	((x)*1000*1000)

#define PPM_DURATION			10
#define PPM_DUMP_TIME			5

struct time_generic
/* holds all the platform specific values */
{
#ifndef _WIN32
	time_t tv_sec;
	long tv_nsec;
#else
	long tv_sec;
	long tv_nsec;
	int init;
	LARGE_INTEGER frequency;
	LARGE_INTEGER ticks;
#endif
};

static int do_exit = 0;
static fx2adc_dev_t *dev = NULL;

static uint32_t samp_rate = DEFAULT_SAMPLE_RATE;

static uint32_t total_samples = 0;
static uint32_t dropped_samples = 0;

static unsigned int ppm_duration = PPM_DURATION;

void usage(void)
{
	fprintf(stderr,
		"fx2adc_test, a test tool for FX2 based USB scopes\n\n"
		"Usage:\n"
		"\t[-s samplerate (default: 30e6 = 30 MHz)]\n"
		"\t[-d device_index (default: 0)]\n"
		"\t[-p[seconds] enable PPM error measurement (default: 10 seconds)]\n");
	exit(1);
}

#ifdef _WIN32
BOOL WINAPI
sighandler(int signum)
{
	if (CTRL_C_EVENT == signum) {
		fprintf(stderr, "Signal caught, exiting!\n");
		do_exit = 1;
		fx2adc_cancel_async(dev);
		return TRUE;
	}
	return FALSE;
}
#else
static void sighandler(int signum)
{
	signal(SIGPIPE, SIG_IGN);
	fprintf(stderr, "Signal caught, exiting!\n");
	do_exit = 1;
	fx2adc_cancel_async(dev);
}
#endif

#ifndef _WIN32
static int ppm_gettime(struct time_generic *tg)
{
	int rv = ENOSYS;
	struct timespec ts;

#ifdef __unix__
	rv = clock_gettime(CLOCK_MONOTONIC, &ts);
	tg->tv_sec = ts.tv_sec;
	tg->tv_nsec = ts.tv_nsec;
#elif __APPLE__
	struct timeval tv;

	rv = gettimeofday(&tv, NULL);
	tg->tv_sec = tv.tv_sec;
	tg->tv_nsec = tv.tv_usec * 1000;
#endif
	return rv;
}
#endif

#ifdef _WIN32
static int ppm_gettime(struct time_generic *tg)
{
	int rv;
	int64_t frac;
	if (!tg->init) {
		QueryPerformanceFrequency(&tg->frequency);
		tg->init = 1;
	}
	rv = QueryPerformanceCounter(&tg->ticks);
	tg->tv_sec = tg->ticks.QuadPart / tg->frequency.QuadPart;
	frac = (int64_t)(tg->ticks.QuadPart - (tg->tv_sec * tg->frequency.QuadPart));
	tg->tv_nsec = (long)(frac * 1000000000L / (int64_t)tg->frequency.QuadPart);
	return !rv;
}
#endif

static int ppm_report(uint64_t nsamples, uint64_t interval)
{
	double real_rate, ppm;

	real_rate = nsamples * 1e9 / interval;
	ppm = 1e6 * (real_rate / (double)samp_rate - 1.);
	return (int)round(ppm);
}

static void ppm_test(uint32_t len)
{
	static uint64_t nsamples = 0;
	static uint64_t interval = 0;
	static uint64_t nsamples_total = 0;
	static uint64_t interval_total = 0;
	static struct time_generic ppm_now;
	static struct time_generic ppm_recent;
	static enum {
		PPM_INIT_NO,
		PPM_INIT_DUMP,
		PPM_INIT_RUN
	} ppm_init = PPM_INIT_NO;

	ppm_gettime(&ppm_now);

	if (ppm_init != PPM_INIT_RUN) {
		/*
		 * Kyle Keen wrote:
		 * PPM_DUMP_TIME throws out the first N seconds of data.
		 * The dongle's PPM is usually very bad when first starting up,
		 * typically incorrect by more than twice the final value.
		 * Discarding the first few seconds allows the value to stabilize much faster.
		*/
		if (ppm_init == PPM_INIT_NO) {
			ppm_recent.tv_sec = ppm_now.tv_sec + PPM_DUMP_TIME;
			ppm_init = PPM_INIT_DUMP;
			return;
		}
		if (ppm_init == PPM_INIT_DUMP && ppm_recent.tv_sec < ppm_now.tv_sec)
			return;
		ppm_recent = ppm_now;
		ppm_init = PPM_INIT_RUN;
		return;
	}

	nsamples += (uint64_t)(len);
	interval = (uint64_t)(ppm_now.tv_sec - ppm_recent.tv_sec);
	if (interval < ppm_duration)
		return;
	interval *= 1000000000UL;
	interval += (int64_t)(ppm_now.tv_nsec - ppm_recent.tv_nsec);
	nsamples_total += nsamples;
	interval_total += interval;
	printf("real sample rate: %i current PPM: %i cumulative PPM: %i\n",
		(int)((1000000000UL * nsamples) / interval),
		ppm_report(nsamples, interval),
		ppm_report(nsamples_total, interval_total));
	ppm_recent = ppm_now;
	nsamples = 0;
}

static void fx2adc_callback(unsigned char *buf, uint32_t len, void *ctx)
{
	//printf("callback!\n");
	ppm_test(len);
}

int main(int argc, char **argv)
{
#ifndef _WIN32
	struct sigaction sigact;
#endif
	int n_read, r, opt, i;
	int sync_mode = 0;
	uint8_t *buffer;
	int dev_index = 0;
	uint32_t out_block_size = DEFAULT_BUF_LENGTH;
	int count;
	bool use_ext_clk = false;

	while ((opt = getopt(argc, argv, "d:s:p:he")) != -1) {
		switch (opt) {
		case 'd':
			dev_index = (uint32_t)atoi(optarg);
			break;
		case 's':
			samp_rate = (uint32_t)atof(optarg);
			break;
		case 'e':
			use_ext_clk = true;
			break;
		case 'p':
			if (optarg)
				ppm_duration = atoi(optarg);
			break;
		case 'h':
		default:
			usage();
			break;
		}
	}

	buffer = malloc(out_block_size * sizeof(uint8_t));

	if (dev_index < 0) {
		exit(1);
	}

	r = fx2adc_open(&dev, (uint32_t)dev_index);
	if (r < 0) {
		fprintf(stderr, "Failed to open fx2adc device #%d.\n", dev_index);
		exit(1);
	}
#ifndef _WIN32
	sigact.sa_handler = sighandler;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigaction(SIGINT, &sigact, NULL);
	sigaction(SIGTERM, &sigact, NULL);
	sigaction(SIGQUIT, &sigact, NULL);
	sigaction(SIGPIPE, &sigact, NULL);
#else
	SetConsoleCtrlHandler( (PHANDLER_ROUTINE) sighandler, TRUE );
#endif

	/* Set the sample rate */
	r = fx2adc_set_sample_rate(dev, samp_rate, use_ext_clk);
	if (r < 0)
		fprintf(stderr, "WARNING: Failed to set sample rate.\n");

	/* read back the real sample rate that might have been rounded */
	samp_rate = fx2adc_get_sample_rate(dev);

	fprintf(stderr, "Reporting PPM error measurement every %u seconds...\n", ppm_duration);
	fprintf(stderr, "Press ^C after a few minutes.\n");

	r = fx2adc_read(dev, fx2adc_callback, NULL,
			      0, out_block_size);

	if (do_exit)
		fprintf(stderr, "\nUser cancel, exiting...\n");
	else
		fprintf(stderr, "\nLibrary error %d, exiting...\n", r);

exit:
	fx2adc_close(dev);
	free (buffer);

	return r >= 0 ? r : -r;
}
