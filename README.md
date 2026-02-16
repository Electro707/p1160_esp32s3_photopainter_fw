# P1160 - ESP32S3 PhotoPainter Open Firmware

This projects attempts to make an open sane firmware for the Waveshare Photopainter, specifically the esp32-s3 version.

# Hardware
- [Waveshare ESP-S3 PhotoPainter](https://www.waveshare.com/esp32-s3-photopainter.htm)

# API Docs
The web API docs can be found in [docs/openapi.yaml](docs/openapi.yaml).

# Unit Test
The folder `tests` contains a Python test script alongside a unit test C program for the API.
To run it, you first must build the firmware like normal, then run the make file. Right now only 1 target exists, `make test_apis`.

# Credits
A decent amount of this firmware comes from Waveshare's own example, especially on the eink initialization and pmic stuff.

The PMIC stuff, which was partially copied from Waveshare's example, is from the [XPowerLibs](https://github.com/lewisxhe/XPowersLib) library.

# Future Todo

- pmic rework
    - The PMIC uses the [XPowerLibs](https://github.com/lewisxhe/XPowersLib) library, which is what Waveshare's example "came with". I would like to make my own library.
- Moving eink.c into it's own component
    - Might have to make another component for the sane defines, such as u8
- Low-Level driving of SPI
    - Turns out that using esp-idf's spi library with DMA, the thing sets up the linked list required for the DMA per transaction: `spi_master.c->spi_device_polling_start()->spi_new_trans()->s_spi_prepare_data()->s_spi_dma_prepare_data()`
    As well as the entire SPI transaction settings. That among other things like always checking if a transaction is valid.
    I get it for a general purpose "any task can speak to spi" sort of setup and for a developer safe api, but given this is a controlled system it is a todo to just talk to the low-level drivers myself, as what it's doing is a bit much. Plus a learning opportunity for me. 
    This will be a last priority, as the saying goes "don't fix what ain't broken"


# License

This project is licensed under GPLv3, except for the directory `components/xpowerlibs`, which is under the MIT license.