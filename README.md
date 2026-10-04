# ESP32-CYD 2432S028 Wi-Fi CSI Sensing Firmware

Target:
- ESP32-2432S028
- ESP32-WROOM-32
- ILI9341 320x240 TFT
- XPT2046 resistive touch

This project is designed to be built in GitHub Actions so the local PC does not need ESP-IDF/Arduino/PlatformIO.

## Build
1. Create a GitHub repository.
2. Upload this entire project.
3. Open Actions.
4. Run "Build CYD CSI firmware".
5. Download the artifact `CYD-2432S028-CSI-firmware`.

## Flash
Use Espressif ESP Launchpad in Chrome/Edge:
https://espressif.github.io/esp-launchpad/

The DIY mode accepts pre-built firmware images.

IMPORTANT:
This is a development build. The browser can flash the compiled binaries, but it cannot build the firmware itself.

## Hardware pins
Display:
MOSI 13, MISO 12, SCLK 14, CS 15, DC 2, BL 21

Touch:
CLK 25, MOSI 32, MISO 39, CS 33, IRQ 36

The firmware scans nearby Wi-Fi networks, lets the user select an SSID, accepts a password through the touchscreen keyboard, connects, enables ESP32 CSI, and displays a live CSI activity graph.

CSI is Wi-Fi channel sensing; it is not a camera and does not literally see through walls.
