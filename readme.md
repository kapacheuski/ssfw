# TRON SSFW

This project is a firmware and tools suite for TroneSystems sensor and BLE devices, built on Zephyr RTOS. It includes embedded firmware, board configuration, and Python utilities for development, testing, and data visualization.

## Features

- Zephyr-based firmware for nRF52/nRF53 SoCs
- BLE NUS (Nordic UART Service) communication
- Sensor data acquisition (ADC, IMU, etc.)
- Python tools for weather dashboard, BLE testing, and more

---

## Building the Firmware

1. **Install Nordic nRF Connect SDK (NCS) and Zephyr dependencies**  
   Follow the [official Zephyr Getting Started Guide](https://docs.zephyrproject.org/latest/develop/getting_started/index.html) or [Nordic's guide](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/gs_installing.html).

2. **Initialize the west workspace**  
   ```sh
   west init -l .
   west update
   ```

3. **Build for your board (example: trn_ss01)**
   ```sh
   west build -b trn_ss01 dev/sstest
   ```

4. **Flash to device**

   To flash using the default runner:
   ```sh
   west flash
   ```

   **To flash using ST-Link V2 with OpenOCD:**

   1. Make sure OpenOCD is installed and your ST-Link V2 is connected to the board.
   2. Use the following command:
      ```sh
      west flash --runner openocd
      ```
   3. If you have a custom OpenOCD configuration or board, you may need to specify it with:
      ```sh
      west flash --runner openocd --openocd-board <your_board.cfg>
      ```
   4. Example for nRF52 using a generic config:
      ```sh
      west flash --runner openocd --openocd-board board/nrf52.cfg
      ```
   5. Or Use direct OpenOCD command:
      ```sh
      openocd -f interface/stlink.cfg -c 'transport select hla_swd' -f target/nrf52.cfg -c 'program dev/beacon/build/merged.hex verify reset; shutdown;'"
      ```
   This will program your device using the ST-Link V2 interface via OpenOCD.

---

## Using the Python Tools

The `tools/` directory contains Python scripts for data visualization and BLE interaction.

### Example: Sensor data Plot

1. **Install dependencies**
   ```sh
   pip install -r tools/requirements.txt
   ```

2. **Run the sensors plot dashboard**
   ```sh
   python tools/ble_plot.py sstest
   ```


### Other Scripts

- Explore other scripts in `tools/` for BLE testing, data logging, etc.
- Each script may have its own usage instructions or command-line options.

---

## Directory Structure

```
dev/         # Embedded firmware source code
boards/      # Board configuration files (.dts, .conf)
tools/       # Python utilities and scripts
readme.md    # This file
```

---

## Support

- For Zephyr/SDK issues, see [Zephyr Documentation](https://docs.zephyrproject.org/latest/)
- For TroneSystems hardware, consult your board's datasheet and pinout.

---

**Contributions and issues