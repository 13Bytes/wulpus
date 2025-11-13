# WULPUS source files for MSP430 Ultrasound MCU firmware project
This directory contains the source firmware files for 
- MSP430FR5043 Ultrasound MCU (`fw/msp430/wulpus_msp430_firmware`) mounted on the WULPUS acquisition PCB 

# How to get started?
- Install TI Code Composer Studio (CCS) https://www.ti.com/tool/CCSTUDIO  (at time of writing version 20.2)
- Set your used board hardware-version in [hardware.h](wulpus_msp430_firmware\wulpus\hardware.h)

# Flash the firmware to the MSP430
- Open CCS, open this folder
- Select File > Open Projects from File System...
- Connect the MSP FET programmer to the US probe using the 8-pin Molex connector according to the diagram in the User Guide.
- Power the US probe with a battery or USB (Set the jumper P2 accordingly)
- Flash the code: Run > Flash Code

# License
The files in the `hw/nRF52/wulpus_msp430_firmware` directory contains third-party sources that come with their own licenses (primarily BSD and Apache 2.0 License). See the respective folders and source files' headers for the licenses used.