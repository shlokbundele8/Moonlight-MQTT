// Moonlight MQTT - ESP8266 over WiFi (plain, no TLS).
//
// Simple starter for ESP8266. For a secure link use WiFiClientSecure with
// BearSSL and setTrustAnchors, the same way the ESP32 example uses setCACert.

#include <ESP8266WiFi.h>
#include <MoonlightMQTT.h>

using namespace moonlight;

const char* WIFI_SSID = "your-wifi";
const char* WIFI_PASS = "your-password";
const char* BROKER    = "your-broker.local";
const uint16_t PORT   = 1883;

WiFiClient net;
ArduinoTransport<WiFiClient> transport(net);
StaticClient<1024, 512> mqtt(transport);

bool reopen(void*) { return net.connect(BROKER, PORT) == 1; }

void onMessage(const Message& m, void*) {
  Serial.print("message on ");
  Serial.write((const uint8_t*)m.topic, m.topicLen);
  Serial.print(": ");
  Serial.write(m.payload, m.payloadLen);
  Serial.println();
}

void setup() {
  Serial.begin(115200);

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) { delay(300); Serial.print("."); }
  Serial.println("\nWiFi connected");

  if (!net.connect(BROKER, PORT)) { Serial.println("TCP connect failed"); return; }

  mqtt.onMessage(onMessage);
  mqtt.enableAutoReconnect(reopen, nullptr);

  ConnectOptions opts;
  opts.clientId   = "moonlight-esp8266";
  opts.keepAlive  = 30;
  opts.cleanStart = true;

  if (mqtt.connect(opts) != OK) { Serial.println("MQTT connect failed"); return; }
  Serial.println("MQTT connected");

  mqtt.subscribe("moonlight/esp8266/cmd", QOS1);
}

unsigned long lastPublish = 0;

void loop() {
  mqtt.loop();
  if (mqtt.connected() && millis() - lastPublish > 5000) {
    lastPublish = millis();
    mqtt.publish("moonlight/esp8266/hello", "hi from esp8266", QOS0);
  }
}
