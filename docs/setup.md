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

4. Upload Home Node
   - Open `firmware/home_node/home_node.ino`.
   - Select a NodeMCU ESP8266 board.
   - Edit WiFi and SMTP placeholders.
   - Upload the sketch.

5. Copy Home Node MAC
   - Open Serial Monitor at `115200`.
   - Note the printed Home Node MAC address.

6. Paste MAC into Pet Node
   - Open `firmware/pet_node/pet_node.ino`.
   - Replace the `HOME_NODE_MAC` values with the Home Node MAC.

7. Upload Pet Node
   - Confirm pet node WiFi placeholders match the same router.
   - Upload the sketch to the second NodeMCU.

8. Open dashboard
   - Read the Home Node IP from Serial Monitor.
   - Open `http://HOME_NODE_IP/` in a browser on the same network.

