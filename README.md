# fx2adc


fx2adc is a cross-platform userspace library and set of commandline applications for acquiring a continuous, uninterrupted stream of samples from USB oscilloscopes that are based on the [Cypress FX2](https://www.infineon.com/cms/de/product/universal-serial-bus/usb-2.0-peripheral-controllers/ez-usb-fx2lp-fx2g2-usb-2.0-peripheral-controller/) high-speed USB interface chip  and the [AD9288](https://www.analog.com/media/en/technical-documentation/data-sheets/AD9288.pdf) ADC (or [clones](https://corebai.com/en/usb-kongzhiqi/CBM9002A-100TCG.html) [thereof](http://www.mxtronics.com/n107/n124/n181/n184/c692/attr/2630.pdf)).

Without hardware modification, these oscilloscopes can provide an 8-bit unsigned stream of samples at 30 MSPS. When an external clock generator is added with a hardware modification, 40 MSPS or even up to 45 MSPS can be reached (depending on the USB host controller and the host machine).

Currently, the following hardware is supported:

- [Hantek 6022BE](https://sigrok.org/wiki/Hantek_6022BE)
- [Hantek 6022BL](https://sigrok.org/wiki/Hantek_6022BL)
- [Hantek PSO2020](https://sigrok.org/wiki/Hantek_PSO2020)
- [Instrustar ISDS205A](https://sigrok.org/wiki/Instrustar_ISDS205A)
- [SainSmart DDS120](https://sigrok.org/wiki/SainSmart_DDS120)
- [YiXingDianZi MDSO](https://sigrok.org/wiki/YiXingDianZi_MDSO)

Except the Hantek PSO2020, all oscilloscopes listed here have two input channels. However, as the USB 2.0 bandwidth is the bottleneck (around 45 MByte/s), you only can achieve 16 MSPS per channel when both channels are active. Note that through the design of the FX2, you can only either stream CH1, or CH1 + CH2. Streaming CH2 alone is not possible.

Currently, fx2adc only supports streaming a single channel, but dual channel functionality will be added in the future.

## What can it be used for?

For the regular use-case of those oscilloscopes there is already existing software like [Sigrok](https://sigrok.org/) or [OpenHantek](https://github.com/OpenHantek/OpenHantek6022/).
This project focuses on (ab)using the hardware outside of its original intended purpose:

- Using it as RF capture hardware for [vhs-decode](https://github.com/oyvindln/vhs-decode)
- Using it as a direct sampling SDR, or even attaching a low-IF tuner chip like the R820T
- Attaching and configuring an external clock generator like the Si5351 to achieve higher sample rates (up to 45 MSPS) and to synchronize multiple devices

## Installation

### Linux

As this project shares code with [rtl-sdr](https://osmocom.org/projects/rtl-sdr/wiki/Rtl-sdr) and [osmo-fl2k](https://osmocom.org/projects/osmo-fl2k/wiki), the installation process is almost identical.

To install the build dependencies on a distribution based on Debian (e.g. Ubuntu), run the following command:

    sudo apt-get install build-essential cmake pkgconf libusb-1.0-0-dev

To build fx2adc:

    git clone https://github.com/steve-m/fx2adc.git
    mkdir fx2adc/build
    cd fx2adc/build
    cmake ../ -DINSTALL_UDEV_RULES=ON
    make -j 4
    sudo make install
    sudo ldconfig

To be able to access the USB device as non-root, the udev rules need to be installed (either use -DINSTALL_UDEV_RULES=ON or manually copy fx2adc.rules to /etc/udev/rules.d/).

Before being able to use the device as a non-root user, the udev rules need to be reloaded:

    sudo udevadm control -R
    sudo udevadm trigger

Furthermore, make sure your user is a member of the group 'plugdev'.
To make sure the group exists and add your user to it, run:

    sudo groupadd plugdev
    sudo usermod -a -G plugdev <your username>

If you haven't already been a member, you need to logout and login again for the group membership to become effective.


### Windows

fx2adc can be built with [MSYS2](https://www.msys2.org/). More information will follow, as well as a precompiled binary release.

A libusb compatible driver needs to be installed with [Zadig](https://zadig.akeo.ie/).

### Mac OS X

As rtl-sdr [works on OS X](https://github.com/macports/macports-ports/blob/master/science/rtl-sdr/Portfile), fx2adc should run as well, although it hasn't been tested yet.

## Applications

### fx2adc_file

This application records the sample data into a file. The following command captures samples at 30 MSPS to the file capture.u8 until the application is terminated with Ctrl-C:

    fx2adc_file -s 30e6 capture.u8

### fx2adc_tcp

This application is similar to rtl_tcp, it opens a listening TCP socket (by default on port 1234). For example, you can use the [GNURadio TCP source block](https://wiki.gnuradio.org/index.php?title=TCP_Source) and a UChar to Float block to view the samples and spectrum in real time using GNURadio.

### fx2adc_test

The purpose of this application is measuring the real sample rate the device outputs (and the sample rate error in PPM). It can be used to test if the device works correctly and if the clock is stable, and if there are any bottlenecks with the USB connection.

Example output:

    » fx2adc_test -p 60
    Opened Hantek PSO2020
    Reporting PPM error measurement every 60 seconds...
    Press ^C after a few minutes.
    Allocating 15 zero-copy buffers
    real sample rate: 30002251 current PPM: 75 cumulative PPM: 75
    real sample rate: 30002373 current PPM: 79 cumulative PPM: 77
    real sample rate: 30002361 current PPM: 79 cumulative PPM: 78
    real sample rate: 30002462 current PPM: 82 cumulative PPM: 79

## Credits

fx2adc is developed by Steve Markgraf, and is heavily based on rtl-sdr, osmo-fl2k and libsigrok. Furthermore, it uses a [modified version](https://github.com/steve-m/sigrok-firmware-fx2lafw/tree/fx2adc) of the [fx2lafw](http://sigrok.org/wiki/Fx2lafw).
