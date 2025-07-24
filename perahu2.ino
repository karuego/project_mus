#define TINY_GSM_MODEM_SIM800

#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <TinyGsmClient.h>
#include <ArduinoHttpClient.h>
#include <SoftwareSerial.h>

#define PIN_RELAY 12
// #define AMBANG_BATAS 0.40
#define AMBANG_BATAS 1.0
#define SAMPLE_COUNT 10
#define INTERVAL_CEK 1000

// Firebase info
const char FIREBASE_HOST[] = "gprs-7499d-default-rtdb.firebaseio.com";
const String FIREBASE_AUTH = "AIzaSyDlrp7Dkcjr69m5GGJVH_xc7jzxdIywfsM";
const String FIREBASE_PATH = "/MONITORING";
const int SSL_PORT = 443;

// GPRS APN (Telkomsel)
char apn[]  = "internet";
char user[] = "";
char pass[] = "";

// Modul SIM800L
SoftwareSerial sim800(8, 9);
TinyGsm modem(sim800);
TinyGsmClientSecure gsm_client_secure_modem(modem, 0);
HttpClient http_client(gsm_client_secure_modem, FIREBASE_HOST, SSL_PORT);

// Sensor & kontrol
Adafruit_ADS1115 ads;
int16_t adc[4];
float volts[4], total[4], rerata[4];

uint8_t menguras = 0;
uint8_t tinggi = 0, tinggi_prev = 0;
uint32_t last_cek = 0;

void setup() {
  Serial.begin(9600);
  sim800.begin(9600);

  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, HIGH); // Pompa mati

  if (!ads.begin()) {
    Serial.println("Gagal inisialisasi ADS1115");
    while (1);
  }

  ads.setGain(GAIN_TWOTHIRDS);
  Serial.println("[Mulai Monitoring + SIM800L]");

  modem.restart();
  Serial.print("Modem info: ");
  Serial.println(modem.getModemInfo());

  http_client.setHttpResponseTimeout(10000);
}

void loop() {
  if (millis() - last_cek >= INTERVAL_CEK) {
    last_cek = millis();
    bacaSensor();
  }

  // Data hanya dikirim jika tinggi air berubah
  if (tinggi == tinggi_prev) return;
  tinggi_prev = tinggi;

  kirimFirebase();
}

void bacaSensor() {
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

  if (rerata[0] < AMBANG_BATAS) tinggi = 0, menguras = 0;
  if (rerata[0] > AMBANG_BATAS) menguras = 1;
  if (rerata[1] > AMBANG_BATAS) tinggi = 25;
  if (rerata[2] > AMBANG_BATAS) tinggi = 50;
  if (rerata[3] > AMBANG_BATAS) tinggi = 75;

  if (tinggi >= 25 && menguras) {
    digitalWrite(PIN_RELAY, LOW); // Pompa nyala
  // } else if (tinggi == 0 && menguras == 1) {
  } else if (tinggi < 25 && !menguras) {
    digitalWrite(PIN_RELAY, HIGH); // Pompa mati
  }

  Serial.println("--------------------------------------------------");
  Serial.print("Tinggi Air: "); Serial.print(tinggi); Serial.println("%");
  if (menguras == 1)
    Serial.println("Pompa: ON | Info: sedang menguras air");
  else
    Serial.println("Pompa: OFF");
}

void kirimFirebase() {
  Serial.print(F("Menyambung ke APN: "));
  Serial.print(apn);
  if (!modem.gprsConnect(apn, user, pass)) {
    Serial.println(" - Gagal konek GPRS");
    return;
  }
  Serial.println(" - GPRS Tersambung");

  http_client.connect(FIREBASE_HOST, SSL_PORT);

  // Payload hanya berisi Level Air
  String url = FIREBASE_PATH + ".json?auth=" + FIREBASE_AUTH;
  String payload = "{\"Level Air\":" + String(tinggi) + "}";

  http_client.connectionKeepAlive();

  Serial.println("[Mengirim Level Air ke Firebase]");
  http_client.beginRequest();
  http_client.put(url);
  http_client.sendHeader("Content-Type", "application/json");
  http_client.sendHeader("Content-Length", payload.length());
  http_client.beginBody();
  http_client.print(payload);
  http_client.endRequest();

  int statusCode = http_client.responseStatusCode();
  String response = http_client.responseBody();

  Serial.print("Status code: ");
  Serial.println(statusCode);
  Serial.print("Response: ");
  Serial.println(response);

  http_client.stop();
  modem.gprsDisconnect();
}
