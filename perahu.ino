#define TINY_GSM_MODEM_SIM800

#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <TinyGsmClient.h>
#include <ArduinoHttpClient.h>
#include <SoftwareSerial.h>

#define PIN_RELAY 12
#define AMBANG_BATAS 0.40
#define SAMPLE_COUNT 10
#define INTERVAL_CEK 1000
#define INTERVAL_KIRIM 30000

// Firebase info
const char FIREBASE_HOST[] = "gprs-7499d-default-rtdb.firebaseio.com";
const String FIREBASE_AUTH = "";
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
uint32_t last_cek = 0;
uint32_t last_kirim = 0;

void setup() {
  Serial.begin(115200);
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

  if (millis() - last_kirim >= INTERVAL_KIRIM) {
    last_kirim = millis();
    kirimFirebase();
  }
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
  }

  int levelPersen = 0;
  if (rerata[0] < AMBANG_BATAS) levelPersen = 25;
  if (rerata[1] < AMBANG_BATAS) levelPersen = 40;
  if (rerata[2] < AMBANG_BATAS) levelPersen = 55;
  if (rerata[3] < AMBANG_BATAS) levelPersen = 70;

  if (levelPersen >= 25 && menguras == 0) {
    digitalWrite(PIN_RELAY, LOW); // Pompa nyala
    menguras = 1;
  } else if (levelPersen == 0 && menguras == 1) {
    digitalWrite(PIN_RELAY, HIGH); // Pompa mati
    menguras = 0;
  }

  Serial.println("--------------------------------------------------");
  Serial.print("Level Air: "); Serial.print(levelPersen); Serial.println("%");
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

  // Hitung ulang level air
  int levelPersen = 0;
  if (rerata[0] < AMBANG_BATAS) levelPersen = 25;
  if (rerata[1] < AMBANG_BATAS) levelPersen = 40;
  if (rerata[2] < AMBANG_BATAS) levelPersen = 55;
  if (rerata[3] < AMBANG_BATAS) levelPersen = 70;

  // Payload hanya berisi Level Air
  String url = FIREBASE_PATH + ".json?auth=" + FIREBASE_AUTH;
  String payload = "{\"Level Air\":" + String(levelPersen) + "}";

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
