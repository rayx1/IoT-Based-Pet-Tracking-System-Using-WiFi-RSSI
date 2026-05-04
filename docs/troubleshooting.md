# Troubleshooting

## ESP-NOW init failed

- Confirm the board selected in Arduino IDE is an ESP8266 board, not ESP32.
- Make sure both sketches use the ESP8266 Arduino core.
- Reboot the board and try again.
- Check Serial Monitor for WiFi startup errors before ESP-NOW initialization.

## Pet node not detected

- Verify the Home Node MAC was copied correctly into each `pet_node` config.
- Verify each Pet Node MAC was added to `PET_NODE_LIST` on the Home Node.
- Confirm all boards connect to the same WiFi router SSID.
- Check that all boards report the same WiFi channel in Serial Monitor.
- Move the boards closer together during first testing.

## "Rejected packet ... pet ID mismatch"

- The MAC matches an entry in `PET_NODE_LIST`, but the pet node's `PET_ID_VALUE` does not equal the `PROTOCOL_PET_ID` you wrote in that entry.
- Open the offending pet node sketch, set `PET_ID_VALUE` to the same string used in the home node entry, and re-upload.

## "Ignoring packet from unknown MAC"

- A pet node is sending but its MAC is not in `PET_NODE_LIST` on the home node.
- Add an entry for that MAC, re-upload the home node, or fix the typo in the existing entry.

## RSSI not updating for one specific pet

- Confirm that pet's MAC byte sequence in `PET_NODE_LIST` exactly matches what the pet's Serial Monitor prints (case-insensitive, but every byte must match).
- The dashboard shows `RSSI sample: Stale` when no matching frame has been seen in the last 3 seconds.
- If only one of several pets shows stale RSSI while others update, the entry for that pet has the wrong MAC bytes.

## RSSI stalls for ALL pets after long uptime

- ESP-NOW + STA + promiscuous sniffer + WebServer share one radio. Some firmware combinations destabilise after hours of load.
- Try power-cycling the home node.
- If the issue is reproducible, reduce dashboard refresh rate (the meta-refresh in the HTML is currently 3 s) or reduce per-pet `SEND_INTERVAL_MS` on the pet nodes to lighten the air.
- Last-resort: comment out `startPromiscuousSniffer()` and accept losing the RSSI signal in exchange for stability.

## Email not sending

- Re-check `SMTP_HOST`, `SMTP_PORT`, sender email, and app password.
- Confirm your WiFi internet access is working.
- Verify the email library is installed correctly.
- Read the Serial Monitor SMTP error text.

## Email timestamps are wildly wrong

- `SMTP_TIME_GMT_OFFSET_VALUE` units depend on your installed `ESP-Mail-Client` version.
- Older builds expect seconds (`19800` = IST). Newer builds expect hours (`5.5` = IST).
- Set it to `0` for UTC if the wrong unit causes nonsensical dates.

## Gmail authentication error

- Use a Gmail App Password, not your normal account password.
- Enable 2-Step Verification on the Google account first.
- Confirm the sender email matches the account used to create the App Password.

## Web dashboard not opening

- Confirm the Home Node connected to WiFi and printed an IP address.
- Make sure your phone or laptop is on the same network.
- Open the exact IP shown in Serial Monitor.
- If needed, reboot the Home Node and try again.

## Silence button does nothing / returns 405

- The endpoint is now `POST /silence`. Hitting it via a plain `GET` (e.g. typing the URL in the browser bar) returns `405 Use POST`.
- Use the dashboard's silence form. If you scripted a curl call, use `curl -X POST http://HOME_IP/silence`.
- If you set `DASHBOARD_TOKEN_VALUE`, you must also pass `-d "token=YOUR_TOKEN"`.

## Buzzer always ON

- Increase `RSSI_THRESHOLD_DBM` because the current threshold may be too strict.
- Confirm the buzzer wiring is connected to `D5` and `GND`.
- Check if any pet's packet timeout is being triggered.
- Check whether the dashboard silence state is active.
- Some buzzer modules are active-low; if needed, invert the buzzer logic in `writeBuzzer`.

## Buzzer pattern with multiple pets

- The buzzer reflects the **most urgent pet**: any pet `Lost` produces fast beeps; otherwise any pet `Far` produces slow beeps.
- A single silence press silences the buzzer for 2 minutes regardless of how many pets are alerting.

## Wrong WiFi channel

- All boards must lock to the same router channel.
- Make sure all boards connect to the same SSID before ESP-NOW exchange.
- If your router uses band steering or changing channels, test with a fixed 2.4 GHz channel.
- Reboot all nodes after router channel changes.
