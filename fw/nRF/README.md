# WULPUS source files for Bluetooth Chipsets

This directory contains the source firmware files for the Bluetooth Chip on the WULPUS acquisition PCB.
Current possible variants include:

- **nRF52**832
- **nRF54**L15
- **nRF52**840 Dongle

# How to get started?

- Install nRF Connect SDK https://www.nordicsemi.com/Products/Development-software/nRF-Connect-SDK (follow the [Documentation](https://docs.nordicsemi.com/bundle/ncs-latest/page/nrf/installation/install_ncs.html))

# Flash the firmware

- in VSCode in the **nRF Connect** Plugin, add a new Build Configuration with the board you're using
- Click the Flash button


---

# For developers:

The `mesh`-implementation in the nRF firmware can be disabled, by settings `CONFIG_BT_MESH=n` in the `prj.conf` or the respective `{board}.conf`-file.

