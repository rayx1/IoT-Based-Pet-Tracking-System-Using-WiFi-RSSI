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
   - No extra SMTP library is required.
   - The sketches use ESP8266 core libraries plus built-in `WiFiClientSecure`.

4. Create local config files
   - Copy `firmware/pet_node/secrets.example.h` to `firmware/pet_node/secrets.h`.
   - Copy `firmware/home_node/secrets.example.h` to `firmware/home_node/secrets.h`.
   - Keep both `secrets.h` files private; they are ignored by Git.

5. Decide on the pet roster
   - List each pet you want to track.
   - Pick a unique protocol ID per pet, e.g. `PET-001`, `PET-002`, `PET-003`.
   - Pick a friendly display name per pet, e.g. `Bella`, `Max`.

6. Upload each Pet Node once
   - Open `firmware/pet_node/pet_node.ino`.
   - For each pet node, edit `firmware/pet_node/secrets.h` and set `PET_ID_VALUE` to that pet's unique ID, plus the WiFi credentials.
   - Upload to that pet node board, open Serial Monitor at `115200`, copy the `Pet Node MAC`.
   - Repeat for every pet node, recording `(MAC, PET_ID, display name)` for each.
   - Tip: if you flash multiple pet nodes from the same machine, swap `secrets.h` (or maintain `secrets-pet1.h` / `secrets-pet2.h` snapshots) between uploads.

7. Configure the Home Node roster
   - Open `firmware/home_node/secrets.h`.
   - Edit `PET_NODE_LIST`. Add one entry per pet: `{ {mac bytes}, "PROTOCOL_PET_ID", "Display Name" }`.
   - Lines are continued with a trailing `\`. The home node sizes itself from the list automatically.
   - Edit the WiFi and SMTP values in the same file.

8. Upload Home Node
   - Upload the sketch to the home NodeMCU.
   - Open Serial Monitor at `115200`.
   - Note the printed `Home Node MAC` and `Home Node IP`.
   - Confirm the startup log lists every pet you expect.

9. Paste Home MAC into each Pet Node
   - Edit `HOME_NODE_MAC_BYTES` in `firmware/pet_node/secrets.h`.
   - Re-upload each pet node board.

10. Verify on dashboard
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
