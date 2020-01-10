## Apollo 3 SPI NAND Driver

Driver for Micron MT29F8G01AD SPI/QuadSPI NAND Flash for Ambiq Apollo 3

Designed to expose suitable API for Dhara file system, via `src/dhara_adaptor.c`

Unit tests written using [metal.test](https://github.com/klemens-morgenstern/metal.test) to run automatically on hardware.
Dhara file system does not have automated tests yet, see `src/main.c`

### Requirements
* Run under Unix/MacOS or WSL
* GNU Make
* arm-none-eabi toolchain on path
* GDB with Python support, e.g. `arm-none-eabi-gdb-py`
* VSCode with [cortex-debug](https://marketplace.visualstudio.com/items?itemName=marus25.cortex-debug) optional, if so, need cortex-debug extension
* Segger J-Link (on EVB), and segger GDB server.

### Known Issues
* Dhara fails on `dhara_map_resume()` call, possible issue with reads?
* QuadSPI only implemented for read, and returns wrong data as turnaround isn't working
* Blocking only at present. In future, will probably still be blocking, but with option to yield to OS scheduler.

### Usage

#### Installation 
* Download Ambiq SDK and unzip
* Put Sparkfun BSP into Ambiq SDK folder
* Put this repo in same root folder as Ambiq SDK
* Get the git submodules to install dhara and metal.test
* Edit your compiler path in c_cpp_properties.json for better intellisense (e.g. hints from your cross-compiled stdio.h, not the system compiler)

#### EVK Wiring
**Watch out**, VDD pins on Apollo3 EVK headers are not powered by default, see schematic!

**Ensure EVK I/O voltage is set correctly**


|MCU Name|MCU Pin|Flash Name|Flash Pin|
| --- | --- | -- | -- |
|1.8V|||8|
|GND|||4|
|SCLK|24|SCLK|6|
|CS|19|CS|1|
|MSPI0/MOSI|22|SI/IO0|5|
|MSPI1/MISO|26|SO/IO1|2|
|MSPI2|4|IO2|3|
|MSPI3|23|IO3|7|


#### Running tests
* Makefile is under `gcc` folder, `cd` there to run commands
* `make run_unit_tests` downloads unit tests for flash only and runs them using metal.runner
* To test dhara, build main instead (`make`)
* With VSCode, tasks are setup to debug main, and debug unit tests seperately.
    * Tests are set as default `test` task, `<CTRL><SHIFT><P> Tasks:Run Test Task `
    * Make is set as default build task
    * Debug has options for `main.c` (dhara) or unit tests