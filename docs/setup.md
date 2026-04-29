# Setup Guide

## Flashing Process

1. Install Arduino IDE
   - Download and install Arduino IDE 2.x.

2. Install ESP8266 board support
   - Open Arduino IDE.
   - Go to `File -> Preferences`.
   - Add this Boards Manager URL:
     - `http://arduino.esp8266.com/stable/package_esp8266com_index.json`
   - Open `Tools -> Board -> Boards Manager`.
   - Search for `esp8266` and install the ESP8266 package.

3. Install libraries
   - Open `Tools -> Manage Libraries`.
   - Install `ESP Mail Client` by Mobizt.

4. Upload Pet Node once
   - Open `firmware/pet_node/pet_node.ino`.
   - Select a NodeMCU ESP8266 board.
   - Edit WiFi placeholders or create `firmware/pet_node/secrets.h` from `secrets.example.h`.
   - Upload the sketch.

5. Copy Pet Node MAC
   - Open Serial Monitor at `115200`.
   - Note the printed Pet Node MAC address.

6. Paste Pet MAC into Home Node
   - Open `firmware/home_node/home_node.ino`.
   - Replace `PET_NODE_MAC_BYTES`, or edit `firmware/home_node/secrets.h`.
   - Edit WiFi and SMTP placeholders.

7. Upload Home Node
   - Upload the sketch to the home NodeMCU.
   - Open Serial Monitor at `115200`.
   - Note the printed Home Node MAC address and IP address.

8. Paste Home MAC into Pet Node
   - Open `firmware/pet_node/pet_node.ino`.
   - Replace `HOME_NODE_MAC_BYTES`, or edit `firmware/pet_node/secrets.h`.

9. Upload Pet Node again
   - Confirm pet node WiFi placeholders match the same router.
   - Upload the sketch to the second NodeMCU.

10. Open dashboard
   - Read the Home Node IP from Serial Monitor.
   - Open `http://HOME_NODE_IP/` in a browser on the same network.
