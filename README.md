# Jangkau
**Reach your neighbours, one step at a time.**
Sime Darby Property University Hackathon 2026 · People track · The Null Pointers, UNIMAS

*Jangkau* means "step" in Iban and "reach out" in Malay. Smart step tiles, adaptive path lights, QR story posts and opt-in neighbour walks help new residents in Elmina Central Park meet each other.

## Links
- **Resident app (opened by QR codes):** https://izza521.github.io/Jangkau/Jangkau_app.html
- **Wokwi simulation:** (https://wokwi.com/projects/478632511461382145)
- **Presentation video:** [paste YouTube link]

## Files
| File | What it is |
|---|---|
| `Jangkau_sketch.ino` | ESP32 code for the step path (paste into Wokwi as `sketch.ino`) |
| `Jangkau_diagram.json` | Wokwi wiring (paste into Wokwi as `diagram.json`) |
| `Jangkau_libraries.txt` | Libraries for Wokwi (paste into Wokwi as `libraries.txt`) |
| `Jangkau_dashboard.html` | Township dashboard (open in Chrome) |
| `Jangkau_app.html` | Resident app (what the QR codes open) |
| `Jangkau_README.md` | Full setup guide |

## Quick start
1. Open the **Wokwi link**, press **▶ Play**, and flip the slide switch to **night**.
2. Open `Jangkau_dashboard.html` in Chrome, type the Team ID used in the sketch, and press **Connect**. Wait for **"Live from rig"**.
3. Open the **resident app**, go to **Tonight → Connect to the live path**, and enter the same Team ID.
4. In Wokwi, click a load cell (tile) and drag the weight above 20 kg, then back to 0. The tile glows and the lamps ahead light up.
5. In the app, do **Walk → Find a partner → Say hello and start**, then tap **I'm here** 3 times. The path glows gold.
6. Turn the knob past 1.00 m for a flood alert.

Keep the Wokwi, dashboard and app windows side by side and visible.

## Notes
- This is a simulated prototype; the same ESP32 code runs on real hardware (HX711 load cells, DHT22).
- Lamp energy is calculated in code; demo time runs 60× faster. Energy per step (about 3 J) is an estimate.
- Demo data uses a free public MQTT test broker.
- Stories and names in the app are sample content.
