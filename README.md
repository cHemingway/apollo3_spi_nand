### Apollo 3 Blue VSCode Template

Standalone "out of tree" template for the [Ambiq Apollo3 Blue](https://www.fujitsu.com/uk/microsite/feeu/products/semiconductors/ulp-mcus-rtcs/ulp-mcu/apollo3blue/), with [Sparkfun Edge](https://www.sparkfun.com/products/15170) BSP (pin names etc), and VSCode build/debug/paths

Designed to be used on WSL/Linux

Assumes SDK + Sparkfun BSP is in same directory as this folder

### Usage

* Download Ambiq SDK and unzip
* Put Sparkfun BSP
* Put this template in same root folder as Ambiq SDK
* Edit your compiler path in c_cpp_properties.json for better intellisense (e.g. hints from your cross-compiled stdio.h, not the system compiler)