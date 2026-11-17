#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include <math.h>

// =====================================
// KONFIGURASI
// =====================================

const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";

const char* SERVER_URL = "http://httpbin.org/post";

#define PAYLOAD_SIZE 20
#define TOTAL_MESSAGE 100
#define INTERVAL_MS 2000
#define TIMEOUT_MS 5000

unsigned long rttData[TOTAL_MESSAGE];

int receivedCount = 0;
int lostCount = 0;

// =====================================
// MEMBUAT PAYLOAD 20 BYTE
// =====================================

String createPayload(int seq) {

  String payload =
    "{\"seq\":" + String(seq) + ",\"x\":\"A\"}";

  // Padding menggunakan whitespace JSON
  while (payload.length() < PAYLOAD_SIZE) {
    payload += " ";
  }

  return payload;
}

// =====================================
// HITUNG MEAN
// =====================================

double calculateMean() {

  if (receivedCount == 0)
    return 0;

  double total = 0;

  for (int i = 0; i < receivedCount; i++) {
    total += rttData[i];
  }

  return total / receivedCount;
}

// =====================================
// HITUNG MEDIAN
// =====================================

double calculateMedian() {

  if (receivedCount == 0)
    return 0;

  unsigned long temp[TOTAL_MESSAGE];

  for (int i = 0; i < receivedCount; i++) {
    temp[i] = rttData[i];
  }

  for (int i = 0; i < receivedCount - 1; i++) {
    for (int j = i + 1; j < receivedCount; j++) {

      if (temp[j] < temp[i]) {
        unsigned long swap = temp[i];
        temp[i] = temp[j];
        temp[j] = swap;
      }
    }
  }

  if (receivedCount % 2 == 0) {

    int a = receivedCount / 2 - 1;
    int b = receivedCount / 2;

    return (temp[a] + temp[b]) / 2.0;

  } else {

    return temp[receivedCount / 2];
  }
}

// =====================================
// HITUNG STANDARD DEVIATION
// =====================================

double calculateStdDev() {

  if (receivedCount <= 1)
    return 0;

  double mean = calculateMean();
  double sum = 0;

  for (int i = 0; i < receivedCount; i++) {

    double diff = rttData[i] - mean;
    sum += diff * diff;
  }

  return sqrt(sum / (receivedCount - 1));
}

// =====================================
// HITUNG P95
// =====================================

double calculateP95() {

  if (receivedCount == 0)
    return 0;

  unsigned long temp[TOTAL_MESSAGE];

  for (int i = 0; i < receivedCount; i++) {
    temp[i] = rttData[i];
  }

  for (int i = 0; i < receivedCount - 1; i++) {
    for (int j = i + 1; j < receivedCount; j++) {

      if (temp[j] < temp[i]) {
        unsigned long swap = temp[i];
        temp[i] = temp[j];
        temp[j] = swap;
      }
    }
  }

  int index = ceil(0.95 * receivedCount) - 1;

  if (index < 0)
    index = 0;

  if (index >= receivedCount)
    index = receivedCount - 1;

  return temp[index];
}

// =====================================
// SETUP
// =====================================

