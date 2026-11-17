
#include <WiFi.h>
#include <MQTT.h>
#include <time.h>
#include <math.h>

// =====================================
// KONFIGURASI
// =====================================

const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";

const char* MQTT_HOST = "test.mosquitto.org";
const int MQTT_PORT = 1883;
const char* MQTT_TOPIC = "wokwi/s6/qos1/500byte";

#define TOTAL_MESSAGES 100
#define INTERVAL_MS 2000
#define TIMEOUT_MS 5000
#define PAYLOAD_SIZE 500
#define MQTT_QOS 1

WiFiClient network;
MQTTClient mqtt(1024);

// =====================================
// VARIABEL
// =====================================

bool mqttReceived = false;

unsigned long sendTime = 0;
unsigned long receiveTime = 0;
unsigned long rtt = 0;

int currentSeq = 0;
int successCount = 0;
int lostCount = 0;

unsigned long rttData[TOTAL_MESSAGES];
bool receivedData[TOTAL_MESSAGES];

// =====================================
// BUAT PAYLOAD 500 BYTE
// =====================================

String createPayload(int seq) {
  char buffer[7];
  sprintf(buffer, "%06d", seq);

  String payload = String(buffer);

  while (payload.length() < PAYLOAD_SIZE) {
    payload += "X";
  }

  return payload;
}

// =====================================
// WAKTU WIB
// =====================================

String getTimeNow() {
  time_t now;
  time(&now);

  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  char buffer[10];
  strftime(buffer, sizeof(buffer), "%H:%M:%S", &timeinfo);

  return String(buffer);
}

// =====================================
// CALLBACK MQTT
// =====================================

void messageReceived(String &topic, String &payload) {
  int seq = payload.substring(0, 6).toInt();

  if (topic == MQTT_TOPIC &&
      seq == currentSeq &&
      !mqttReceived) {

    receiveTime = millis();
    rtt = receiveTime - sendTime;
    mqttReceived = true;
  }
}

// =====================================
// KONEKSI MQTT
// =====================================

void connectMQTT() {
  mqtt.begin(MQTT_HOST, MQTT_PORT, network);
  mqtt.onMessage(messageReceived);

  while (!mqtt.connect("Wokwi-S6-QoS1")) {
    delay(1000);
  }

  mqtt.subscribe(MQTT_TOPIC, MQTT_QOS);
}

// =====================================
// KIRIM 1 PESAN
// =====================================

void runMQTT(int seq) {
  String payload = createPayload(seq);

  currentSeq = seq;
  mqttReceived = false;

  sendTime = millis();
  receiveTime = 0;
  rtt = 0;

  bool published = mqtt.publish(
    MQTT_TOPIC,
    payload,
    false,
    MQTT_QOS
  );

  if (!published) {
    lostCount++;
    receivedData[seq - 1] = false;

    Serial.printf(
      "S6,%d,%s,%d,%lu,,,%d,PUBLISH_FAILED,%d\n",
      seq,
      getTimeNow().c_str(),
      PAYLOAD_SIZE,
      sendTime,
      MQTT_QOS,
      WiFi.RSSI()
    );

    return;
  }

  unsigned long startWait = millis();

  while (!mqttReceived &&
         millis() - startWait < TIMEOUT_MS) {

    mqtt.loop();
    delay(1);
  }

  if (mqttReceived) {
    successCount++;

    rttData[seq - 1] = rtt;
    receivedData[seq - 1] = true;

    Serial.printf(
      "S6,%d,%s,%d,%lu,%lu,%lu,%d,SUCCESS,%d\n",
      seq,
      getTimeNow().c_str(),
      PAYLOAD_SIZE,
      sendTime,
      receiveTime,
      rtt,
      MQTT_QOS,
      WiFi.RSSI()
    );

  } else {
    lostCount++;
    receivedData[seq - 1] = false;

    Serial.printf(
      "S6,%d,%s,%d,%lu,,,%d,TIMEOUT,%d\n",
      seq,
      getTimeNow().c_str(),
      PAYLOAD_SIZE,
      sendTime,
      MQTT_QOS,
      WiFi.RSSI()
    );
  }
}

// =====================================
// MEAN
// =====================================

