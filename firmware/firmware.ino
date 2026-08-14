// ═══════════════════════════════════════════════════════════
// firmware.ino — Princess AI Buddy
// ESP32-S3-N16R8
// Fixed I2S for both mic and speaker on ESP32-S3
// Mic  : Philips 32bit Stereo — left channel extracted
// Spkr : MSB 32bit Mono — 16bit samples padded to 32bit
// ═══════════════════════════════════════════════════════════


#define FW_VERSION "1.0.3"

#include "secrets.h"
#include "faces.h"
#include "Sparkle_RGB.h"
#include "ota.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <driver/i2s_std.h>
#include <ArduinoJson.h>

const char* SYSTEM_PROMPT =
  "You are a magical AI fairy called Sparkle talking to a "
  "young girl. Keep answers to 2-3 short sentences. Use "
  "simple fun words. End with a sparkly question or fun fact. "
  "Never say anything scary or sad.";

#define SAMPLE_RATE   16000
#define MAX_REC_SECS  8
#define PITCH_FACTOR  1.35f
#define MAX_TOKENS    120
#define MAX_HISTORY   6

#define BTN_PIN  0
#define LED_PIN  4
#define MIC_SCK  14
#define MIC_WS   15
#define MIC_SD   16
#define SPK_BCK  12
#define SPK_WS   13
#define SPK_DIN  11

const int MAX_SAMPLES = SAMPLE_RATE * MAX_REC_SECS;
int16_t*  recBuf     = nullptr;
int16_t*  pitchBuf   = nullptr;
i2s_chan_handle_t micHandle, spkHandle;
String chatHistory = "";
int    turnCount   = 0;
bool ensureWiFi(uint32_t timeoutMs = 12000);
void printNetworkState(const char* tag);

// ── URL encode text for VoiceRSS ──────────────────────────────
String urlEncode(const String& text) {
  const char *hex = "0123456789ABCDEF";
  String out;
  out.reserve(text.length() * 3);

  for (int i = 0; i < (int)text.length(); i++) {
    uint8_t c = (uint8_t)text[i];

    if ((c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~') {
      out += (char)c;
    } else {
      out += '%';
      out += hex[(c >> 4) & 0x0F];
      out += hex[c & 0x0F];
    }
  }

  return out;
}


// ── I2S mic ──────────────────────────────────────────────────
// INMP441 on ESP32-S3: Philips, 32-bit, Stereo
// Left channel contains the audio data
void setupMic() {
  i2s_chan_config_t ch = I2S_CHANNEL_DEFAULT_CONFIG(
    I2S_NUM_0, I2S_ROLE_MASTER);
  i2s_new_channel(&ch, nullptr, &micHandle);
  i2s_std_config_t cfg = {
    .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                  I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = (gpio_num_t)MIC_SCK,
      .ws   = (gpio_num_t)MIC_WS,
      .dout = I2S_GPIO_UNUSED,
      .din  = (gpio_num_t)MIC_SD,
      .invert_flags = {}
    }
  };
  i2s_channel_init_std_mode(micHandle, &cfg);
  i2s_channel_enable(micHandle);
}

// ── I2S speaker ──────────────────────────────────────────────
// MAX98357A on ESP32-S3: MSB, 32-bit, Mono
// We pad 16-bit samples to 32-bit before writing
void setupSpeaker() {
  i2s_chan_config_t ch = I2S_CHANNEL_DEFAULT_CONFIG(
    I2S_NUM_1, I2S_ROLE_MASTER);
  i2s_new_channel(&ch, &spkHandle, nullptr);
  i2s_std_config_t cfg = {
    .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
    .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(
                  I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = (gpio_num_t)SPK_BCK,
      .ws   = (gpio_num_t)SPK_WS,
      .dout = (gpio_num_t)SPK_DIN,
      .din  = I2S_GPIO_UNUSED,
      .invert_flags = {}
    }
  };
  i2s_channel_init_std_mode(spkHandle, &cfg);
  i2s_channel_enable(spkHandle);
}

