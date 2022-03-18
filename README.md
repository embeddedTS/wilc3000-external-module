# Microchip WILC3000/1000 Driver as External Module

Microchip no longer maintains the `wilc` driver as an external module and it is only available in their [at91-linux tree](https://github.com/linux4sam/linux-at91) as an in-kernel module.

Additionally, the wilc1000 driver that is currently in mainline Linux kernel does not support the full feature set of the WILC3000 hardware (e.g. Bluetooth LE).

This repository seeks to re-host these files outside of their kernel for ease of building as external modules in other kernel trees.

## Building
Set either (or both) `CONFIG_WILC_SPI` or `CONFIG_WILC_SDIO` to `m` and build as an external module.

To build from kernel source tree as PWD:

```
# Be sure to set ARCH and CROSS_COMPILE as needed!
CONFIG_WILC_SPI=m INSTALL_MOD_PATH="/path/to/target/rootfs/" make M=/path/to/wilc3000-external-module modules_install
```


To build from the root directory of this repository:

```
# Be sure to set ARCH and CROSS_COMPILE as needed!
CONFIG_WILC_SPI=m INSTALL_MOD_PATH="/path/to/target/rootfs/" make -C /path/to/kernel/src/dir M=$PWD modules_install
```
