# 👑 Sparkle — Princess AI Buddy

[![Build and Release Firmware](https://img.shields.io/github/actions/workflow/status/shenoiz/princess_ai_bot/build.yml?branch=main&style=for-the-badge&logo=github-actions&logoColor=white&label=CI%20%2F%20CD&color=FF6EC7)](https://github.com/shenoiz/princess_ai_bot/actions/workflows/build.yml)
[![Latest Release](https://img.shields.io/github/v/release/shenoiz/princess_ai_bot?style=for-the-badge&logo=github&logoColor=white&label=Firmware&color=C084FC)](https://github.com/shenoiz/princess_ai_bot/releases/latest)
[![Release Date](https://img.shields.io/github/release-date/shenoiz/princess_ai_bot?style=for-the-badge&logo=github&logoColor=white&color=A78BFA)](https://github.com/shenoiz/princess_ai_bot/releases/latest)
[![OTA](https://img.shields.io/badge/OTA-Enabled-6EE7B7?style=for-the-badge&logo=wifi&logoColor=white)](https://github.com/shenoiz/princess_ai_bot/releases)
[![Board](https://img.shields.io/badge/Board-ESP32--S3--N16R8-FDE68A?style=for-the-badge&logo=espressif&logoColor=white)](https://www.espressif.com/en/products/socs/esp32-s3)
[![Arduino](https://img.shields.io/badge/Arduino-IDE-00878F?style=for-the-badge&logo=arduino&logoColor=white)](https://www.arduino.cc/)
[![Groq](https://img.shields.io/badge/STT%20%2B%20AI-Groq-FF6EC7?style=for-the-badge&logoColor=white)](https://console.groq.com)
[![VoiceRSS](https://img.shields.io/badge/TTS-VoiceRSS-C084FC?style=for-the-badge&logoColor=white)](https://voicerss.org)
[![License](https://img.shields.io/badge/License-MIT-FDE68A?style=for-the-badge)](LICENSE)
[![PRs Welcome](https://img.shields.io/badge/PRs-Welcome-6EE7B7?style=for-the-badge&logo=git&logoColor=white)](https://github.com/shenoiz/princess_ai_bot/pulls)
[![Issues](https://img.shields.io/github/issues/shenoiz/princess_ai_bot?style=for-the-badge&logo=github&logoColor=white&color=FF6EC7)](https://github.com/shenoiz/princess_ai_bot/issues)
[![Commits](https://img.shields.io/github/commit-activity/m/shenoiz/princess_ai_bot?style=for-the-badge&logo=github&logoColor=white&color=A78BFA)](https://github.com/shenoiz/princess_ai_bot/commits/main)
[![Repo Size](https://img.shields.io/github/repo-size/shenoiz/princess_ai_bot?style=for-the-badge&logo=github&logoColor=white&color=6EE7B7)](https://github.com/shenoiz/princess_ai_bot)
[![Stars](https://img.shields.io/github/stars/shenoiz/princess_ai_bot?style=for-the-badge&logo=github&logoColor=white&color=FDE68A)](https://github.com/shenoiz/princess_ai_bot/stargazers)

> A voice-in, voice-out AI toy for your little princess.
> Hold the button → she listens → thinks → replies out loud with animated OLED face and RGB glow.
> Merge a PR to main → GitHub Actions compiles → device updates itself over Wi-Fi on next boot.

---

## Table of Contents

- [How It Works](#how-it-works)
- [Hardware](#hardware)
- [Wiring](#wiring)
- [File Structure](#file-structure)
- [Software Architecture](#software-architecture)
- [API Keys](#api-keys)
- [Arduino IDE Setup](#arduino-ide-setup)
- [First USB Flash](#first-usb-flash)
- [Branch Strategy](#branch-strategy)
- [CI/CD Pipeline](#cicd-pipeline)
- [OTA Updates](#ota-updates)
- [OLED Faces](#oled-faces)
- [RGB LED Modes](#rgb-led-modes)
- [Privacy](#privacy)
- [Troubleshooting](#troubleshooting)

---

## How It Works

```
Child holds BOOT button (GPIO 0)
    │
    ▼
ESP32-S3 records voice via INMP441
I2S config: Philips 32-bit stereo, SAMPLE_RATE 16000Hz
Left channel extracted → 16-bit mono stored in PSRAM
Recording stops exactly when button is released
    │
    ▼
Pitch shift runs on-device (overlap-add algorithm, PITCH_FACTOR 1.35)
Real voice never leaves the hardware — only shifted audio is sent
    │
    ▼
Shifted audio + 44-byte WAV header uploaded to Groq Whisper STT
via HTTPS multipart/form-data POST
Returns transcribed text string
    │
    ▼
Text sent to Groq LLaMA 3.1-8b-instant
System prompt: Sparkle the fairy — 2 short sentences, fun, never scary
Last MAX_HISTORY turns kept in memory for conversation context
JSON history sanitized before every request to prevent 400 errors
Returns AI reply text
    │
    ▼
Reply text sent to VoiceRSS TTS API
Returns 16kHz 16-bit mono WAV audio
    │
    ▼
Dual-core streaming playback:
  Producer task: downloads WAV → parses RIFF header → fills ring buffer in PSRAM
  Consumer (main core): reads ring buffer → pads 16-bit PCM to 32-bit → I2S DMA → MAX98357A → speaker
Playback starts after START_BUFFER bytes prebuffered to prevent starvation
Silence fed to I2S during natural gaps between sentences
I2S flushed after playback so last word is not clipped
    │
    ▼
Emotion face shown on OLED for 700ms based on reply keywords
RGB LED mirrors face state
Then SPEAKING face + RGB while audio plays
    │
    ▼
Returns to IDLE face + RGB breathing animation
Waits for next button press
```

---

## Hardware

| Part | What to buy | Cost |
|------|------------|------|
| Brain | ESP32-S3-WROOM1-N16R8 DevKit — Unsoldered Header — CH9102 | ~$6 |
| Microphone | INMP441 I2S digital mic module | ~$3 |
| Speaker amp | MAX98357A I2S amplifier module | ~$4 |
| Speaker | 3W 4Ω round speaker — 40 to 80mm | ~$2 |
| Display | SSD1306 0.96 inch OLED — I2C — 128x64px | ~$2 |
| RGB LED | Built-in NeoPixel on GPIO 48 — already on board | free |
| Button | BOOT button on GPIO 0 — already on board | free |
| LED indicator | Any 5mm LED + 330Ω resistor | <$1 |

**Total hardware cost: ~$18. API cost: $0 on free tiers.**

---

## Wiring

### Install CH9102 Driver First

Board uses CH9102 USB chip. Download from **wch.cn**, install, reboot PC, plug board via USB-C.
A COM port must appear in Device Manager before any upload will work.

### Solder Pin Headers

Board ships unsoldered. Tack one corner pin per row to hold it straight, then solder the rest.

---

### INMP441 Microphone → ESP32-S3

| INMP441 | ESP32-S3 GPIO | Notes |
|---------|--------------|-------|
| VDD | 3.3V | Must be 3.3V — not 5V |
| GND | GND | |
| SCK | GPIO 14 | I2S bit clock |
| WS | GPIO 15 | Word select |
| SD | GPIO 16 | Serial data out |
| L/R | GND | Selects left channel |

### MAX98357A Speaker Amp → ESP32-S3

| MAX98357A | ESP32-S3 GPIO | Notes |
|----------|--------------|-------|
| VIN | 3.3V | ESP32-S3 VBUS is input only — use 3.3V |
| GND | GND | |
| BCLK | GPIO 12 | I2S bit clock |
| LRC | GPIO 13 | Left/right clock |
| DIN | GPIO 11 | Serial data in |
| SD | 3.3V | Must be HIGH — floating SD causes random mute |
| GAIN | GND | Tie to GND for 9dB gain |

### SSD1306 OLED → ESP32-S3

| OLED | ESP32-S3 GPIO | Notes |
|------|--------------|-------|
| VCC | 3.3V | |
| GND | GND | |
| SCL | GPIO 9 | I2C clock |
| SDA | GPIO 8 | I2C data |

### Indicator LED

| Component | ESP32-S3 GPIO | Notes |
|-----------|--------------|-------|
| LED anode (+) | GPIO 4 | Via 330Ω resistor to GND |

**Important:**
- GPIO 0 = BOOT button — already on board, used as talk button
- GPIO 48 = RGB NeoPixel — already on board, no wiring needed
- GPIO 1 = TX pin — do not use for anything

---

## File Structure

```
princess_ai_bot/
├── .github/
│   └── workflows/
│       └── build.yml          ← CI/CD pipeline
├── firmware/
│   ├── firmware.ino           ← main sketch
│   ├── faces.h                ← OLED face library (12 expressions)
│   ├── Sparkle_RGB.h          ← RGB NeoPixel library (13 modes)
│   ├── ota.h                  ← OTA updater
│   └── secrets.h.example      ← template — safe to commit
└── README.md
```

**Note:** `version.txt` is no longer used. OTA reads the version directly from the GitHub Releases API. You can delete `version.txt` from your repo if it exists.

---

## Software Architecture

### firmware.ino — Function Map

| Function | What it does |
|----------|-------------|
| `urlEncode()` | Percent-encodes text for VoiceRSS URL including Unicode and Hindi characters |
| `setupMic()` | Configures I2S0 as Philips 32-bit stereo — required for INMP441 on ESP32-S3 |
| `setupSpeaker()` | Configures I2S1 as MSB 32-bit mono — required for correct DMA clock on ESP32-S3 |
| `makeWavHeader()` | Builds 44-byte RIFF WAV header for recorded audio before sending to Groq |
| `pitchShift()` | Overlap-add pitch shift — raises voice 35% before sending to cloud |
| `recordWhileHeld()` | Records in 128-frame chunks while button held, stops on release — no fixed duration |
| `writeSpeaker()` | Pads 16-bit PCM to 32-bit left-justified, writes to I2S in 256-sample chunks |
| `ensureWiFi()` | Reconnects WiFi if dropped — called before every API request |
| `transcribe()` | Posts WAV to Groq Whisper via multipart form-data, returns text |
| `askAI()` | Posts conversation to Groq LLaMA, sanitizes reply for JSON safety |
| `speakText()` | Dual-core streaming TTS — producer fills ring buffer, consumer plays via I2S |
| `pickFaceState()` | Maps keywords in AI reply to FaceState enum |
| `setFaceAndRGB()` | Sets OLED face and RGB LED to same state atomically |
| `connectWifi()` | Connects WiFi on boot, shows IP on OLED |
| `setup()` | Allocates PSRAM buffers, inits all hardware, connects WiFi, runs OTA check |
| `loop()` | Button → record → pitch shift → STT → AI → emotion face → TTS → idle |

### faces.h — OLED Face Library

12 animated expressions at 12fps on SSD1306 128x64.

Public API:
- `Face::begin()` — initialises I2C and OLED
- `Face::setState(FaceState)` — changes expression, resets frame counter
- `Face::tick()` — call every loop iteration, renders at 12fps
- `Face::showStateFor(state, ms)` — holds a face for fixed duration

### Sparkle_RGB.h — RGB NeoPixel Library

Controls built-in NeoPixel on GPIO 48. 13 mood modes with unique animations.

Public API:
- `RGB::begin()` — initialises NeoPixel
- `RGB::setMode(Mode)` — changes mode
- `RGB::tick()` — call every loop for animation
- `RGB::off()` — turns off LED

RGB::tick() is not called during mic recording or I2S audio playback to prevent hardware interference.

### ota.h — OTA Updater

On every boot after WiFi connects:

1. Calls `https://api.github.com/repos/shenoiz/princess_ai_bot/releases/latest`
2. Extracts release tag name (strips `v` prefix) and `.bin` download URL in one call
3. Compares patch number numerically — `1.0.14` > `1.0.3` correctly
4. If device is behind — resolves the GitHub redirect to final signed URL
5. Downloads and flashes via `httpUpdate.update()`
6. Device reboots into new firmware automatically
7. If device is up to date or newer — continues normally

No `version.txt` file is used or needed.

---

## API Keys

### Groq — STT and AI — Free, no credit card

1. Sign up at **console.groq.com**
2. Create API key — starts with `gsk_`
3. Free tier: 14,400 Whisper seconds per day, 30,000 LLaMA tokens per minute

### VoiceRSS — TTS — Free tier

1. Sign up at **voicerss.org**
2. API key shown on dashboard immediately
3. Free tier: 350 requests per day

---

## Arduino IDE Setup

### Add Board URL

File → Preferences → Additional Board Manager URLs:
```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```

### Install Board

Tools → Board Manager → search **esp32** → install **esp32 by Espressif Systems**

### Install Libraries

Tools → Library Manager → install:
- `Adafruit SSD1306`
- `Adafruit GFX Library`
- `Adafruit NeoPixel`
- `ArduinoJson`

### Board Settings

| Setting | Value |
|---------|-------|
| Board | ESP32S3 Dev Module |
| USB CDC On Boot | Enabled |
| Flash Size | 16MB (128Mb) |
| Partition Scheme | App3M FAT9M 16MB |
| PSRAM | OPI PSRAM |
| Upload Speed | 921600 |
| Port | COM port that appeared after CH9102 driver install |

---

## First USB Flash

This is the only time USB is needed. Every update after this happens over Wi-Fi automatically.

1. Copy `firmware/secrets.h.example` → `firmware/secrets.h`
2. Fill in all four values — WIFI_SSID, WIFI_PASS, GROQ_KEY, VOICERSS_KEY
3. Open `firmware/firmware.ino` in Arduino IDE — all tabs load automatically
4. Set board settings from the table above exactly
5. Click Upload
6. Open Serial Monitor at 115200 baud
7. You should see: WiFi OK → OTA up to date → Ready

---

## Branch Strategy

Direct pushes to `main` and `develop` are blocked. All changes go through Pull Requests.

```
main        ← production. CI fires only here. Merged PRs only.
develop     ← integration. All features merge here first.
feature/*   ← your working branch. Always branch from develop.
hotfix/*    ← urgent fix. Branch from main directly.
```

### Normal workflow

```bash
# 1. Start from develop
git checkout develop
git pull origin develop

# 2. Create feature branch
git checkout -b feature/my-change

# 3. Make changes and commit
git add .
git commit -m "feat: describe your change"
git push origin feature/my-change

# 4. Open PR on GitHub: feature/my-change → develop
# 5. Merge it

# 6. Open PR on GitHub: develop → main
# 7. Merge it → CI pipeline fires automatically
```

### Branch protection rules set on main and develop

- Restrict deletions
- Require pull request before merging
- Block force pushes

---

## CI/CD Pipeline

File: `.github/workflows/build.yml`

Fires automatically when a PR is merged into `main`. Watch it live under the **Actions** tab.

| Step | What happens |
|------|-------------|
| Checkout | Full git history fetched |
| Install arduino-cli | CLI installed to `$HOME/bin` with explicit path used in all subsequent steps |
| Install ESP32 core | Espressif ESP32 core 3.0.7 installed |
| Install libraries | Adafruit SSD1306, GFX, NeoPixel, ArduinoJson installed |
| Generate secrets.h | Created from GitHub Secrets — keys never appear in repo |
| Set version | Version set to `1.0.{github.run_number}` — injected into firmware.ino via sed for compile only, repo file unchanged |
| Compile firmware | Compiled for ESP32-S3 with 16MB flash, OPI PSRAM, App3M FAT9M partition |
| Create GitHub Release | Release tagged `v1.0.N` created — firmware.ino.bin attached as asset |

CI never writes anything back to `main`. No branch protection conflicts ever.

### GitHub Secrets to add

Repo → Settings → Secrets and variables → Actions → New repository secret:

| Name | Value |
|------|-------|
| `WIFI_SSID` | Your Wi-Fi network name |
| `WIFI_PASS` | Your Wi-Fi password |
| `GROQ_KEY` | Your Groq API key |
| `VOICERSS_KEY` | Your VoiceRSS API key |

---

## OTA Updates

After merging a PR to main and CI completing:

1. Reboot the device (press RST button)
2. Device connects to WiFi
3. Calls GitHub Releases API — gets latest release tag and bin URL in one request
4. Compares patch number numerically with compiled FW_VERSION
5. If latest is newer — resolves GitHub redirect to final signed URL — downloads and flashes
6. Device reboots into new firmware
7. Serial Monitor shows new version on next boot

OTA uses `App3M FAT9M 16MB` partition scheme which has a proper OTA slot. Do not change the partition scheme or OTA will stop working.

### Serial Monitor output during OTA

```
OTA: checking for update...
OTA: current=1.0.12 latest=1.0.14
OTA: update available 1.0.14
OTA: final URL https://release-assets.githubusercontent.com/...
(device flashes and reboots automatically)
NET[BOOT]: ...
OTA: current=1.0.14 latest=1.0.14
OTA: up to date
Ready! Hold BOOT and speak.
```

---

## OLED Faces

12 animated expressions. Emotion face shows for 700ms after AI replies then switches to SPEAKING.

| Face | When shown | Reply keywords that trigger it |
|------|-----------|-------------------------------|
| IDLE | Resting — 3 second blink cycle | — |
| LISTENING | Button held — recording | — |
| THINKING | Processing STT and AI | — |
| SPEAKING | Playing TTS audio | — |
| HAPPY | Default after reply | haha, funny, giggle, laugh |
| SURPRISED | Reaction | wow, amazing, surprise |
| EXCITED | Enthusiasm | yay, hooray, awesome, excited, sparkly |
| LOVE | Affection | love, heart, adorable, sweet |
| SAD | Sadness | sorry, sad, miss |
| CURIOUS | Curiosity | wonder, curious, maybe, perhaps |
| SLEEPY | Tiredness | sleep, tired, bedtime, night |
| WINK | Playfulness | wink, secret |

---

## RGB LED Modes

Built-in NeoPixel on GPIO 48. Mirrors OLED face state. Not animated during mic recording or audio playback.

| Mode | Colour | Animation |
|------|--------|-----------|
| IDLE | Soft blue | Slow breathing |
| LISTENING | Cyan | Static |
| THINKING | Purple | Breathing with hue drift |
| SPEAKING | Blue-white | Static |
| HAPPY | Warm amber | Breathing |
| LOVE | Deep pink | Double heartbeat pulse |
| EXCITED | Rainbow | Fast colour wheel |
| SURPRISED | White | Decay pulse |
| SAD | Deep blue | Slow breathing |
| SLEEPY | Dark blue-grey | Very slow breathing |
| WINK | Hot pink | Breathing |
| CURIOUS | Sky blue | Breathing |
| OTA | Blue-cyan | Breathing during update |

---

## Privacy

The child's real voice never leaves the device.

Before any audio is sent over the internet, the ESP32-S3 applies a pitch-shift algorithm that raises the voice by 35% making it sound robotic. Groq Whisper transcribes the shifted audio accurately. Only the shifted audio and reply text travel online.

---

## Troubleshooting

**Board not showing as COM port**
Install CH9102 driver from wch.cn and reboot PC.

**PSRAM alloc failed in Serial Monitor**
Tools → PSRAM → OPI PSRAM. Must be set exactly.

**OTA not triggering**
Check that GH_USER and GH_REPO in ota.h match your GitHub username and repo name exactly. Both are case-sensitive.

**OTA failed: Partition Could Not be Found**
You are using huge_app partition which has no OTA slot. Change to App3M FAT9M 16MB in Arduino IDE and do one USB flash with the new partition. OTA will work after that.

**No sound from speaker**
Check MAX98357A SD pin is connected to 3.3V — floating SD causes random mute. Check VIN is 3.3V not 5V — ESP32-S3 VBUS is input only.

**Mic only catching one word**
Hold button and speak immediately — recording starts the instant button is pressed. Do not pause before speaking.

**400 error on AI after first query**
Special characters in AI reply breaking JSON history. The sanitize block in askAI() handles this — confirm it is present in your firmware.

**CI pipeline not appearing in Actions tab**
Pipeline fires only on push to main via merged PR. Direct pushes are blocked by branch protection.

**TTS stuttering between sentences**
Reduce MAX_TOKENS to shorten AI replies and reduce audio duration.