// ── WAV header ───────────────────────────────────────────────
void makeWavHeader(uint8_t* b, uint32_t dl) {
  uint32_t br = SAMPLE_RATE * 2;
  auto w32=[&](int o,uint32_t v){memcpy(b+o,&v,4);};
  auto w16=[&](int o,uint16_t v){memcpy(b+o,&v,2);};
  memcpy(b,   "RIFF",4); w32(4,dl+36);
  memcpy(b+8, "WAVE",4); memcpy(b+12,"fmt ",4);
  w32(16,16); w16(20,1); w16(22,1);
  w32(24,SAMPLE_RATE); w32(28,br);
  w16(32,2); w16(34,16);
  memcpy(b+36,"data",4); w32(40,dl);
}

// ── Pitch shift ──────────────────────────────────────────────
void pitchShift(int16_t* src, int16_t* dst, int n) {
  const int WIN=256, HOP=WIN/2;
  memset(dst,0,n*2);
  float rp=0;
  for(int op=0; op+WIN<n; op+=HOP) {
    int s=(int)rp;
    for(int i=0;i<WIN;i++){
      if(s+i>=n) break;
      float w=0.5f*(1-cosf(2*M_PI*i/(WIN-1)));
      int32_t v=dst[op+i]+(int32_t)(src[s+i]*w);
      dst[op+i]=(int16_t)constrain(v,-32768,32767);
    }
    rp+=HOP*PITCH_FACTOR;
    if((int)rp+WIN>=n) break;
  }
}

// ── Record while button held ──────────────────────────────────
// Reads 32-bit stereo frames, extracts left channel as 16-bit
int recordWhileHeld() {
  int totalMono = 0;
  memset(recBuf, 0, MAX_SAMPLES * 2);
  const int FRAMES    = 128;
  const int BUF_BYTES = FRAMES * 2 * 4;
  int32_t tmp[FRAMES * 2];

  while(digitalRead(BTN_PIN) == LOW) {
    size_t bytesRead = 0;
    esp_err_t err = i2s_channel_read(
      micHandle, tmp, BUF_BYTES, &bytesRead, pdMS_TO_TICKS(100));
    if(err == ESP_OK && bytesRead > 0) {
      int frames = bytesRead / 8;
      for(int i = 0; i < frames && totalMono < MAX_SAMPLES; i++) {
        // Left channel, shift 32-bit to 16-bit
        recBuf[totalMono++] = (int16_t)(tmp[i*2] >> 14);
      }
    }
    Face::tick();
  }
  Serial.printf("Recorded %d samples (%.1fs)\n",
    totalMono, (float)totalMono/SAMPLE_RATE);
  return totalMono;
}

// ── Write audio to speaker — 16-bit PCM padded to 32-bit ─────
// ESP32-S3 I2S in 32-bit mode needs samples left-justified
void writeSpeaker(uint8_t* data, int len) {
  // data is 16-bit PCM bytes from VoiceRSS WAV
  // pad each 16-bit sample to 32-bit for I2S DMA
  uint32_t t0 = micros();
  const int CHUNK_SAMPLES = 256;
  int32_t out32[CHUNK_SAMPLES];
  int16_t* in16 = (int16_t*)data;
  int totalSamples = len / 2;
  int offset = 0;

  while(offset < totalSamples) {
    int batch = min(CHUNK_SAMPLES, totalSamples - offset);
    for(int i = 0; i < batch; i++) {
      // Left-justify: shift 16-bit sample to top of 32-bit word
      out32[i] = ((int32_t)in16[offset + i]) << 16;
    }
    size_t written;
    i2s_channel_write(spkHandle, out32, batch*4, &written, 200);
    offset += batch;
  }
}


// ── Network recovery helpers ───────────────────────────────────
bool ensureWiFi(uint32_t timeoutMs) {
  if (WiFi.status() == WL_CONNECTED) return true;

  Serial.println("NET: WiFi disconnected, reconnecting...");
  WiFi.disconnect(false, false);
  delay(200);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - start < timeoutMs) {
    delay(100);
    Face::tick();
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("NET: WiFi restored, IP=");
    Serial.println(WiFi.localIP());
    return true;
  }

  Serial.println("NET: WiFi reconnect failed");
  return false;
}

