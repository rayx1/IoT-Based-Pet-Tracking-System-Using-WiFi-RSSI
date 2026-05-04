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

4. Decide on the pet roster
   - List each pet you want to track.
   - Pick a unique protocol ID per pet, e.g. `PET-001`, `PET-002`, `PET-003`.
   - Pick a friendly display name per pet, e.g. `Bella`, `Max`.

5. Upload each Pet Node once
   - Open `firmware/pet_node/pet_node.ino`.
   - For each pet node, edit `PET_ID_VALUE` to that pet's unique ID (or set it in `firmware/pet_node/secrets.h`).
   - Edit WiFi placeholders or create `firmware/pet_node/secrets.h` from `secrets.example.h`.
   - Upload to that pet node board, open Serial Monitor at `115200`, copy the `Pet Node MAC`.
   - Repeat for every pet node, recording `(MAC, PET_ID, display name)` for each.

6. Configure the Home Node roster
   - Open `firmware/home_node/home_node.ino`.
   - Edit `PET_NODE_LIST` (or set it in `firmware/home_node/secrets.h`).
   - Add one entry per pet: `{ {mac bytes}, "PROTOCOL_PET_ID", "Display Name" }`.
   - Lines are continued with a trailing `\`. The home node sizes itself from the list automatically.
   - Edit WiFi and SMTP placeholders.

7. Upload Home Node
   - Upload the sketch to the home NodeMCU.
   - Open Serial Monitor at `115200`.
   - Note the printed `Home Node MAC` and `Home Node IP`.
   - Confirm the startup log lists every pet you expect.

8. Paste Home MAC into each Pet Node
   - Open `firmware/pet_node/pet_node.ino`.
   - Replace `HOME_NODE_MAC_BYTES`, or edit `firmware/pet_node/secrets.h`.
   - Re-upload each pet node board (each one can keep its own `secrets.h` with its own `PET_ID_VALUE`).

9. Verify on dashboard
   - Read the Home Node IP from Serial Monitor.
   - Open `http://HOME_NODE_IP/` in a browser on the same network.
   - Confirm one card appears per pet and packets begin arriving.

## Adding a Pet Later

1. Flash a new Pet Node with a fresh unique `PET_ID_VALUE` (e.g. `PET-004`).
2. Copy its MAC from Serial Monitor.
3. Append a new entry to `PET_NODE_LIST` on the Home Node, keeping the trailing `\` on the previous line.
4. Re-upload the Home Node.
5. Refresh the dashboard.

## Removing a Pet

1. Delete that pet's entry from `PET_NODE_LIST`.
2. Re-upload the Home Node.
3. The pet's card disappears and any further packets from that MAC are ignored.