void setup() {

  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("==============================================================");
  Serial.println("                 EKSPERIMEN S1 - HTTP");
  Serial.println("==============================================================");

  Serial.println();
  Serial.println("Konfigurasi:");
  Serial.println("Server           : httpbin.org");
  Serial.println("Port             : 80");
  Serial.println("HTTP Connection  : KEEP-ALIVE");
  Serial.println("Payload          : 20 byte");
  Serial.println("Total Message    : 100");
  Serial.println("Interval         : 2 seconds");
  Serial.println("Timeout          : 5 seconds");

  // =====================================
  // CEK PAYLOAD
  // =====================================

  Serial.println();
  Serial.println("Payload check...");

  String testPayload = createPayload(1);

  Serial.print("Payload size     : ");
  Serial.print(testPayload.length());
  Serial.println(" byte");

  if (testPayload.length() == PAYLOAD_SIZE) {
    Serial.println("Payload check    : OK");
  } else {
    Serial.println("Payload check    : ERROR");
  }

  // =====================================
  // CONNECT WIFI
  // =====================================

  Serial.println();
  Serial.print("Connecting WiFi");

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi CONNECTED");

  Serial.print("IP Address       : ");
  Serial.println(WiFi.localIP());

  // =====================================
  // SINKRONISASI WAKTU WIB
  // =====================================

  Serial.println();
  Serial.println("Synchronizing time...");

  configTime(
    7 * 3600,
    0,
    "pool.ntp.org",
    "time.nist.gov"
  );

  struct tm timeinfo;

  while (!getLocalTime(&timeinfo)) {
    Serial.print(".");
    delay(500);
  }

  Serial.println();
  Serial.println("Time synchronized");

  Serial.printf(
    "Current time     : %02d:%02d:%02d WIB\n",
    timeinfo.tm_hour,
    timeinfo.tm_min,
    timeinfo.tm_sec
  );

  // =====================================
  // HEADER CSV
  // =====================================

  Serial.println();
  Serial.println("==============================================================");

  Serial.println(
    "skenario,seq,time,payload_byte,t_kirim_ms,t_terima_ms,rtt_ms,http_code,status,rssi_dbm"
  );

  // =====================================
  // HTTP KEEP-ALIVE
  // =====================================

  HTTPClient http;

  http.setReuse(true);
  http.setConnectTimeout(TIMEOUT_MS);
  http.setTimeout(TIMEOUT_MS);

  unsigned long nextSendTime = millis();

  for (int seq = 1; seq <= TOTAL_MESSAGE; seq++) {

    // Tunggu jadwal pengiriman
    while ((long)(millis() - nextSendTime) < 0) {
      delay(1);
    }

    nextSendTime += INTERVAL_MS;

    // =====================================
    // BUAT PAYLOAD
    // =====================================

    String payload = createPayload(seq);

    unsigned int payloadByte = payload.length();

    int httpCode = -1;

    // BEGIN HANYA SEKALI
    if (seq == 1) {

      http.begin(SERVER_URL);

      http.addHeader("Content-Type", "application/json");
      http.addHeader("Connection", "keep-alive");
    }

    // =====================================
    // CATAT WAKTU KIRIM DAN TERIMA
    // =====================================

    unsigned long tKirim = millis();

    httpCode = http.POST(payload);

    unsigned long tTerima = millis();

    unsigned long rtt = tTerima - tKirim;

    // =====================================
    // AMBIL WAKTU WIB
    // =====================================

    time_t now;
    time(&now);

    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    char timeString[10];

    strftime(
      timeString,
      sizeof(timeString),
      "%H:%M:%S",
      &timeinfo
    );

    // =====================================
    // RSSI
    // =====================================

    long rssi = WiFi.RSSI();

    // =====================================
    // STATUS DAN OUTPUT CSV
    // =====================================

    if (httpCode >= 200 && httpCode < 300) {

      receivedCount++;

      rttData[receivedCount - 1] = rtt;

      Serial.printf(
        "S1,%d,%s,%u,%lu,%lu,%lu,%d,SUCCESS,%ld\n",
        seq,
        timeString,
        payloadByte,
        tKirim,
        tTerima,
        rtt,
        httpCode,
        rssi
      );

    } else {

      lostCount++;

      Serial.printf(
        "S1,%d,%s,%u,%lu,,,,LOST,%ld\n",
        seq,
        timeString,
        payloadByte,
        tKirim,
        rssi
      );
    }
  }

  // =====================================
  // TUTUP HTTP
  // =====================================

  http.end();

  // =====================================
  // HITUNG PDR
  // =====================================

  double pdr =
    ((double)receivedCount / TOTAL_MESSAGE) * 100.0;

  // =====================================
  // HASIL AKHIR
  // =====================================

  Serial.println();
  Serial.println("==============================================================");
  Serial.println("                       HASIL S1");
  Serial.println("==============================================================");

  Serial.printf(
    "| Total Sent      : %3d                         |\n",
    TOTAL_MESSAGE
  );

  Serial.printf(
    "| Received        : %3d                         |\n",
    receivedCount
  );

  Serial.printf(
    "| Lost            : %3d                         |\n",
    lostCount
  );

  Serial.printf(
    "| PDR             : %6.2f %%                    |\n",
    pdr
  );

  Serial.println("--------------------------------------------------------------");

  Serial.printf(
    "| Mean RTT        : %7.2f ms                   |\n",
    calculateMean()
  );

  Serial.printf(
    "| Median RTT      : %7.2f ms                   |\n",
    calculateMedian()
  );

  Serial.printf(
    "| Std. Deviation  : %7.2f ms                   |\n",
    calculateStdDev()
  );

  Serial.printf(
    "| P95 RTT         : %7.2f ms                   |\n",
    calculateP95()
  );

  Serial.println("==============================================================");
  Serial.println("                       S1 SELESAI");
  Serial.println("==============================================================");
}

void loop() {
}