void printNetworkState(const char* tag) {
  Serial.printf("NET[%s]: status=%d RSSI=%d heap=%u psram=%u\n",
                tag, (int)WiFi.status(), WiFi.RSSI(),
                (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getFreePsram());
}

// ── Groq Whisper STT ─────────────────────────────────────────
String transcribe(int16_t* audio, int samples) {
  int dl = samples * 2, wl = dl + 44;
  String pre = "--ESPBuddy\r\nContent-Disposition: form-data; "
               "name=\"file\"; filename=\"a.wav\"\r\n"
               "Content-Type: audio/wav\r\n\r\n";
  String epi = "\r\n--ESPBuddy\r\nContent-Disposition: form-data; "
               "name=\"model\"\r\n\r\nwhisper-large-v3-turbo"
               "\r\n--ESPBuddy--\r\n";
  int tl = pre.length() + wl + epi.length();
  uint8_t* body = (uint8_t*)ps_malloc(tl);
  if(!body) { Serial.println("STT: OOM"); return ""; }
  int p = 0;
  memcpy(body+p, pre.c_str(), pre.length()); p += pre.length();
  makeWavHeader(body+p, dl);                 p += 44;
  memcpy(body+p, audio, dl);                 p += dl;
  memcpy(body+p, epi.c_str(), epi.length());
  if(!ensureWiFi()) { free(body); return ""; }
  printNetworkState("STT BEFORE");
  WiFiClientSecure cl; cl.setInsecure();
  cl.setTimeout(15000);
  HTTPClient http;
  http.begin(cl,"https://api.groq.com/openai/v1/audio/transcriptions");
  http.addHeader("Authorization", String("Bearer ")+GROQ_KEY);
  http.addHeader("Content-Type",
                 "multipart/form-data; boundary=ESPBuddy");
  http.setConnectTimeout(15000);
  http.setTimeout(20000);
  http.setReuse(false);
  http.addHeader("Connection", "close");
  int code = http.POST(body, tl);
  free(body);
  String res = "";
  if(code == 200) {
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, http.getString());
    res = doc["text"].as<String>();
    res.trim();
  } else {
    Serial.printf("STT error %d\n", code);
  }
  http.end();
  return res;
}

// ── Groq LLaMA 3.1 ───────────────────────────────────────────
String askAI(String userText) {

  String safeUser = userText;
  safeUser.replace("\\", "");
  safeUser.replace("\"", "'");
  safeUser.replace("\n", " ");
  chatHistory += "{\"role\":\"user\",\"content\":\""
               + safeUser + "\"},";

  turnCount++;
  if(turnCount > MAX_HISTORY) {
    chatHistory = chatHistory.substring(
      chatHistory.indexOf("},") + 2);
    turnCount--;
  }
  String msgs = "[{\"role\":\"system\",\"content\":\""
              + String(SYSTEM_PROMPT) + "\"},"
              + chatHistory.substring(0, chatHistory.length()-1)
              + "]";
  String body = "{\"model\":\"llama-3.1-8b-instant\","
                "\"max_tokens\":" + String(MAX_TOKENS) + ","
                "\"temperature\":0.8,"
                "\"messages\":" + msgs + "}";
  if(!ensureWiFi()) { return "Can you ask me again please?"; }
  printNetworkState("AI BEFORE");
  WiFiClientSecure cl; cl.setInsecure();
  cl.setTimeout(15000);
  HTTPClient http;
  http.begin(cl,"https://api.groq.com/openai/v1/chat/completions");
  http.addHeader("Authorization", String("Bearer ")+GROQ_KEY);
  http.addHeader("Content-Type","application/json");
  http.setConnectTimeout(15000);
  http.setTimeout(20000);
  http.setReuse(false);
  http.addHeader("Connection", "close");
  int code = http.POST(body);
  String reply = "Can you ask me again please?";
  if(code == 200) {
    DynamicJsonDocument doc(4096);
    deserializeJson(doc, http.getString());
    reply = doc["choices"][0]["message"]["content"].as<String>();
    reply.trim();
  } else {
    Serial.printf("AI error %d\n", code);
  }
  http.end();

  // Sanitize reply before storing in JSON history
  String safe = reply;
  safe.replace("\\", "");
  safe.replace("\"", "'");
  safe.replace("\n", " ");
  safe.replace("\r", "");
  chatHistory += "{\"role\":\"assistant\",\"content\":\""
               + safe + "\"},";

  turnCount++;
  return reply;
}

