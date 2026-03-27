# CommBadge

A wearable voice note device inspired by the Star Trek communicator. Press the badge to record a spoken note, press again to play it back. Later, sync to an Android phone over Bluetooth, where notes are transcribed to text and displayed as a list.

Built on an ESP32-S3 with a real I2S microphone and amplifier, running native ESP-IDF firmware.

---

## Project status

### ✅ Milestone E1 — Badge skeleton (complete)
- Button input with debounce, short press and long press detection
- State machine: IDLE, RECORDING, PLAYING, SYNC_ADVERTISING
- MAX98357 I2S amplifier and speaker
- All state transitions logged over serial

### ✅ Milestone E2 — Microphone capture (complete)
- INMP441 I2S MEMS microphone initialised and capturing on I2S port 1
- 16kHz mono PCM audio captured on button press
- RMS level meter visible on serial monitor
- Diagnosed and resolved I2S stereo interleaving on ESP-IDF v6.0
- Key finding: GND pin selection is critical — not all GND pins are electrically equivalent on this board

### ✅ Milestone E3 — WAV recording and playback (complete)
- Custom partition table: 4MB FAT partition on internal flash
- Wear-levelled FATFS mounted at `/audio` via `storage_service`
- Audio recorded to `/audio/recording.wav` on button press (overwrites each time)
- WAV header written correctly with final size on stop
- Immediate playback via MAX98357 on second button press
- Full loop working: press → speak → press → hear playback
- Speech barely intelligible at 1-8x software gain — audio quality tuning ongoing

### 🔄 Milestone E4 — Audio quality (next)
- Tune software gain and sample extraction for clean speech
- Characterise SNR on breadboard vs enclosure
- Verify WAV files are valid and playable on PC

---

## Hardware

| Component | Part |
|---|---|
| Microcontroller | Olimex ESP32-S3-DevKit-Lipo (8MB flash, 8MB PSRAM) |
| Microphone | INMP441 I2S MEMS microphone |
| Amplifier | MAX98357 I2S Class D amp |
| Storage | Internal flash (4MB FAT partition) |
| Battery | 3.7V LiPo (pending) |

### Pin assignments

| Signal | GPIO |
|---|---|
| Button | 1 |
| MAX98357 BCLK | 15 |
| MAX98357 LRCLK | 16 |
| MAX98357 DIN | 17 |
| INMP441 SCK | 10 |
| INMP441 WS | 8 |
| INMP441 SD | 9 |

### Important wiring note
Not all GND pins on the Olimex board are electrically equivalent. Mic, amp and button must share the same GND pin or a closely adjacent one — using distant GND pins introduces noise that degrades audio quality significantly. Verify with a multimeter if audio quality is poor.

---

## Firmware architecture

```
comm_badge/
  main/
    app_main.c          — boot sequence and main event loop
    config.h            — all pin assignments in one place
  components/
    button_service/     — GPIO, debounce, short/long press detection
    state_machine/      — states and legal transitions
    audio_service/      — I2S mic capture, WAV file writing
    playback_service/   — WAV file reading, I2S output via MAX98357
    storage_service/    — wear-levelled FAT filesystem on internal flash
```

Built with ESP-IDF v6.0. Target: `esp32s3`.

---

## Development setup

1. Install ESP-IDF v6.0 and VS Code with the ESP-IDF extension
2. Clone this repo
3. Open an ESP-IDF terminal and initialise the environment:
```
C:\esp\v6.0\esp-idf\export.ps1
```
4. Then:
```
cd comm_badge
idf.py set-target esp32s3
idf.py build
idf.py -p COM4 flash monitor
```

---

## Roadmap

### E4 — Audio quality
Tune gain and sample extraction for clean intelligible speech. Verify WAV playable on PC.

### E5 — Note management
Support multiple notes with timestamps. Metadata survives reboot.

### E6 — BLE basics
Advertise over Bluetooth LE. Android test app can connect and read badge status.

### E7 — Note index over BLE
Android app retrieves list of available notes from badge.

### E8 — File transfer
Transfer WAV files to Android over BLE. App marks notes as synced.

### E9 — Android transcription
On-device speech-to-text. Notes appear as readable text in the app.

### E10 — Power management
Idle timeout, sleep and wake. Badge becomes battery-usable.

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