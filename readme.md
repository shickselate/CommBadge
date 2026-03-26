# CommBadge

A wearable voice note device inspired by the Star Trek communicator. Press the badge to record a spoken note. Later, sync to an Android phone over Bluetooth, where notes are transcribed to text and displayed as a list.

Built on an ESP32-S3 with a real I2S microphone and amplifier, running native ESP-IDF firmware.

---

## Project status

### ✅ Milestone E1 — Badge skeleton (complete)
- Button input with debounce, short press and long press detection
- State machine: IDLE, RECORDING, SYNC_ADVERTISING
- MAX98357 I2S amplifier and speaker with Star Trek communicator chirp sounds
- All state transitions logged over serial

### ✅ Milestone E2 — Microphone capture (complete)
- INMP441 I2S MEMS microphone initialised and capturing on I2S port 1
- 16kHz mono PCM audio captured on button press
- RMS level meter visible on serial monitor, responds to sound and vibration
- Diagnosed and resolved I2S stereo interleaving on ESP-IDF v6.0
- Note: on a breadboard the mic couples strongly to vibration — expected behaviour, will improve in enclosure

### 🔄 Milestone E3 — WAV file writing (next)
- Wire up microSD breakout
- Mount filesystem
- Write valid WAV files on button press
- Confirm files are playable on PC

---

## Hardware

| Component | Part |
|---|---|
| Microcontroller | Olimex ESP32-S3-DevKit-Lipo (8MB flash, 8MB PSRAM) |
| Microphone | INMP441 I2S MEMS microphone |
| Amplifier | MAX98357 I2S Class D amp |
| Storage | MicroSD breakout + card |
| Battery | 3.7V LiPo (pending) |

### Pin assignments

| Signal | GPIO |
|---|---|
| Button | 1 |
| MAX98357 BCLK | 15 |
| MAX98357 LRCLK | 16 |
| MAX98357 DIN | 17 |
| INMP441 SCK | 5 |
| INMP441 WS | 6 |
| INMP441 SD | 4 |
| MicroSD CLK | TBD |
| MicroSD MOSI | TBD |
| MicroSD MISO | TBD |
| MicroSD CS | TBD |

---

## Firmware architecture

```
comm_badge/
  main/
    app_main.c          — boot sequence and main event loop
    config.h            — all pin assignments in one place
  components/
    button_service/     — GPIO, debounce, short/long press detection
    feedback_service/   — I2S tone generation, chirp patterns
    state_machine/      — states and legal transitions
    audio_service/      — I2S mic capture, level monitoring
```

Built with ESP-IDF v6.0. Target: `esp32s3`.

---

## Development setup

1. Install ESP-IDF v6.0 and VS Code with the ESP-IDF extension
2. Clone this repo
3. Open an ESP-IDF terminal and run `export.ps1` (Windows) to initialise the environment
4. Then:

```bash
cd comm_badge
idf.py set-target esp32s3
idf.py build
idf.py -p COM4 flash monitor
```

---

## Roadmap

### E3 — WAV file writing
Record audio to a valid WAV file on the microSD card. Press to record, press to stop, file is playable on a PC.

### E4 — Note management
Support multiple notes. Generate filenames with timestamps. Track metadata (ID, duration, size, sync status). Survive reboots cleanly.

### E5 — BLE basics
Advertise over Bluetooth LE. Android test app can connect and read badge status.

### E6 — Note index over BLE
Android app can retrieve a list of available notes from the badge.

### E7 — File transfer
Transfer WAV files from badge to Android app over BLE in chunks. App marks notes as synced.

### E8 — Power management
Idle timeout, sleep and wake. Badge becomes battery-usable rather than bench-only.

---

## Android app (planned)

A native Kotlin Android app that:
- Scans for and connects to the badge over BLE
- Downloads recorded WAV files
- Transcribes audio to text using on-device speech recognition
- Displays a chronological list of notes with full transcript and optional audio playback

---

## Final goal

A wearable badge you can tap to capture a spoken thought. Later, open the Android app, sync, and read your notes as text — no typing, no phone in hand, no always-listening microphone. Simple, reliable, and a little bit sci-fi. 🖖

