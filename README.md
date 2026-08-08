# ICE9 Bluetooth Sniffer

Wireshark-compatible all-channel Bluetooth sniffer for bladeRF, with
wideband sniffing (4-60 MHz) for HackRF and USRP.

## Dependencies

`libliquid` is the only hard dependency. By default, on Linux and MacOS this code
uses VkFFT (included) for GPU-accelerated FFT support. If you don't wish to or
can't use that, fftw3 is also a dependency.

This tool can be built without SDR libraries, but for live capture you must have
at least one of `libhackrf`, `libbladerf`, and/or `libuhd`.

### Linux

To install the base dependency:

    sudo apt install libliquid-dev

For Vulkan:

    sudo apt install libvulkan-dev vulkan-tools glslang-dev spirv-tools

Alternatively, for fftw3:

    sudo apt install libfftw3-dev

And finally, pick your favorite SDR libraries (if applicable):

    sudo apt libhackrf-dev libbladerf-dev libuhd-dev

### MacOS

On macOS, fftw3 is not required and [Homebrew](https://brew.sh/) is the
recommended package manager:

    brew install liquid-dsp hackrf libbladerf uhd

This code is untested against MacPorts. The deps can be installed with:

    port install liquid-dsp hackrf bladeRF uhd

## Building and Installing

    mkdir build
    cd build
    cmake ..
    make
    make install

The `install` target will copy the binary into `/usr/local/bin` (by
default) and will attempt to install into the system Wireshark directory
on Linux if detected. Use `ice9-bluetooth --install` to install the
binary into your local extcap dir (`$HOME/.config/wireshark/extcap`). An
`uninstall` target is also provided as a convenience.

## Building with Docker
Run the following command if you need to build the binary from a Docker container

    docker build -o build .

A `build` folder will appear with the compiled ice9-bluetooth binary. Please note that you will need to 
have the relevant libraries (e.g., libhackrf) installed on the intended system to run the binary correctly.

## Running

This tool is primarily meant to be run from within Wireshark. That said,
it is fully operable from the command line. Refer to the [usage
notes](help.txt) for full details. For a brief overview, to capture 20
channels centered on 2427 MHz and log all BLE traffic to a PCAP file:

    ./ice9-bluetooth -l -c 2427 -C 20 -w ble.pcap

To capture all channels (the default as of 23.06.0) run the following:

    ./ice9-bluetooth -l -i bladerf0 -a -w all_channels.pcap

For performance stats, add `-s`. For low-level details and info about
classic Bluetooth packets, add `-v`.

To use in Wireshark, plug in your SDR and launch Wireshark. Scroll to the
bottom of the interfaces list in the main window and you should see "ICE9
Bluetooth: hackrf-$serial" (or similar) listed. Click the wheel icon to the
left of it to configure it if you want, but the defaults should get you BLE
packets (if your system is fast enough).

### Benchmarking

There isn't a proper benchmark mode as such, but you can try
demodulating a bunch of random bytes like so:

    ./ice9-bluetooth -f /dev/urandom -s -C 20

or on macOS:

    ./ice9-bluetooth -f /dev/random -s -C 20

The channelizer will be the bottleneck. Start with 20 channels and
observe the performance relative to real time. If it is not over 100%,
lower the number of channels until it is. If it is over realtime, keep
going until you reach 96 channels.

If you do benchmark this code, please share your numbers with me!

## Design

    +----------------------------+
    |  polyphase channelizer     |
    |  1 x 20 MHz -> 20 x 2 MHz  |
    +-------+------+-------+-----+
            |      |       |
            |      |       |
            |      |       |
      +-----v----+ | +-----v-----+
      | thread 1 | | | thread 2  |
      +----------+ | +-----------+
                   |                  output:
                   | +-----------+    bursts
          ...      +-> thread 20 |
                     +-----------+

             burst queue
                  |
                  |
      +-----------v-------------+
      |    burst processor      |
      | FM demod / BT detection |
      +-------------------------+

The complex IQ samples come in from file or HackRF and are fed into a
polyphase channelizer. This splits the n MHz input into n channels at 2
MHz wide. These channelized samples are fed to n threads that each
process one channel. Each thread runs a "burst catcher", that uses
Liquid's AGC to capture bursts on the channel and feeds them via a queue
to the burst processor.

The burst processor takes the complex IQ bursts, FM demodulates them,
performs carrier frequency offset (CFO) correction, normalizes them to
roughly [-1.0, 1.0], bypasses symbol sync (for hysterical reasons), and
performs hard bit decisions. These bit buffers are fed into the
Bluetooth detectors. First we attempt to detect BR packets using
libbtbb's techniques (borrowed from Ubertooth and earlier gr-bluetooth).
If that fails, we then try to detect BLE packets.

## Bugs

This code is naughty and occasionally needs to be killed with prejudice
(`kill -9`). This happens most often in benchmark mode.

## LLM Use

Many commits authored after 2026-07-25 have been made with the use of
large language models (LLMs). I respect that this may be a controversial
disclosure, so I am making it clear up-front. For more context as to
why, observe commit frequency to this repository before and after that
date.

## Author

This code was written by Mike Ryan of ICE9 Consulting LLC. For more
information visit https://ice9.us/
