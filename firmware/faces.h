// faces.h
// Princess OLED face library for SSD1306 128x64 I2C display.
// 12 animated expressions at ~12fps.
// Call Face::begin() in setup(), Face::setState(FACE_XXX) to change,
// and Face::tick() every loop iteration.

#pragma once
#include <Adafruit_SSD1306.h>
#include <math.h>

#define OLED_W    128
#define OLED_H    64
#define OLED_ADDR 0x3C
#define OLED_SDA  8
#define OLED_SCL  9

enum FaceState {
  FACE_IDLE,        // calm, slow blink every 3s
  FACE_HAPPY,       // squint eyes, rosy cheeks, big smile
  FACE_LISTENING,   // wide eyes, raised brows, pulsing mic ring
  FACE_THINKING,    // side-glance pupils, wavy mouth, floating dots
  FACE_SPEAKING,    // bouncing open mouth, floating music notes
  FACE_SURPRISED,   // giant circle eyes, exclamation marks
  FACE_SAD,         // droopy eyes, frown, falling teardrop
  FACE_LOVE,        // heart eyes, floating hearts, blush
  FACE_EXCITED,     // sparkle eyes, whole face bounces
  FACE_SLEEPY,      // heavy lids, drifting Zzz
  FACE_WINK,        // one eye winks, sparkle
  FACE_CURIOUS,     // one brow up, pupils look up, question mark
};

