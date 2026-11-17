#include <ESP8266WiFi.h>
#include <Wire.h>
#include <BH1750.h>
#include <ThingSpeak.h>

// =====================================
// Pengaturan Wi-Fi
// =====================================
const char* ssid = "12345678";
const char* password = "12345678";

// =====================================
// Pengaturan ThingSpeak
// =====================================
unsigned long channelID = 3563994;
const char* writeAPIKey = "SFBOPAWA8BLZR85V";

WiFiClient client;
BH1750 lightMeter;

// =====================================
// Interval reconnect Wi-Fi
// =====================================
unsigned long previousReconnect = 0;
const unsigned long reconnectInterval = 5000;

// =====================================
// Interval pembacaan sensor
// =====================================
unsigned long previousSensorRead = 0;
const unsigned long sensorInterval = 5000;

// =====================================
// Interval pengiriman ThingSpeak
// =====================================
unsigned long previousSend = 0;
const unsigned long sendInterval = 30000;

// Menyimpan nilai lux terakhir
float lux = 0;

// Status Wi-Fi sebelumnya
bool wifiPreviouslyConnected = false;


void setup() {

  Serial.begin(9600);

  // =====================================
  // Menghubungkan sensor BH1750
  // =====================================
  Wire.begin(D2, D1);

  if (lightMeter.begin()) {
    Serial.println("BH1750 berhasil terhubung");
  } else {
    Serial.println("BH1750 gagal terhubung");
  }

  // =====================================
  // Memulai koneksi Wi-Fi
  // =====================================
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.println("Memulai koneksi Wi-Fi...");

  // =====================================
  // Menghubungkan ThingSpeak
  // =====================================
  ThingSpeak.begin(client);
}


void reconnectWiFi() {

  unsigned long currentMillis = millis();

  // =====================================
  // Jika Wi-Fi terputus
  // =====================================
  if (WiFi.status() != WL_CONNECTED) {

    // Jika sebelumnya terhubung,
    // berarti baru saja terputus
    if (wifiPreviouslyConnected) {
      Serial.println("Wi-Fi terputus!");
      wifiPreviouslyConnected = false;
    }

    // Mencoba reconnect setiap 5 detik
    if (currentMillis - previousReconnect >= reconnectInterval) {

      previousReconnect = currentMillis;

      Serial.println("Mencoba reconnect Wi-Fi...");

      WiFi.disconnect();
      WiFi.begin(ssid, password);
    }

  }

  // =====================================
  // Jika Wi-Fi terhubung
  // =====================================
  else {

    // Hanya tampil ketika sebelumnya
    // Wi-Fi dalam kondisi terputus
    if (!wifiPreviouslyConnected) {

      wifiPreviouslyConnected = true;

      Serial.println("Wi-Fi terhubung kembali!");
      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());
    }
  }
}


void loop() {

  // =====================================
  // Memeriksa koneksi Wi-Fi
  // =====================================
  reconnectWiFi();

  unsigned long currentMillis = millis();

  // =====================================
  // Membaca sensor setiap 5 detik
  // =====================================
  if (currentMillis - previousSensorRead >= sensorInterval) {

    previousSensorRead = currentMillis;

    lux = lightMeter.readLightLevel();

    Serial.print("Intensitas Cahaya: ");
    Serial.print(lux);
    Serial.println(" lux");
  }


  // =====================================
  // Mengirim data ke ThingSpeak setiap 30 detik
  // =====================================
  if (currentMillis - previousSend >= sendInterval) {

    previousSend = currentMillis;

    if (WiFi.status() == WL_CONNECTED && lux >= 0) {

      ThingSpeak.setField(1, lux);
      ThingSpeak.setField(2, lux);

      int status = ThingSpeak.writeFields(
        channelID,
        writeAPIKey
      );

      if (status == 200) {

        Serial.println("Data berhasil dikirim ke ThingSpeak!");

      } else {

        Serial.print("Gagal mengirim data. Kode: ");
        Serial.println(status);
      }

    } else {

      Serial.println("Wi-Fi terputus atau pembacaan sensor gagal.");
    }
  }

  delay(100);
}