// ── VoiceRSS TTS ─────────────────────────────────────────────
void speakText(String text) {
  text.replace("\"", "'");
  text.replace("\n", " ");
  String encoded = urlEncode(text);

  // ------------------------------------------------------------
  // Streaming state
  //
  // Single producer  = WiFi task
  // Single consumer  = current speakText() task
  //
  // 96 KB ring buffer:
  //
  // 96000 / 32000 = ~3 seconds of 16-bit mono PCM.
  // ------------------------------------------------------------

  struct StreamState {

    uint8_t *buffer;

    const size_t capacity = 96 * 1024;

    volatile size_t head;
    volatile size_t tail;

    volatile bool producerDone;
    volatile bool producerError;

    volatile uint32_t pcmTotal;
    volatile uint32_t pcmProduced;

    TaskHandle_t producerTask;
  };

  StreamState *st =
    new StreamState();

  if(!st) {
    Serial.println("TTS: state allocation failed");
    return;
  }

  st->buffer =
    (uint8_t*)ps_malloc(st->capacity);

  if(!st->buffer) {
    Serial.println("TTS: 96KB ring allocation failed");
    delete st;
    return;
  }

  st->head = 0;
  st->tail = 0;
  st->producerDone = false;
  st->producerError = false;
  st->pcmTotal = 0;
  st->pcmProduced = 0;
  st->producerTask = nullptr;

  // ------------------------------------------------------------
  // Producer task
  //
  // This task does NOTHING with the speaker.
  // Its only job is:
  //
  // HTTP → WAV parser → PCM → ring buffer
  // ------------------------------------------------------------

  auto producer = [](void *parameter) {

    StreamState *st =
      (StreamState*)parameter;

    // ----------------------------------------------------------
    // URL is passed separately through a small context object.
    // ----------------------------------------------------------

    struct ProducerContext {
      StreamState *state;
      String url;
    };

    // The context is actually stored immediately before the task
    // starts through the parameter wrapper below.
    //
    // Recover it.
    ProducerContext *ctx =
      (ProducerContext*)parameter;

    st = ctx->state;

    String url = ctx->url;

    delete ctx;

    if(!ensureWiFi()) {
      st->producerError = true;
      st->producerDone = true;
      vTaskDelete(nullptr);
      return;
    }

    printNetworkState("TTS BEFORE");

    WiFiClientSecure cl;
    cl.setInsecure();
    cl.setTimeout(15000);

    HTTPClient http;
    http.setConnectTimeout(15000);
    http.setTimeout(20000);
    http.setReuse(false);

    Serial.println("TTS: connecting to VoiceRSS...");

    if(!http.begin(cl, url)) {
      Serial.println("TTS: HTTP begin failed");
      st->producerError = true;
      st->producerDone = true;
      http.end();
      cl.stop();
      vTaskDelete(nullptr);
      return;
    }

    http.addHeader("Connection", "close");

    http.addHeader("Connection", "close");

    int code = http.GET();

    Serial.printf(
      "TTS code: %d\n",
      code
    );

    if(code != 200) {

      Serial.printf(
        "TTS error %d\n",
        code
      );

      st->producerError = true;
      st->producerDone = true;

      http.end();
      cl.stop();

      vTaskDelete(nullptr);
      return;
    }

    int contentLen =
      http.getSize();

    Serial.printf(
      "TTS HTTP size: %d bytes\n",
      contentLen
    );

    WiFiClient *s =
      http.getStreamPtr();

    if(!s) {

      Serial.println(
        "TTS: stream unavailable"
      );

      st->producerError = true;
      st->producerDone = true;

      http.end();
      cl.stop();

      vTaskDelete(nullptr);
      return;
    }

    s->setTimeout(5000);

    // ----------------------------------------------------------
    // Exact network read helper.
    // ----------------------------------------------------------

    auto readExact =
      [&](uint8_t *dst,
          size_t length,
          uint32_t timeoutMs) -> bool {

      size_t received = 0;

      uint32_t lastProgress =
        millis();

      while(received < length) {

        size_t need =
          length - received;

        if(need > 1024)
          need = 1024;

        int n =
          s->readBytes(
            (char*)(dst + received),
            need
          );

        if(n > 0) {

          received += n;

          lastProgress =
            millis();

          continue;
        }

        vTaskDelay(
          pdMS_TO_TICKS(2)
        );

        if(millis() - lastProgress >
           timeoutMs) {

          return false;
        }
      }

      return true;
    };

    // ----------------------------------------------------------
    // RIFF header
    // ----------------------------------------------------------

    uint8_t riff[12];

    if(!readExact(
          riff,
          12,
          15000)) {

      Serial.println(
        "TTS: RIFF header timeout"
      );

      st->producerError = true;
      st->producerDone = true;

      http.end();
      cl.stop();

      vTaskDelete(nullptr);
      return;
    }

    if(memcmp(riff, "RIFF", 4) != 0 ||
       memcmp(riff + 8, "WAVE", 4) != 0) {

      Serial.println(
        "TTS: invalid WAV"
      );

      st->producerError = true;
      st->producerDone = true;

      http.end();
      cl.stop();

      vTaskDelete(nullptr);
      return;
    }

    Serial.println(
      "TTS: WAV OK"
    );

    // ----------------------------------------------------------
    // Locate "data" chunk
    // ----------------------------------------------------------

    uint32_t pcmBytes = 0;

    while(true) {

      uint8_t chunk[8];

      if(!readExact(
            chunk,
            8,
            15000)) {

        Serial.println(
          "TTS: WAV chunk timeout"
        );

        st->producerError = true;
        st->producerDone = true;

        http.end();

        vTaskDelete(nullptr);
        return;
      }

      uint32_t chunkSize =
        ((uint32_t)chunk[4]) |
        ((uint32_t)chunk[5] << 8) |
        ((uint32_t)chunk[6] << 16) |
        ((uint32_t)chunk[7] << 24);

      if(memcmp(
            chunk,
            "data",
            4) == 0) {

        pcmBytes =
          chunkSize;

        break;
      }

      // Skip non-data chunk.
      uint32_t skip =
        chunkSize +
        (chunkSize & 1);

      uint8_t discard[256];

      while(skip > 0) {

        size_t amount = skip;

        if(amount > sizeof(discard))
          amount = sizeof(discard);

        if(!readExact(
              discard,
              amount,
              15000)) {

          Serial.println(
            "TTS: WAV skip timeout"
          );

          st->producerError = true;
          st->producerDone = true;

          http.end();

          vTaskDelete(nullptr);
          return;
        }

        skip -= amount;
      }
    }

    pcmBytes &= ~1UL;

    Serial.printf(
      "TTS PCM data: %lu bytes\n",
      (unsigned long)pcmBytes
    );

    st->pcmTotal =
      pcmBytes;

    // ----------------------------------------------------------
    // Temporary network buffer.
    // ----------------------------------------------------------

    uint8_t networkBuf[4096];

    uint32_t remaining =
      pcmBytes;

    uint32_t lastProgress =
      millis();

    // ----------------------------------------------------------
    // Network → ring buffer
    // ----------------------------------------------------------

    while(remaining > 0) {

      // --------------------------------------------------------
      // Calculate available free space.
      // Leave one byte unused so head == tail means EMPTY.
      // --------------------------------------------------------

      size_t head =
        st->head;

      size_t tail =
        st->tail;

      size_t used;

      if(head >= tail)
        used = head - tail;
      else
        used =
          st->capacity -
          tail +
          head;

      size_t freeSpace =
        st->capacity -
        used -
        1;

      // --------------------------------------------------------
      // Ring buffer full.
      //
      // Speaker is consuming it.
      // Wait rather than overwriting audio.
      // --------------------------------------------------------

      if(freeSpace < 1024) {

        vTaskDelay(
          pdMS_TO_TICKS(2)
        );

        continue;
      }

      size_t want =
        sizeof(networkBuf);

      if(want > freeSpace)
        want = freeSpace;

      if(want > remaining)
        want = remaining;

      // Keep PCM sample aligned.
      want &= ~1UL;

      if(want == 0) {

        vTaskDelay(
          pdMS_TO_TICKS(2)
        );

        continue;
      }

      // --------------------------------------------------------
      // Read network data.
      // --------------------------------------------------------

      int n =
        s->readBytes(
          (char*)networkBuf,
          want
        );

       
      if(n <= 0) {

        vTaskDelay(
          pdMS_TO_TICKS(2)
        );

        if(millis() -
           lastProgress > 15000) {

          Serial.printf(
            "TTS: network stalled at %lu / %lu\n",
            (unsigned long)
              st->pcmProduced,
            (unsigned long)
              pcmBytes
          );

          st->producerError =
            true;

          break;
        }

        continue;
      }

      lastProgress =
        millis();

      // --------------------------------------------------------
      // Put received PCM into ring.
      // --------------------------------------------------------

      size_t first =
        n;

      if(first >
         st->capacity - head)
        first =
          st->capacity - head;

      memcpy(
        st->buffer + head,
        networkBuf,
        first
      );

      if(first < (size_t)n) {

        memcpy(
          st->buffer,
          networkBuf + first,
          n - first
        );
      }

      head =
        (head + n) %
        st->capacity;

      st->head =
        head;

      st->pcmProduced += n;

      remaining -= n;
    }

    http.end();
    cl.stop();

    st->producerDone =
      true;

    Serial.printf(
      "TTS producer finished: %lu / %lu PCM bytes\n",
      (unsigned long)
        st->pcmProduced,
      (unsigned long)
        st->pcmTotal
    );

    vTaskDelete(nullptr);
  };

  // ------------------------------------------------------------
  // Build producer context.
  // ------------------------------------------------------------

  struct ProducerContext {
    StreamState *state;
    String url;
  };

  ProducerContext *ctx =
    new ProducerContext;

  if(!ctx) {

    Serial.println(
      "TTS: producer context allocation failed"
    );

    free(st->buffer);
    delete st;

    return;
  }

  ctx->state = st;

  ctx->url =
    "https://api.voicerss.org/?key=";

  ctx->url +=
    VOICERSS_KEY;

  ctx->url +=
    "&hl=en-gb&v=Harry&r=0&c=WAV&f=16khz_16bit_mono&src=";

  ctx->url +=
    encoded;

  // ------------------------------------------------------------
  // Start producer on the other ESP32-S3 core.
  // ------------------------------------------------------------

  BaseType_t taskOK =
    xTaskCreatePinnedToCore(
      producer,
      "TTS_NET",
      8192,
      ctx,
      2,
      &st->producerTask,
      0
    );

  if(taskOK != pdPASS) {

    Serial.println(
      "TTS: producer task creation failed"
    );

    delete ctx;
    free(st->buffer);
    delete st;

    return;
  }

  // ------------------------------------------------------------
  // PREBUFFER
  //
  // Wait until approximately 0.75 second of PCM is available
  // before starting the speaker.
  //
  // 24000 bytes / 32000 bytes/sec = 0.75 sec.
  // ------------------------------------------------------------

  const size_t START_BUFFER =
    24000;

  bool started =
    false;

  uint32_t waitStart =
    millis();

  // ------------------------------------------------------------
  // Consumer side.
  //
  // This is the SAME task that called speakText().
  // While writeSpeaker() is playing, the producer task is
  // simultaneously downloading into the ring.
  // ------------------------------------------------------------

  uint8_t *playBuf = (uint8_t*)ps_malloc(8192);

  if(!playBuf) {
    Serial.println("TTS: play buffer allocation failed");
    free(st->buffer);
    delete st;
    return;
  }

  while(true) {

    size_t head =
      st->head;

    size_t tail =
      st->tail;

    size_t available;

    if(head >= tail)
      available =
        head - tail;
    else
      available =
        st->capacity -
        tail +
        head;
    
    
    // ----------------------------------------------------------
    // Before playback begins, wait for the prebuffer.
    // ----------------------------------------------------------

    if(!started) {

      if(available >=
         START_BUFFER ||
         (st->producerDone &&
          available > 0)) {

        started = true;

        Serial.printf(
          "TTS: playback starting with %u buffered bytes\n",
          (unsigned)available
        );

        Serial.printf(
          "TTS: first audio after %lu ms\n",
          (unsigned long)
            (millis() - waitStart)
        );
      }
      else {

        if(st->producerError) {

          Serial.println(
            "TTS: producer failed before playback"
          );

          break;
        }

        Face::tick();

        delay(2);

        continue;
      }
    }

    // ----------------------------------------------------------
    // Nothing currently available.
    //
    // If producer isn't finished, WAIT.
    // Do not repeat old PCM.
    // ----------------------------------------------------------

    if(available == 0) {

      if(st->producerDone)
        break;

      Face::tick();

      delay(1);

      continue;
    }

    // ----------------------------------------------------------
    // Read a contiguous portion from ring.
    // ----------------------------------------------------------

    size_t contiguous =
      st->capacity -
      tail;

    size_t take =
      available;

    if(take > contiguous)
      take = contiguous;

    if(take > 8192)
      take = 8192;
    
    // 16-bit PCM alignment.
    take &= ~1UL;

    if(take == 0) {

      delay(1);

      continue;
    }

    memcpy(
      playBuf,
      st->buffer + tail,
      take
    );

    tail =
      (tail + take) %
      st->capacity;

    st->tail =
      tail;

    // ----------------------------------------------------------
    // Existing speaker path.
    //
    // writeSpeaker() remains completely unchanged.
    // ----------------------------------------------------------

    writeSpeaker(
      playBuf,
      take
    );

    Face::tick();

    // ----------------------------------------------------------
    // Stop once producer has completely finished and the ring
    // buffer has been drained.
    // ----------------------------------------------------------

    if(st->producerDone) {

      size_t h =
        st->head;

      size_t t =
        st->tail;

      size_t left;

      if(h >= t)
        left = h - t;
      else
        left =
          st->capacity -
          t +
          h;

      if(left == 0)
        break;
    }
  }

  // ------------------------------------------------------------
  // Producer should normally already be finished.
  // Give it a short chance to exit if necessary.
  // ------------------------------------------------------------

  uint32_t cleanupStart =
    millis();

  while(!st->producerDone &&
        millis() - cleanupStart < 1000) {

    delay(2);
    yield();
  }

  Serial.printf(
    "TTS streamed: %lu / %lu PCM bytes\n",
    (unsigned long)
      st->pcmProduced,
    (unsigned long)
      st->pcmTotal
  );

  if(st->producerError) {
    Serial.println(
      "TTS: producer reported an error"
    );
  }
  free(playBuf);
  free(st->buffer);
  delete st;

  Serial.println(
    "TTS done"
  );
}


