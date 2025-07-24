#include <WiFi.h>
#include <Wire.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Adafruit_ADS1X15.h>

#define WIFI_SSID "mobil_rc"
#define WIFI_PASS "0806040200"
#define WIFI_CHAN 6

#define SERVER_HOST "api-karno21.deno.dev"
#define SERVER_PORT 443

#define PIN_RELAY 4
#define PIN_SDA 21
#define PIN_SCL 22

// #define AMBANG_BATAS 0.38
#define AMBANG_BATAS 2.8
#define SAMPLE_COUNT 10
#define INTERVAL 1000

// Sertifikat root CA Let's Encrypt
// Kosongkan jika ingin lewati validasi fingerprint (kurang aman)
#define SERVER_FINGERPRINT ""

// HTTPClient http;
//WiFiClient client;
WiFiClientSecure client;
Adafruit_ADS1115 ads;
QueueHandle_t adcQueue;

uint32_t last_time = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("Mulai");
  
  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, HIGH); // Pompa mati

  if (!ads.begin()) {
  // if (!ads.begin()) {
    Serial.println("Gagal inisialisasi ADS1115");
    while (1);
  }
  ads.setGain(GAIN_ONE);
  // ads.setDataRate(RATE_ADS1115_32SPS);

  Serial.print(F("Connecting to "));
  Serial.println(WIFI_SSID);

  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

  WiFi.begin(WIFI_SSID, WIFI_PASS, WIFI_CHAN);
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - last_time < 500) continue;
    last_time = millis();
    Serial.print(".");
  }
  
  Serial.println(F("\nWiFi connected"));

  // Lewati verifikasi sertifikat (kurang aman)
  client.setInsecure();

  // Buat queue untuk komunikasi antar-core
  adcQueue = xQueueCreate(10, sizeof(uint8_t));

  // Task di Core 0 untuk baca ADS1115
  xTaskCreatePinnedToCore(
    task_baca_sensor, // Fungsi task
    "ADS1115_Reader", // Nama task
    10000,            // Stack size
    NULL,             // Parameter
    1,                // Priority
    NULL,             // Task handle
    0                 // Core 0
  );

  // Task di Core 1 untuk kirim data
  xTaskCreatePinnedToCore(
    task_kirim_data,
    "HTTP_Sender",
    10000,
    NULL,
    1,
    NULL,
    1
  );

  Serial.println("[Mulai Monitoring]");
}

void loop() {
  vTaskDelete(NULL); // Hapus task loop default
}

void task_baca_sensor(void* pvParameters) {
  int16_t adc[4];
  float volts[4], total[4], rerata[4];

  uint8_t menguras = 0;
  uint8_t tinggi = 0, tinggi_prev = 0;
  uint8_t is_aktif = 0;

  // UBaseType_t stackLeft = uxTaskGetStackHighWaterMark(NULL);
  // Serial.print("Stack remaining: "); Serial.println(stackLeft);

  while (1) {
    // Jeda selama 1000ms
    // vTaskDelay(pdMS_TO_TICKS(1000));
    if (millis() - last_time >= 1000) {
      for (uint8_t i = 0; i < 4; i++) {
        Serial.print("A"), Serial.print(i), Serial.print(" "), Serial.println(rerata[i]);
      }

      Serial.print("Tinggi Air: "); Serial.print(tinggi); Serial.println("%");
    
      if (is_aktif) Serial.println("Pompa: ON");
      else Serial.println("Pompa: OFF");
      Serial.println("--------------------------------------------------");
    }

    // if (tinggi >= 25 && menguras) {
    if (tinggi >= 25) {
      if (menguras) {
        digitalWrite(PIN_RELAY, LOW); // Pompa nyala
        is_aktif = 1;
      }
    // } else if (tinggi == 0 && menguras == 1) {
    // } else if (tinggi < 25 && !menguras) {
    } else {
      if (!menguras) {
        digitalWrite(PIN_RELAY, HIGH); // Pompa mati
        is_aktif = 0;
      }
    }

    // Data hanya dikirim jika tinggi air berubah
    if (tinggi != tinggi_prev) {
      tinggi_prev = tinggi;

      // Kirim ke queue
      xQueueSend(adcQueue, &tinggi, portMAX_DELAY);
    }

    for (uint8_t i = 0; i < 4; i++) total[i] = 0;

    for (uint8_t i = 0; i < SAMPLE_COUNT; i++) {
      for (uint8_t j = 0; j < 4; j++) {
        adc[j] = ads.readADC_SingleEnded(j);
        volts[j] = ads.computeVolts(adc[j]);
        total[j] += volts[j];
      }
      delay(1);
    }

    for (uint8_t i = 0; i < 4; i++) {
      rerata[i] = total[i] / SAMPLE_COUNT;
    }

    // if (rerata[0] >= AMBANG_BATAS) {
    if (rerata[0] < AMBANG_BATAS) {
      tinggi = 0;
      menguras = 0;
      continue;
    }

    menguras = 1;
    // if (rerata[1] >= AMBANG_BATAS) continue; tinggi = 25;
    // if (rerata[2] >= AMBANG_BATAS) continue; tinggi = 50;
    // if (rerata[3] >= AMBANG_BATAS) continue; tinggi = 75;
    if (rerata[1] < AMBANG_BATAS) continue; tinggi = 25;
    if (rerata[2] < AMBANG_BATAS) continue; tinggi = 50;
    if (rerata[3] < AMBANG_BATAS) continue; tinggi = 75;
  }
}

void task_kirim_data(void* pvParameters) {
  uint8_t tinggi = 0;

  while (1) {
    if (xQueueReceive(adcQueue, &tinggi, portMAX_DELAY) != pdTRUE)
      continue;
    
    Serial.println(F("Mengirim data..."));
    if (!client.connect(SERVER_HOST, SERVER_PORT)) {
      Serial.println(F("[ERROR] Gagal koneksi ke Server."));
      continue;
    }

    String url = String("/?t=") + tinggi;

    /*http.begin(url);
    http.setUserAgent("ESP32");
    http.addHeader("Connection", "close");

    int httpCode = http.sendRequest("HEAD");

    if (httpCode > 0) {
      Serial.printf("HEAD Response Code: %d\n", httpCode);
      // Cetak semua header
      String header = http.getString(); // kosong karena HEAD tidak punya body
      Serial.println("Headers diterima:");
      for (int i = 0; i < http.headers(); i++) {
        Serial.print(http.headerName(i));
        Serial.print(": ");
        Serial.println(http.header(i));
      }
    } else {
      Serial.printf("HEAD request gagal, error: %s\n", http.errorToString(httpCode).c_str());
    }

    http.end();*/

    // Kirim request
    // client.print(F("GET "));
    client.print(F("HEAD "));
    client.print(url);
    client.println(F(" HTTP/1.1"));
    client.print(F("Host: "));
    client.println(SERVER_HOST);
    client.println(F("User-Agent: ESP32"));
    client.println(F("Connection: close"));
    client.println(); // Baris kosong di akhir header

    // Tunggu respons
    while (client.connected()) {
      String line = client.readStringUntil('\n');
      if (line == "\r") break; // Akhir header
    }

    Serial.println(F("Isi response:"));
    while (client.available()) {
      String line = client.readStringUntil('\n');
      Serial.println(line);
    }
    Serial.println(F("Selesai mengirim."));
    
    // Menutup koneksi
    client.stop();
  }
}
