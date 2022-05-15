#include <WiFiClient.h>
#include <PubSubClient.h>
#include <SoftwareSerial.h>
#include <ESP8266WiFi.h>

#include "credentials.h"

#define AVG_SIZE 3

SoftwareSerial VESerial;
WiFiClient client;
PubSubClient mqtt(client);

// error, charge, load
unsigned int STATES[] = {0, 0, 0};
unsigned int P_STATES[] = {0, 0, 0};

int VALUES[5][AVG_SIZE];
int TOTALS[5] = {0, 0, 0, 0, 0};
unsigned int V_IDX[5] = {0, 0, 0, 0, 0};

int getIndex(String label) {
  if (label == "V") return 0;
  if (label == "I") return 1;
  if (label == "IL") return 2;
  if (label == "VPV") return 3;
  if (label == "PPV") return 4;
  return -1;
}

int mean(int idx) {
  return TOTALS[idx] / AVG_SIZE;
}

bool isUpdated(int idx) {
  if (STATES[idx] == P_STATES[idx]) return false;
  P_STATES[idx] = STATES[idx];
  return true;
}

void handleLine(String line) {
  int sep = line.indexOf("\t");
  if (sep < 0) return;
  String label = line.substring(0, sep);

  if (label == "LOAD") {
    STATES[2] = line.endsWith("ON") ? 1 : 0;
    return;
  }

  if (label == "CHECKSUM") return;
  if (label.startsWith("H")) return;
  if (label == "SER#" || label == "PID" || label == "FW") return;

  int value = line.substring(sep + 1).toInt();

  if (label == "ERR") {
    STATES[0] = value;
  } else if (label == "CS") {
    STATES[1] = value;
  } else {
    int idx = getIndex(label);
    if (idx < 0) return;
    int cur = V_IDX[idx];
    TOTALS[idx] -= VALUES[idx][cur];
    VALUES[idx][cur] = value;
    TOTALS[idx] += value;
    V_IDX[idx] = (cur + 1) % AVG_SIZE;
  }
}

void send(String feed, int value) {
  boolean ok = mqtt.publish((String(AIO_USER) + "/feeds/mppt." + feed).c_str(), String(value).c_str());
  Serial.printf("%d ", ok ? 1 : 0);
  Serial.print(feed);
  Serial.printf("\t%d\n", value);
}

void setup(void){
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, 0);

  Serial.begin(57600);
  VESerial.begin(19200, SWSERIAL_8N1, 4, 2, false);
  Serial.println("");

  boolean on = false;
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    on = !on;
    digitalWrite(LED_BUILTIN, on ? 1 : 0);
    delay(333);
    Serial.print(".");
  }

  Serial.print("\nIP: ");
  Serial.println(WiFi.localIP());

  for (int i = 0; i < 5; ++i) {
    for (int j = 0; j < AVG_SIZE; ++j) {
      VALUES[i][j] = 0;
    }
  }

  mqtt.setServer(AIO_HOST, AIO_PORT);
  digitalWrite(LED_BUILTIN, 1);
}

void loop(void){
  static char rc;
  static String cur;
  static int idx = 0;
  static unsigned long now, last = millis();

  while (VESerial.available() > 0) {
    rc = VESerial.read();
    if (rc == '\r') {
      handleLine(cur);
      cur = "";
    } else if (rc != '\n') {
      cur += rc;
    }
  }

  now = millis();
  if (now - last < 5000) return;

  if (!mqtt.connect("mppt", AIO_USER, AIO_KEY)) return;

  switch (idx) {
    case 0:
      send("v", mean(0));
      send("i", mean(1));
      break;
    case 1:
      digitalWrite(LED_BUILTIN, 0);
      send("vpv", mean(3));
      digitalWrite(LED_BUILTIN, 1);
      send("ppv", mean(4));
      break;
    default:
      if (isUpdated(0)) send("err", STATES[0]);
      if (isUpdated(1)) send("cs", STATES[1]);
      if (isUpdated(2)) {
        send("load", STATES[2]);
        if (STATES[2]) send("il", mean(2));
      }
      break;
  }

  idx = (idx + 1) % 3;
  last = now;
}