// ── Face picker — OLED only ───────────────────────────────────
FaceState pickFaceState(const String& reply) {
  String r = reply;
  r.toLowerCase();

  if (r.indexOf("love") > -1 ||
      r.indexOf("heart") > -1 ||
      r.indexOf("adorable") > -1 ||
      r.indexOf("sweet") > -1) return FACE_LOVE;

  if (r.indexOf("wow") > -1 ||
      r.indexOf("amazing") > -1 ||
      r.indexOf("surprise") > -1) return FACE_SURPRISED;

  if (r.indexOf("yay") > -1 ||
      r.indexOf("hooray") > -1 ||
      r.indexOf("awesome") > -1 ||
      r.indexOf("excited") > -1 ||
      r.indexOf("sparkly") > -1) return FACE_EXCITED;

  if (r.indexOf("haha") > -1 ||
      r.indexOf("funny") > -1 ||
      r.indexOf("giggle") > -1 ||
      r.indexOf("laugh") > -1) return FACE_HAPPY;

  if (r.indexOf("sorry") > -1 ||
      r.indexOf("sad") > -1 ||
      r.indexOf("miss") > -1) return FACE_SAD;

  if (r.indexOf("wonder") > -1 ||
      r.indexOf("curious") > -1 ||
      r.indexOf("maybe") > -1 ||
      r.indexOf("perhaps") > -1) return FACE_CURIOUS;

  if (r.indexOf("sleep") > -1 ||
      r.indexOf("tired") > -1 ||
      r.indexOf("bedtime") > -1 ||
      r.indexOf("night") > -1) return FACE_SLEEPY;

  return FACE_HAPPY;
}

