#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <ArduinoHttpClient.h>

#define PIN_RELAY 12
#define AMBANG_BATAS 0.40
#define SAMPLE_COUNT 10
#define INTERVAL 1000

// Sensor & kontrol
Adafruit_ADS1115 ads;
int16_t adc[4];
float volts[4], total[4], rerata[4];

uint8_t menguras = 0;
uint8_t tinggi = 0, tinggi_prev = 0;
uint32_t last_cek = 0;

void setup() {
  Serial.begin(9600);

  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, HIGH); // Pompa mati

  if (!ads.begin()) {
    Serial.println("Gagal inisialisasi ADS1115");
    while (1);
  }

  ads.setGain(GAIN_TWOTHIRDS);
  Serial.println("[Mulai Monitoring + SIM800L]");
}

void loop() {
  if (millis() - last_cek < INTERVAL) return;
  last_cek = millis();

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

  if (tinggi >= 25 && menguras) {
    digitalWrite(PIN_RELAY, LOW); // Pompa nyala
  // } else if (tinggi == 0 && menguras == 1) {
  } else if (tinggi < 25 && !menguras) {
    digitalWrite(PIN_RELAY, HIGH); // Pompa mati
  }

  // if (tinggi == tinggi_prev) return;
  // tinggi_prev = tinggi;

  Serial.println("--------------------------------------------------");
  Serial.print("Tinggi Air: "); Serial.print(tinggi); Serial.println("%");
  if (menguras == 1)
    Serial.println("Pompa: ON | Info: sedang menguras air");
  else
    Serial.println("Pompa: OFF");
}
