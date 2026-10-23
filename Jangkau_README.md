# Jangkau smart step path: Wokwi setup

## 1. Make the simulation (about 5 minutes)
1. Go to https://wokwi.com, sign in (free), choose **New Project → ESP32 → Arduino**.
2. Open the `sketch.ino` tab, delete everything, paste in **sketch.ino**.
3. Open the `diagram.json` tab, delete everything, paste in **diagram.json**. The parts and wires appear by themselves.
4. Click **Library Manager** and add: `Adafruit NeoPixel`, `PubSubClient`, `LiquidCrystal I2C`, `HX711`, `DHT sensor library for ESPx`.
   (Or create a `libraries.txt` tab and paste **libraries.txt**.)
5. In `sketch.ino`, change `TEAM_ID` to something unique, e.g. `jk-unimas-nullpointers`.
6. Press the green **Play** button. Save the project so you get a share link.

## 2. Use it
| Do this in Wokwi | What it stands for | What happens |
|---|---|---|
| Slide switch | Clock / daylight sensor | Day = lamps off. Night = lamps at a calm 20% |
| Click **Tile A** load cell, drag weight above 20 kg, then back to 0 | Someone steps on a smart tile | Step counted, tile's yellow LED glows (powered by harvested energy), lamps ahead brighten one by one |
| Then do Tile B, then Tile C | The person walks down the path | Lights follow the walker tile by tile, then dim behind them |
| Green button | Neighbour quest finished | Path glows gold |
| Knob past 1.00 m | Pond water rising | Flood alarm: red LED + buzzer, quests paused, path fully lit |
| Click the DHT22, change temperature/humidity | Weather sensor | Dashboard shows temperature, humidity, "feels like" and advice |
| Last 3 lamps | Forest edge | Always low and amber (quiet edge for wildlife) |

LCD line 1 = mode and steps. Line 2 switches every 3 s between (energy saved %, quests, joules harvested) and (temperature, humidity, pond level). The last character is `*` (online) or `!` (offline but still working).

## 3. Resident app and dashboard
- **app.html** is the resident app (what people see after scanning a QR code on a story post). Open it in Chrome, go to **Tonight → Connect to the live path**, type the same Team ID and press Connect. Finishing a neighbour walk then makes the path in Wokwi glow gold.
- The published version of the app (the link in the QR codes) works on any phone but cannot talk to the rig, so it shows demo data for the path.

## 4. Live township dashboard
1. Open **dashboard.html** in Chrome on a laptop.
2. Put Wokwi and the dashboard in **two windows side by side** (a hidden window gets slowed down by the browser).
3. Type the same Team ID, press **Connect**. The pill turns green: "Live from rig".
4. **Send quest from app** makes the path in Wokwi glow gold.
5. If the internet is slow while recording, switch to **Demo data**.

## 5. Demo video order
1. Night on: lamps calm at 20%.
2. Step on Tile A, then B, then C: tiles glow, lights follow the walker, dashboard counts steps and harvested energy.
3. On the resident app, finish a neighbour walk (or press "Send quest from app" on the dashboard): path glows gold.
4. Raise the temperature on the DHT22: dashboard shows heat caution.
5. Turn the knob up: flood alarm, quest blocked, red banner.
6. Point at "Lamp energy saved" vs full brightness.

## Notes for the proposal
- Harvest is an estimate (about 3 J per step). It powers each tile's own sensor and LED glow; the big lamps run on grid or solar power and save energy by dimming.
- Lamp energy is calculated in code (no power sensor simulated). Demo time runs 60× faster.
- Public test broker (broker.hivemq.com) is for the demo only; a pilot would use a private broker with TLS.
- The same firmware runs on a real ESP32 with HX711 load-cell boards and a DHT22.