void pickFace(String reply) {
  Face::setState(pickFaceState(reply));
}

// RGB mirrors the OLED face state. Animation is kept out of recording,
// network, STT, AI, and TTS playback paths.
void setRGBForFace(FaceState state) {
  switch (state) {
    case FACE_IDLE:       RGB::setMode(RGB::IDLE);       break;
    case FACE_HAPPY:      RGB::setMode(RGB::HAPPY);      break;
    case FACE_LISTENING:  RGB::setMode(RGB::LISTENING);  break;
    case FACE_THINKING:   RGB::setMode(RGB::THINKING);   break;
    case FACE_SPEAKING:   RGB::setMode(RGB::SPEAKING);   break;
    case FACE_SURPRISED:  RGB::setMode(RGB::SURPRISED);  break;
    case FACE_SAD:        RGB::setMode(RGB::SAD);        break;
    case FACE_LOVE:       RGB::setMode(RGB::LOVE);       break;
    case FACE_EXCITED:    RGB::setMode(RGB::EXCITED);    break;
    case FACE_SLEEPY:     RGB::setMode(RGB::SLEEPY);     break;
    case FACE_WINK:       RGB::setMode(RGB::WINK);       break;
    case FACE_CURIOUS:    RGB::setMode(RGB::CURIOUS);    break;
    default:              RGB::setMode(RGB::IDLE);       break;
  }
}

