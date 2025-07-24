#include <WiFi.h>
#include <Wire.h>
#include <WiFiClientSecure.h>
#include <Adafruit_ADS1X15.h>

#define API_KEY "AIzaSyAdkCbVqYoDq8WVbgm8D1_CsKZouoZa00g"
#define DB_URL "https://perahu-d9d35-default-rtdb.asia-southeast1.firebasedatabase.app"

#define WIFI_SSID "mobil_rc"
#define WIFI_PASS "0806040200"
#define WIFI_CHAN 6

#define SERVER_HOST "api-karno21.deno.dev"
#define SERVER_PORT 443

#define PIN_RELAY 4
#define PIN_SDA 21
#define PIN_SCL 22

#define AMBANG_BATAS 0.40
#define SAMPLE_COUNT 10
#define INTERVAL 1000

// Sertifikat root CA Let's Encrypt
// Kosongkan jika ingin lewati validasi fingerprint (kurang aman)
#define SERVER_FINGERPRINT ""

//WiFiClient client;
WiFiClientSecure client;

// Sensor & kontrol
Adafruit_ADS1115 ads;

int16_t adc[4];
float volts[4], total[4], rerata[4];

uint8_t menguras = 0;
uint8_t tinggi = 0, tinggi_prev = 0;

uint32_t last_time = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("Mulai");
  
  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, HIGH); // Pompa mati

  if (!ads.begin()) {
    Serial.println("Gagal inisialisasi ADS1115");
    while (1);
  }

  ads.setGain(GAIN_TWOTHIRDS);

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

  Serial.println("[Mulai Monitoring]");
}

void loop() {
  if (millis() - last_time >= INTERVAL) {
    last_time = millis();
    bacaSensor();

    Serial.println("--------------------------------------------------");
    Serial.print("Tinggi Air: "); Serial.print(tinggi); Serial.println("%");
    if (menguras == 1)
      Serial.println("Pompa: ON");
    else
      Serial.println("Pompa: OFF");
  }

  // Data hanya dikirim jika tinggi air berubah
  if (tinggi == tinggi_prev) return;
  tinggi_prev = tinggi;

  if (tinggi >= 25 && menguras) {
    digitalWrite(PIN_RELAY, LOW); // Pompa nyala
  // } else if (tinggi == 0 && menguras == 1) {
  } else if (tinggi < 25 && !menguras) {
    digitalWrite(PIN_RELAY, HIGH); // Pompa mati
  }

  kirimData();
}

inline void bacaSensor() {
  for (int i = 0; i < 4; i++) total[i] = 0;

  for (uint8_t i = 0; i < SAMPLE_COUNT; i++) {
    for (int j = 0; j < 4; j++) {
      adc[j] = ads.readADC_SingleEnded(j);
      volts[j] = ads.computeVolts(adc[j]);
      total[j] += volts[j];
    }
    delay(1);
  }

  for (int i = 0; i < 4; i++) {
    rerata[i] = total[i] / SAMPLE_COUNT;
    Serial.print("A"), Serial.print(i), Serial.print(" "), Serial.println(rerata[i]);
  }

  if (rerata[0] > AMBANG_BATAS) tinggi = 0, menguras = 0;
  if (rerata[0] < AMBANG_BATAS) menguras = 1;
  if (rerata[1] < AMBANG_BATAS) tinggi = 25;
  if (rerata[2] < AMBANG_BATAS) tinggi = 50;
  if (rerata[3] < AMBANG_BATAS) tinggi = 75;
}

inline void kirimData() {
  Serial.println(F("Mengirim data..."));
  if (!client.connect(SERVER_HOST, SERVER_PORT)) {
    Serial.println(F("[ERROR] Gagal koneksi ke Server."));
    return;
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
