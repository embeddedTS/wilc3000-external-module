# Microchip WILC3000/1000 Driver as External Module

Microchip no longer maintains the `wilc` driver as an external module and it is only available in their [at91-linux tree](https://github.com/linux4sam/linux-at91) as an in-kernel module.

Additionally, the wilc1000 driver that is currently in mainline Linux kernel does not support the full feature set of the WILC3000 hardware (e.g. Bluetooth LE).

This repository seeks to re-host these files outside of their kernel for ease of building as external modules in other kernel trees.

## Building
Set either (or both) `CONFIG_WILC_SPI` or `CONFIG_WILC_SDIO` to `m` and build as an external module.

To build from kernel source tree as PWD:

```
# Be sure to set ARCH and CROSS_COMPILE as needed!
CONFIG_WILC_SPI=m INSTALL_MOD_PATH="/path/to/target/rootfs/" make M=/path/to/wilc3000-external-module modules modules_install
```


To build from the root directory of this repository:

```
# Be sure to set ARCH and CROSS_COMPILE as needed!
CONFIG_WILC_SPI=m INSTALL_MOD_PATH="/path/to/target/rootfs/" make -C /path/to/kernel/src/dir M=$PWD modules modules_install
```

## Version Information
This tree has a main trunk and a number of branches. As noted above, the main branch is a direct copy through time of the WILC3000 driver as pulled from the [at91-linux tree](https://github.com/linux4sam/linux-at91) kernel repository with tags that follow the same named tags from that repository.

In the past, the Microchip maintained driver was able to support older kernel releases, however that has been moved and the driver is only maintained in lock-step with their kernel. To make up for this, the following branching and tagging system was created.

The tag `linux4microchip-2021.10-1` is the latest snapshot from the `linux4microchip-2021.10` tag in the Microchip kernel. From that tag, the branch `2021.10-with-community-patches` is created and commits on top of that were submitted by ourselves or other members of the open-source community. Tags on that branch follow the format `linux4microchip-2021.10-1-upto-linux-...` if specific support is needed. However, the latest commit as of this moment will compile on kernels up to 6.6 and will go as low as 4.x.

The tag `linux4microchip-2024.04` is the latest snapshot from the `linux4microchip-2024.04` tag in the Microchip kernel. From that tag, the branch `2024.04-with-community-patches` is created and commits on top of that were submitted by ourselves or other members of the open-source community. Tags on that branch follow the format `linux4microchip-2024.04-community` with trailing numbers added over time.

The `linux4microchip-2024.04` tag supports kernels 6.4~6.6 and is the exact source from Microchip.

The `2024.04-with-community-patches` branch supports from kernel 6.4 up to the latest stable kernel.

When using a kernel equal to or greater than 6.4, it is recommended to use the latest tag in the `linux4microchip-2024.04-community` chain.

Older kernels should be able to use the `linux4microchip-2021.10-1-upto-linux-6.6` tag.