void setFaceAndRGB(FaceState state) {
  Face::setState(state);
  setRGBForFace(state);
}

void showFaceAndRGBFor(FaceState state, uint32_t durationMs) {
  setFaceAndRGB(state);
  uint32_t start = millis();
  while (millis() - start < durationMs) {
    Face::tick();
    RGB::tick();
    delay(5);
  }
}

// ── WiFi ─────────────────────────────────────────────────────
void connectWifi() {
  Face::oled.clearDisplay();
  Face::oled.setCursor(10,24);
  Face::oled.print("Connecting WiFi");
  Face::oled.display();
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while(WiFi.status() != WL_CONNECTED) {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    delay(300);
  }
  digitalWrite(LED_PIN, LOW);
  Face::oled.clearDisplay();
  Face::oled.setCursor(20,20); Face::oled.print("WiFi OK!");
  Face::oled.setCursor(4,34);  Face::oled.print(WiFi.localIP());
  Face::oled.display();
  delay(1000);
}

// ── Setup ────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  pinMode(BTN_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);

  Face::begin();
  RGB::begin();
  Face::oled.clearDisplay();
  Face::oled.setCursor(20,24);
  Face::oled.print("FW v" FW_VERSION);
  Face::oled.display();
  delay(800);

  recBuf   = (int16_t*)ps_malloc(MAX_SAMPLES * 2);
  pitchBuf = (int16_t*)ps_malloc(MAX_SAMPLES * 2);
  if(!recBuf || !pitchBuf) {
    Serial.println("PSRAM fail — check Tools > PSRAM > OPI PSRAM");
    while(1);
  }

  setupMic();
  setupSpeaker();
  connectWifi();
  printNetworkState("BOOT");
  OTA::checkAndUpdate(FW_VERSION);

  setFaceAndRGB(FACE_HAPPY); delay(800);
  setFaceAndRGB(FACE_IDLE);
  Serial.println("Ready! Hold BOOT and speak.");
}