namespace Face {

Adafruit_SSD1306 oled(OLED_W, OLED_H, &Wire, -1);
FaceState _state     = FACE_IDLE;
uint32_t  _lastFrame = 0, _lastBlink = 0;
int       _frame     = 0;
bool      _blinkOpen = true;

// ── Primitives ──────────────────────────────────────────────

void eye(int cx, int cy, int w, int h, float open = 1.0f) {
  int eh = max(1, (int)(h * open));
  int yo = cy - eh / 2;
  int r  = min(w, eh) / 3;
  if (eh <= 2) { oled.drawFastHLine(cx - w/2, cy, w, WHITE); return; }
  oled.fillRoundRect(cx - w/2, yo, w, eh, r, WHITE);
  oled.fillCircle(cx - w/4, yo + 2, 2, BLACK); // shine dot
}

void pupil(int cx, int cy, int ox = 0, int oy = 0) {
  oled.fillCircle(cx + ox, cy + oy, 4, BLACK);
  oled.fillCircle(cx + ox + 1, cy + oy - 1, 1, WHITE);
}

void brow(int x, int y, int w, int slant) {
  for (int i = 0; i < w; i++)
    oled.drawFastVLine(x + i, y + slant * i / w, 3, WHITE);
}

void smileCurve(int cx, int y, int w, int d, int t = 2) {
  for (int k = 0; k < t; k++)
    for (int x = -w/2; x <= w/2; x++) {
      float n = (float)x / (w/2);
      oled.drawPixel(cx + x, y + k + (int)(d * n * n), WHITE);
    }
}

void frownCurve(int cx, int y, int w, int d) {
  for (int x = -w/2; x <= w/2; x++) {
    float n = (float)x / (w/2);
    oled.drawPixel(cx + x, y - (int)(d * n * n), WHITE);
  }
}


void heart(int cx, int cy, int r) {
  int w = max(4, r * 2 / 3);
  oled.fillCircle(cx - w/2, cy - r/4, w/2 + 1, WHITE);
  oled.fillCircle(cx + w/2, cy - r/4, w/2 + 1, WHITE);
  for (int y = -r/5; y <= r; y++) {
    int half = (int)((float)(r - y) * 0.48f);
    if (half < 1) half = 1;
    oled.drawFastHLine(cx - half, cy + y, half * 2 + 1, WHITE);
  }
}

void sparkle(int cx, int cy, int r) {
  oled.drawFastHLine(cx - r, cy, r * 2 + 1, WHITE);
  oled.drawFastVLine(cx, cy - r, r * 2 + 1, WHITE);
  if (r >= 5) {
    oled.drawLine(cx-r/2, cy-r/2, cx+r/2, cy+r/2, WHITE);
    oled.drawLine(cx+r/2, cy-r/2, cx-r/2, cy+r/2, WHITE);
  }
  oled.fillCircle(cx, cy, 1, WHITE);
}

void blush(int lx, int rx, int y, int r = 6) {
  for (int dy = -r; dy <= r; dy++) {
    for (int dx = -r; dx <= r; dx++) {
      if (dx*dx + dy*dy <= r*r && ((dx + dy) & 1) == 0) {
        oled.drawPixel(lx + dx, y + dy, WHITE);
        oled.drawPixel(rx + dx, y + dy, WHITE);
      }
    }
  }
}

// Screen coordinates: larger Y is lower on the OLED.
// This is the true U-shaped happy smile.
void happyCurve(int cx, int y, int w, int d, int t = 2) {
  for (int k = 0; k < t; k++) {
    for (int x = -w/2; x <= w/2; x++) {
      float n = (float)x / (w/2);
      oled.drawPixel(cx + x, y + k + (int)(d * n * n), WHITE);
    }
  }
}

void cuteClosedEye(int cx, int cy, int w = 18) {
  for (int x = -w/2; x <= w/2; x++) {
    float n = (float)x / (w/2);
    int y = cy - (int)(3.0f * (1.0f - n*n));
    oled.drawPixel(cx + x, y, WHITE);
  }
}

// U-shaped mouth = happy.
// The U gets wider/deeper while Sparkle talks and gently wobbles.
void cuteSpeechMouth(int cx, int cy, int w, int depth, int wobble) {
  int left = cx - w/2;
  int right = cx + w/2;
  for (int x = left; x <= right; x++) {
    float n = (float)(x - cx) / (w/2);
    int y = cy + (int)(depth * n * n) + wobble;
    oled.drawPixel(x, y, WHITE);
    if (depth >= 5) oled.drawPixel(x, y + 1, WHITE);
  }
}

// Inverted U (∩) = sad/frown.
void sadFrown(int cx, int cy, int w, int height) {
  for (int x = -w/2; x <= w/2; x++) {
    float n = (float)x / (w/2);
    int y = cy - (int)(height * n * n);
    oled.drawPixel(cx + x, y, WHITE);
  }
}

// ── Face renderers ───────────────────────────────────────────

void rIdle(float o) {
  eye(38,26,24,18,o); eye(90,26,24,18,o);
  if (o > 0.5f) { pupil(38,26); pupil(90,26); }
  // Canonical idle/happy mouth: U-shaped curve.
  // Keep this geometry as the reference for all happy expressions.
  sadFrown(64,53,28,7);
}


void rHappy(int f) {
  float pulse = 0.30f + 0.04f * sinf(f * 0.15f);
  eye(38,25,26,18,pulse);
  eye(90,25,26,18,pulse);
  blush(18,110,34,6);
  sadFrown(64,50,46,11); // U = happy
  if ((f/6)%2 == 0) {
    sparkle(10,11,3);
    sparkle(118,11,3);
  }
}

void rListening(int f) {
  float p = 0.92f + 0.08f * sinf(f * 0.18f);
  eye(38,25,27,22,p);
  eye(90,25,27,22,p);
  pupil(38,25,0,1);
  pupil(90,25,0,1);
  brow(25,8,24,-3);
  brow(79,8,24,-3);

  int pulse = 2 + (int)(2.0f + 2.0f * sinf(f * 0.35f));
  oled.drawCircle(112,42,pulse,WHITE);
  oled.drawFastHLine(116,42,5,WHITE);
  oled.drawFastHLine(119,38,3,WHITE);
  oled.drawFastHLine(119,46,3,WHITE);
}

void rThinking(int f) {
  brow(25,9,24,3);
  brow(79,7,24,-2);
  eye(38,25,24,18,0.78f);
  eye(90,25,24,18,0.78f);
  pupil(38,25,4,-4);
  pupil(90,25,4,-4);
  sadFrown(64,51,18,4); // small U, friendly not sad

  int d = (f / 5) % 4;
  for (int i = 0; i < d; i++) {
    oled.fillCircle(91 + i*8, 13 - i*3, 1 + (i == 2), WHITE);
  }
}

void rSpeaking(int f) {
  eye(38,23,24,18,0.82f);
  eye(90,23,24,18,0.82f);
  pupil(38,23,0,0);
  pupil(90,23,0,0);

  float s = 0.5f + 0.5f * fabsf(sinf(f * 0.42f));
  int w = 14 + (int)(10.0f * s);
  int depth = 4 + (int)(5.0f * s);
  sadFrown(64,50,w,depth);

  if ((f/3)%2 == 0) {
    oled.drawPixel(22,40,WHITE);
    oled.drawPixel(106,40,WHITE);
  }
  if ((f/8)%2 == 0) sparkle(108,16,3);
}

void rSurprised(int f) {
  float p = 1.0f + 0.05f * sinf(f * 0.28f);
  oled.drawCircle(38,26,(int)(12*p),WHITE);
  oled.drawCircle(90,26,(int)(12*p),WHITE);
  oled.fillCircle(38,26,4,WHITE);
  oled.fillCircle(90,26,4,WHITE);
  brow(24,5,22,-5);
  brow(76,5,22,-5);
  oled.drawCircle(64,52,5 + ((f/6)%2),WHITE); // O = surprised
  if ((f/4)%2 == 0) {
    sparkle(15,12,3);
    sparkle(113,12,3);
  }
}

void rSad(int f) {
  brow(26,14,22,-4);
  brow(78,14,22,-4);
  eye(38,26,24,18,0.42f);
  eye(90,26,24,18,0.42f);
  pupil(38,27,0,2);
  pupil(90,27,0,2);
  // The ONLY inverted-U mouth in the face set: sadness.
  happyCurve(64,54,24,6,3); // ∩ = sad/frown

  int ty = 36 + (f % 18);
  if (ty < 58) {
    oled.fillCircle(30,ty,2,WHITE);
    oled.drawFastVLine(30,ty+2,3,WHITE);
  }
}

void rLove(int f) {
  float p = 1.0f + 0.10f * sinf(f * 0.20f);
  heart(38,25,(int)(13*p));
  heart(90,25,(int)(13*p));
  blush(14,114,39,6);
  sadFrown(64,50,42,10); // U = happy

  int hy = 38 - (f*2)%28;
  if (hy > 8) heart(108,hy,4);
  if ((f/10)%2 == 0) heart(20,16,3);
}

void rExcited(int f) {
  int b = (int)(2.0f * sinf(f * 0.45f));
  sparkle(38,25+b,10);
  sparkle(90,25+b,10);
  sadFrown(64,50+b,44,10); // U = happy
  blush(14,114,38+b,6);

  if ((f/6)%2 == 0) {
    sparkle(12,12,3);
    sparkle(116,12,3);
  } else {
    sparkle(12,48,3);
    sparkle(116,48,3);
  }
}

void rSleepy(int f) {
  float d = 0.20f + 0.12f * sinf(f * 0.06f);
  eye(38,26,24,18,d);
  eye(90,26,24,18,d);
  cuteClosedEye(38,43,16);
  cuteClosedEye(90,43,16);
  sadFrown(64,51,16,3); // gentle U

  int phase = (f/6)%24;
  int zx = 82 + phase;
  int zy = 19 - phase/3;
  oled.setCursor(zx,zy); oled.print("z");
  oled.setCursor(zx+7,zy-6); oled.print("Z");
  oled.setCursor(zx+14,zy-12); oled.print("Z");
}

void rWink(int f) {
  eye(90,24,24,18,1.0f);
  pupil(90,24,-2,0);
  cuteClosedEye(38,26,20);
  blush(16,112,36,5);
  sadFrown(64,50,36,9);
  if ((f/5)%2 == 0) sparkle(22,14,4);
}

void rCurious(int f) {
  brow(26,6,22,-5);
  brow(78,12,22,2);
  eye(38,24,26,22,1.0f);
  eye(90,26,22,16,0.72f);
  pupil(38,24,0,-3);
  pupil(90,26,1,-4);
  sadFrown(64,51,14,3);
  oled.setCursor(113,2);
  oled.print("?");
  if ((f/5)%2 == 0) sparkle(108,14,3);
}

// ── Public API ───────────────────────────────────────────────

void begin() {
  Wire.begin(OLED_SDA, OLED_SCL);
  oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  oled.setTextColor(WHITE);
  oled.setTextSize(1);
  oled.clearDisplay();
  oled.display();
}

void tick();

void setState(FaceState s) { _state = s; _frame = 0; }

// OLED-only state hold. Does not touch audio or network pipelines.
void showStateFor(FaceState s, uint32_t durationMs) {
  setState(s);
  uint32_t start = millis();
  while (millis() - start < durationMs) {
    tick();
    delay(5);
  }
}

void tick() {
  uint32_t now = millis();
  if (now - _lastFrame < 80) return; // ~12fps
  _lastFrame = now;
  _frame++;

  if (_state == FACE_IDLE) {
    if (now - _lastBlink > 3000) _blinkOpen = false;
    if (now - _lastBlink > 3160) { _blinkOpen = true; _lastBlink = now; }
  }

  oled.clearDisplay();
  oled.setTextColor(WHITE);

  switch (_state) {
    case FACE_IDLE:      rIdle(_blinkOpen ? 1.0f : 0.04f); break;
    case FACE_HAPPY:     rHappy(_frame);     break;
    case FACE_LISTENING: rListening(_frame); break;
    case FACE_THINKING:  rThinking(_frame);  break;
    case FACE_SPEAKING:  rSpeaking(_frame);  break;
    case FACE_SURPRISED: rSurprised(_frame); break;
    case FACE_SAD:       rSad(_frame);       break;
    case FACE_LOVE:      rLove(_frame);      break;
    case FACE_EXCITED:   rExcited(_frame);   break;
    case FACE_SLEEPY:    rSleepy(_frame);    break;
    case FACE_WINK:      rWink(_frame);      break;
    case FACE_CURIOUS:   rCurious(_frame);   break;
  }
  oled.display();
}

} // namespace Face