float calculateMean() {
  if (successCount == 0) return 0;

  double total = 0;

  for (int i = 0; i < TOTAL_MESSAGES; i++) {
    if (receivedData[i]) {
      total += rttData[i];
    }
  }

  return total / successCount;
}

// =====================================
// AMBIL DAN URUTKAN DATA RTT
// =====================================

int getSortedData(unsigned long data[]) {
  int count = 0;

  for (int i = 0; i < TOTAL_MESSAGES; i++) {
    if (receivedData[i]) {
      data[count++] = rttData[i];
    }
  }

  for (int i = 0; i < count - 1; i++) {
    for (int j = 0; j < count - i - 1; j++) {
      if (data[j] > data[j + 1]) {
        unsigned long temp = data[j];
        data[j] = data[j + 1];
        data[j + 1] = temp;
      }
    }
  }

  return count;
}

// =====================================
// MEDIAN
// =====================================

float calculateMedian() {
  if (successCount == 0) return 0;

  unsigned long data[TOTAL_MESSAGES];
  int count = getSortedData(data);

  if (count % 2 == 0) {
    return (
      data[count / 2 - 1] +
      data[count / 2]
    ) / 2.0;
  }

  return data[count / 2];
}

// =====================================
// STANDARD DEVIATION SAMPEL
// =====================================

float calculateStdDev() {
  if (successCount <= 1) return 0;

  float mean = calculateMean();
  double total = 0;

  for (int i = 0; i < TOTAL_MESSAGES; i++) {
    if (receivedData[i]) {
      double diff = rttData[i] - mean;
      total += diff * diff;
    }
  }

  return sqrt(total / (successCount - 1));
}

// =====================================
// P95
// =====================================

float calculateP95() {
  if (successCount == 0) return 0;

  unsigned long data[TOTAL_MESSAGES];
  int count = getSortedData(data);

  int index = ceil(0.95 * count) - 1;

  if (index < 0) index = 0;
  if (index >= count) index = count - 1;

  return data[index];
}

// =====================================
// SETUP
// =====================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  // WIFI
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  // NTP WIB
  configTime(
    7 * 3600,
    0,
    "pool.ntp.org",
    "time.nist.gov"
  );

  struct tm timeinfo;

  while (!getLocalTime(&timeinfo)) {
    delay(500);
  }

  // MQTT
  connectMQTT();

  // HEADER CSV
  Serial.println(
    "skenario,seq,time,payload_byte,t_kirim_ms,t_terima_ms,rtt_ms,mqtt_qos,status,rssi_dbm"
  );

  // EKSPERIMEN
  unsigned long nextSendTime = millis();

  for (int seq = 1; seq <= TOTAL_MESSAGES; seq++) {
    while ((long)(millis() - nextSendTime) < 0) {
      mqtt.loop();
      delay(1);
    }

    nextSendTime += INTERVAL_MS;

    runMQTT(seq);
  }

  // =====================================
  // HASIL S6
  // =====================================

  mqtt.disconnect();

  float pdr =
    (float)successCount /
    TOTAL_MESSAGES * 100.0;

  Serial.println();
  Serial.println("==============================================================");
  Serial.println("                         HASIL S6");
  Serial.println("==============================================================");

  Serial.printf(
    "| Total Sent      : %-10d                    |\n",
    TOTAL_MESSAGES
  );

  Serial.printf(
    "| Received        : %-10d                    |\n",
    successCount
  );

  Serial.printf(
    "| Lost            : %-10d                    |\n",
    lostCount
  );

  Serial.printf(
    "| PDR             : %-10.2f %%                 |\n",
    pdr
  );

  Serial.println("--------------------------------------------------------------");

  Serial.printf(
    "| Mean RTT        : %-10.2f ms                |\n",
    calculateMean()
  );

  Serial.printf(
    "| Median RTT      : %-10.2f ms                |\n",
    calculateMedian()
  );

  Serial.printf(
    "| Std. Deviation  : %-10.2f ms                |\n",
    calculateStdDev()
  );

  Serial.printf(
    "| P95 RTT         : %-10.2f ms                |\n",
    calculateP95()
  );

  Serial.println("==============================================================");
  Serial.println("                         S6 SELESAI");
  Serial.println("==============================================================");
}

// =====================================
// LOOP
// =====================================

void loop() {
  delay(1000);
}