// ── Loop ─────────────────────────────────────────────────────
void loop() {
  Face::tick();
  RGB::tick();

  if(digitalRead(BTN_PIN) == LOW) {
    delay(50);
    if(digitalRead(BTN_PIN) != LOW) return;

    // 1. Record while held
    setFaceAndRGB(FACE_LISTENING);
    digitalWrite(LED_PIN, HIGH);
    int samples = recordWhileHeld();
    digitalWrite(LED_PIN, LOW);

    if(samples < SAMPLE_RATE) {
      setFaceAndRGB(FACE_IDLE);
      return;
    }

    // 2. Pitch shift
    setFaceAndRGB(FACE_THINKING);
    pitchShift(recBuf, pitchBuf, samples);
    showFaceAndRGBFor(FACE_THINKING, 300);

    // 3. STT 
    String question = transcribe(pitchBuf, samples);
    Serial.println("Kid: " + question);
    if(question.length() == 0) {
      setFaceAndRGB(FACE_IDLE);
      return;
    }

    // 4. AI 
    showFaceAndRGBFor(FACE_THINKING, 300);
    String reply = askAI(question);
    Serial.println("AI: " + reply);

    // 5. Emotion, then TTS. No RGB tick during speakText().
    FaceState emotion = pickFaceState(reply);
    showFaceAndRGBFor(emotion, 700);
    setFaceAndRGB(FACE_SPEAKING);
    speakText(reply);
    setFaceAndRGB(FACE_IDLE);
  }
  delay(20);
}
