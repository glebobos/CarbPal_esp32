# CarbPal ESP32-C3 Captive Portal

ESP32-based digital carburetor synchronizer firmware. This repository contains the containerized development environment and source code for the ESP32-C3 Captive Portal, featuring a wildcard DNS server and dynamic telemetry dashboard.

Target board: **Seeed Studio XIAO ESP32C3** (4 MB flash, native USB CDC mapped to `/dev/ttyACM0`).

---

## Repository Structure

* `docker/` — Container definitions for building the Vite frontend and compiles firmware.
* `frontend/` — Single-page dashboard built with Vanilla TypeScript & CSS (optimized to `< 20 KB` gzipped).
* `firmware/` — ESP-IDF v5.4.4 source code containing the wild-card DNS server and file server.
* `run.sh` — Local script wrapper for compiling, flashing, and monitoring.

---

## Prerequisites

1. **Docker Engine & Docker Compose** (version 2.0+).
2. **USB Type-C Cable** (with data lines) connected to the XIAO board.
3. **Linux / WSL2 Host**.

---

## Host Configuration

### Linux USB Permissions

To access the `/dev/ttyACM0` or `/dev/ttyUSB0` ports without root permissions, add your user to the `dialout` group:

```bash
sudo usermod -a -G dialout $USER
```
*Note: You must log out and log back in (or restart your terminal session) for changes to take effect.*

### WSL2 USB Passthrough Setup

If using WSL2, the host Windows machine must attach the USB device to the Linux subsystem using `usbipd-win`:

1. Open Windows PowerShell as **Administrator** and list connected USB devices:
   ```powershell
   usbipd list
   ```
2. Locate the Seeed Studio XIAO / "USB Serial Device" bus ID (e.g., `2-3`) and bind it:
   ```powershell
   usbipd bind --busid 2-3
   ```
3. Attach the device to your WSL distribution:
   ```powershell
   usbipd attach --wsl --busid 2-3
   ```
4. Verify the device is present inside WSL:
   ```bash
   ls /dev/ttyACM*
   ```
   *(Expect `/dev/ttyACM0` to appear).*

---

## Quick Start

You can build, flash, and open the serial monitor with a single command:

```bash
./run.sh all
```

To flash at a faster speed (e.g., `921600` baud) on a specific port:
```bash
./run.sh all --port /dev/ttyACM0 --baud 921600
```

---

## Pipeline & Commands

The wrapper script `run.sh` supports the following commands:

* `./run.sh build-frontend` — Build and compress the Vite-based frontend assets.
* `./run.sh build-firmware` — Compile the firmware code and package the SPIFFS image.
* `./run.sh build` — Build both the frontend and firmware.
* `./run.sh flash` — Write the compiled binary and filesystem image to the chip.
* `./run.sh monitor` — Connect to the board's serial monitor output.
* `./run.sh flash-monitor` — Flash the device and immediately open the monitor in a single container call.
* `./run.sh clean` — Wipe the compilation caches, reset SPIFFS images, and clean Docker volumes.
* `./run.sh menuconfig` — Open the interactive ESP-IDF configuration menu.
* `./run.sh shell` — Open a bash shell in the ESP-IDF builder container for debugging.

---

## Option Flags

| Flag | Description | Default |
|---|---|---|
| `--port PORT` | Overrides the target serial port | Auto-detected |
| `--baud RATE` | Overrides the serial baud rate | `460800` |
| `--dry-run` | Prints docker commands without running them | `false` |

---

## Captive Portal Verification

1. Once flashed, search for the WiFi network named **`ESP32-Portal`** from your computer or smartphone.
2. Connect to it (Default password: **`12345678`**).
3. The device's Captive Portal should trigger a popup immediately. If it does not, open your browser and navigate to:
   ```
   http://192.168.4.1/
   ```
4. You will see the live system status card (SSID, IP, heap usage, uptime, connected client count, chipset model, and SDK version).

---

## Bootloader Recovery Mode

If flashing fails or the port `/dev/ttyACM0` disappears after a crash:
1. **Hold down the BOOT button** on the Seeed Studio XIAO ESP32C3.
2. **Plug in** (or re-plug) the USB Type-C cable.
3. **Release** the BOOT button.
4. Flash the board again using `./run.sh flash`.

---

## Changing ESP-IDF Version

To test on a different version of ESP-IDF:
1. Open [docker/Dockerfile.idf](file:///home/glebobos/projects/CarbPal_esp32/docker/Dockerfile.idf).
2. Change the default argument `ARG IDF_VERSION=v5.4.4` to your desired branch or tag.
3. Clean the project and rebuild:
   ```bash
   ./run.sh clean && ./run.sh build
   ```
