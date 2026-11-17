
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
const char* MQTT_TOPIC = "wokwi/s3/qos0/20byte";

#define TOTAL_MESSAGES 100
#define INTERVAL_MS 2000
#define TIMEOUT_MS 5000
#define PAYLOAD_SIZE 20
#define MQTT_QOS 0

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
// PAYLOAD
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

  Serial.print("Connecting MQTT...");

  while (!mqtt.connect("Wokwi-S3-QoS0")) {
    Serial.print(".");
    delay(1000);
  }

  Serial.println(" CONNECTED");

  mqtt.subscribe(MQTT_TOPIC, MQTT_QOS);
}

// =====================================
// KIRIM 1 PESAN
// =====================================

void runMQTT(int seq) {
  String payload = createPayload(seq);
  unsigned int payloadByte = payload.length();

  currentSeq = seq;
  mqttReceived = false;
  receiveTime = 0;
  rtt = 0;

  sendTime = millis();

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
      "S3,%d,%s,%u,%lu,,,,%d,LOST,%ld\n",
      seq,
      getTimeNow().c_str(),
      payloadByte,
      sendTime,
      MQTT_QOS,
      (long)WiFi.RSSI()
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
      "S3,%d,%s,%u,%lu,%lu,%lu,%d,SUCCESS,%ld\n",
      seq,
      getTimeNow().c_str(),
      payloadByte,
      sendTime,
      receiveTime,
      rtt,
      MQTT_QOS,
      (long)WiFi.RSSI()
    );

  } else {
    lostCount++;
    receivedData[seq - 1] = false;

    Serial.printf(
      "S3,%d,%s,%u,%lu,,,,%d,LOST,%ld\n",
      seq,
      getTimeNow().c_str(),
      payloadByte,
      sendTime,
      MQTT_QOS,
      (long)WiFi.RSSI()
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
// MEDIAN
// =====================================

float calculateMedian() {
  if (successCount == 0) return 0;

  unsigned long data[TOTAL_MESSAGES];
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

  if (count % 2 == 0) {
    return (data[count / 2 - 1] + data[count / 2]) / 2.0;
  }

  return data[count / 2];
}

// =====================================
// STANDARD DEVIATION
// =====================================

float calculateStdDev() {
  if (successCount == 0) return 0;

  float mean = calculateMean();
  double total = 0;

  for (int i = 0; i < TOTAL_MESSAGES; i++) {
    if (receivedData[i]) {
      double diff = rttData[i] - mean;
      total += diff * diff;
    }
  }

  return sqrt(total / successCount);
}

// =====================================
// P95
// =====================================

float calculateP95() {
  if (successCount == 0) return 0;

  unsigned long data[TOTAL_MESSAGES];
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

  Serial.println();
  Serial.println("==============================================================");
  Serial.println("             S3 - MQTT QoS 0 - 20 BYTE");
  Serial.println("==============================================================");

  Serial.println("Broker           : test.mosquitto.org");
  Serial.println("Port             : 1883");
  Serial.println("MQTT QoS         : 0");
  Serial.println("Payload size     : 20 byte");
  Serial.println("Total Message    : 100");
  Serial.println("Interval         : 2 seconds");
  Serial.println("Timeout          : 5 seconds");

  // ===================================
  // WIFI
  // ===================================

  Serial.println();
  Serial.print("Connecting WiFi");

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }

  Serial.println(" CONNECTED");

  Serial.print("IP Address       : ");
  Serial.println(WiFi.localIP());

  Serial.print("RSSI             : ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");

  // ===================================
  // NTP WIB
  // ===================================

  configTime(
    7 * 3600,
    0,
    "pool.ntp.org",
    "time.nist.gov"
  );

  struct tm timeinfo;

  Serial.print("Syncing time");

  while (!getLocalTime(&timeinfo)) {
    Serial.print(".");
    delay(500);
  }

  Serial.println();

  Serial.printf(
    "Current time     : %02d:%02d:%02d WIB\n",
    timeinfo.tm_hour,
    timeinfo.tm_min,
    timeinfo.tm_sec
  );

  // ===================================
  // CEK PAYLOAD
  // ===================================

  Serial.print("Payload check    : ");
  Serial.print(createPayload(1).length());
  Serial.println(" byte");

  // ===================================
  // MQTT
  // ===================================

  connectMQTT();

  Serial.println();
  Serial.println("skenario,seq,time,payload_byte,t_kirim_ms,t_terima_ms,rtt_ms,mqtt_qos,status,rssi_dbm");

  // ===================================
  // EKSPERIMEN
  // ===================================

  unsigned long nextSendTime = millis();

  for (int seq = 1; seq <= TOTAL_MESSAGES; seq++) {
    while ((long)(millis() - nextSendTime) < 0) {
      mqtt.loop();
      delay(1);
    }

    nextSendTime += INTERVAL_MS;

    runMQTT(seq);
  }

  // ===================================
  // HASIL
  // ===================================

  mqtt.disconnect();

  float pdr =
    (float)successCount / TOTAL_MESSAGES * 100.0;

  Serial.println();
  Serial.println("==============================================================");
  Serial.println("                     HASIL S3");
  Serial.println("==============================================================");

  Serial.printf("| Total Sent      : %d\n", TOTAL_MESSAGES);
  Serial.printf("| Received        : %d\n", successCount);
  Serial.printf("| Lost            : %d\n", lostCount);
  Serial.printf("| PDR             : %.2f %%\n", pdr);

  Serial.println("--------------------------------------------------------------");

  Serial.printf("| Mean RTT        : %.2f ms\n", calculateMean());
  Serial.printf("| Median RTT      : %.2f ms\n", calculateMedian());
  Serial.printf("| Std. Deviation  : %.2f ms\n", calculateStdDev());
  Serial.printf("| P95 RTT         : %.2f ms\n", calculateP95());

  Serial.println("==============================================================");
  Serial.println("                     S3 SELESAI");
  Serial.println("==============================================================");
}

void loop() {
  delay(1000);
}
