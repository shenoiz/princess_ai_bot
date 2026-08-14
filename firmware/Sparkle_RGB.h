#pragma once
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

// Sparkle RGB V2 - AUDIO SAFE
// GPIO48 is confirmed by the standalone RGB test.
// RGB::tick() is intentionally NOT called from microphone, Wi-Fi, STT,
// AI, TTS, or speaker-playback code. The firmware only animates RGB while
// idle or during the short OLED-only emotion/thinking holds.

namespace RGB {

static constexpr uint8_t RGB_PIN = 48;
static constexpr uint8_t RGB_COUNT = 1;
static constexpr uint8_t MAX_BRIGHTNESS = 85;
static constexpr uint32_t FRAME_INTERVAL_MS = 100; // 10 FPS

enum Mode : uint8_t {
  OFF = 0,
  IDLE,
  LISTENING,
  THINKING,
  HAPPY,
  LOVE,
  EXCITED,
  SURPRISED,
  SAD,
  SLEEPY,
  WINK,
  CURIOUS,
  SPEAKING,
  ERROR_MODE,
  OTA
};

static Adafruit_NeoPixel strip(RGB_COUNT, RGB_PIN, NEO_GRB + NEO_KHZ800);
static Mode mode = OFF;
static uint32_t modeStarted = 0;
static uint32_t lastFrame = 0;
static bool ready = false;

static uint32_t scaled(uint8_t r, uint8_t g, uint8_t b, uint8_t level) {
  uint16_t br = (uint16_t)MAX_BRIGHTNESS * level / 255;
  return strip.Color(
    (uint16_t)r * br / 255,
    (uint16_t)g * br / 255,
    (uint16_t)b * br / 255
  );
}

static void show(uint8_t r, uint8_t g, uint8_t b, uint8_t level) {
  if (!ready) return;
  strip.setPixelColor(0, scaled(r, g, b, level));
  strip.show();
}

static uint8_t breathe(float phase, uint8_t lo, uint8_t hi) {
  float s = 0.5f + 0.5f * sinf(phase);
  return lo + (uint8_t)((hi - lo) * s);
}

static uint32_t wheel(uint8_t pos) {
  pos = 255 - pos;
  if (pos < 85)
    return strip.Color(255 - pos * 3, 0, pos * 3);
  if (pos < 170) {
    pos -= 85;
    return strip.Color(0, pos * 3, 255 - pos * 3);
  }
  pos -= 170;
  return strip.Color(pos * 3, 255 - pos * 3, 0);
}

static void begin() {
  strip.begin();
  strip.setBrightness(MAX_BRIGHTNESS);
  strip.clear();
  strip.show();
  ready = true;
  mode = OFF;
  modeStarted = millis();
  lastFrame = 0;
}

static void tick();

static void setMode(Mode m) {
  mode = m;
  modeStarted = millis();
  lastFrame = 0;
  // One immediate state update. Continuous animation only occurs when the
  // firmware explicitly calls RGB::tick() outside audio processing.
  tick();
}

static void off() {
  if (!ready) return;
  mode = OFF;
  strip.clear();
  strip.show();
}

static void tick() {
  if (!ready) return;

  uint32_t now = millis();
  if (now - lastFrame < FRAME_INTERVAL_MS) return;
  lastFrame = now;

  float t = (now - modeStarted) * 0.001f;

  switch (mode) {
    case OFF:
      show(0, 0, 0, 0);
      break;

    case IDLE: {
      uint8_t b = breathe(t * 1.15f, 12, 48);
      show(40, 150, 255, b);
      break;
    }

    case LISTENING:
      // Static-ish state because RGB animation is intentionally paused
      // during microphone capture.
      show(0, 210, 255, 105);
      break;

    case THINKING: {
      uint8_t b = breathe(t * 1.5f, 25, 90);
      float m = 0.5f + 0.5f * sinf(t * 0.9f);
      show(
        70 + (uint8_t)(90 * m),
        15 + (uint8_t)(20 * m),
        180 + (uint8_t)(55 * m),
        b
      );
      break;
    }

    case HAPPY: {
      uint8_t b = breathe(t * 1.6f, 35, 110);
      show(255, 145, 35, b);
      break;
    }

    case LOVE: {
      float c = fmodf(t, 1.55f);
      float p1 = expf(-18.0f * (c - 0.18f) * (c - 0.18f));
      float p2 = expf(-18.0f * (c - 0.43f) * (c - 0.43f));
      float pulse = 0.18f + 0.82f * fminf(1.0f, p1 + p2);
      show(255, 20, 105, (uint8_t)(255 * pulse));
      break;
    }

    case EXCITED: {
      uint8_t h = (uint8_t)((uint32_t)(t * 90.0f) & 255);
      uint32_t c = wheel(h);
      uint8_t b = breathe(t * 4.0f, 70, 160);
      show(
        (uint8_t)(c >> 16),
        (uint8_t)(c >> 8),
        (uint8_t)c,
        b
      );
      break;
    }

    case SURPRISED: {
      float pulse = expf(-5.5f * t);
      if (t < 0.75f)
        show(255, 255, 255, (uint8_t)(255 * pulse));
      else
        show(60, 190, 255, 45);
      break;
    }

    case SAD: {
      uint8_t b = breathe(t * 0.65f, 12, 55);
      show(25, 80, 210, b);
      break;
    }

    case SLEEPY: {
      uint8_t b = breathe(t * 0.4f, 8, 30);
      show(55, 75, 150, b);
      break;
    }

    case WINK: {
      uint8_t b = breathe(t * 1.8f, 30, 95);
      show(255, 75, 180, b);
      break;
    }

    case CURIOUS: {
      uint8_t b = breathe(t * 1.2f, 25, 80);
      show(80, 180, 255, b);
      break;
    }

    case SPEAKING:
      // Deliberately static while speakText() is sending I2S audio.
      // This is a safety feature, not a forgotten animation.
      show(55, 155, 225, 95);
      break;

    case ERROR_MODE: {
      uint8_t b = breathe(t * 3.0f, 20, 130);
      show(255, 20, 10, b);
      break;
    }

    case OTA: {
      uint8_t b = breathe(t * 2.0f, 30, 140);
      float m = 0.5f + 0.5f * sinf(t * 1.5f);
      show(
        10 + (uint8_t)(20 * m),
        130 + (uint8_t)(100 * m),
        255,
        b
      );
      break;
    }
  }
}

} // namespace RGB
