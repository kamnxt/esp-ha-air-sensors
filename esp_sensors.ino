#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include "Adafruit_SGP30.h"
#include "Adafruit_SHT31.h"
#include "params.h"


Adafruit_SGP30 sgp;
Adafruit_SHT31 sht31 = Adafruit_SHT31();

WiFiClient espClient;
PubSubClient client(espClient);

#ifndef UNIQUE
#error Please define UNIQUE to a unique id string in params.h
// example: #define UNIQUE "livingroomsensors"
#endif
#if !(defined(wifi_ssid) && defined(wifi_password))
#error Please define wifi_ssid and wifi_password in params.h
// example: #define wifi_ssid "YourWifi"
// example: #define wifi_password "YourPass"
#endif
#ifndef mqtt_server
#error Please define mqtt_server in params.h
// example: #define mqtt_server "192.168.1.123"
#endif


char buf[256]; // used for formatting messages to send
int counter = 0; // used for baseline from example code

// from sgp30 example
uint32_t getAbsoluteHumidity(float temperature, float humidity) {
  // approximation formula from Sensirion SGP30 Driver Integration chapter 3.15
  const float absoluteHumidity = 216.7f * ((humidity / 100.0f) * 6.112f * exp((17.62f * temperature) / (243.12f + temperature)) / (273.15f + temperature)); // [g/m^3]
  const uint32_t absoluteHumidityScaled = static_cast<uint32_t>(1000.0f * absoluteHumidity); // [mg/m^3]
  return absoluteHumidityScaled;
}


void measure() {
  float temperature = sht31.readTemperature();
  float humidity = sht31.readHumidity();

  if (! isnan(temperature)) {  // check if 'is not a number'
    Serial.print("Temp *C = "); Serial.print(temperature); Serial.print("\t\t");
  } else {
    Serial.println("Failed to read temperature");
  }

  if (! isnan(humidity)) {  // check if 'is not a number'
    Serial.print("Hum. % = "); Serial.println(humidity);
  } else {
    Serial.println("Failed to read humidity");
  }

  // If you have a temperature / humidity sensor, you can set the absolute humidity to enable the humidity compensation for the air quality signals
  if (!isnan(temperature) && !isnan(humidity)) {
    sgp.setHumidity(getAbsoluteHumidity(temperature, humidity));
  }
  if (! sgp.IAQmeasure()) {
    Serial.println("Measurement failed");
    return;
  }
  Serial.print("TVOC "); Serial.print(sgp.TVOC); Serial.print(" ppb\t");
  Serial.print("eCO2 "); Serial.print(sgp.eCO2); Serial.println(" ppm");

  if (! sgp.IAQmeasureRaw()) {
    Serial.println("Raw Measurement failed");
    return;
  }
  Serial.print("Raw H2 "); Serial.print(sgp.rawH2); Serial.print(" \t");
  Serial.print("Raw Ethanol "); Serial.print(sgp.rawEthanol); Serial.println("");

  // make some json and send it
  sprintf(buf, "{\"h\": %.2f, \"t\": %.2f, \"v\": %d, \"c\": %d}", humidity, temperature, sgp.TVOC, sgp.eCO2);
  client.publish("/"UNIQUE"/state", buf);
  delay(1000);

  counter++;
  if (counter == 30) {
    counter = 0;

    uint16_t TVOC_base, eCO2_base;
    if (! sgp.getIAQBaseline(&eCO2_base, &TVOC_base)) {
      Serial.println("Failed to get baseline readings");
      return;
    }
    Serial.print("****Baseline values: eCO2: 0x"); Serial.print(eCO2_base, HEX);
    Serial.print(" & TVOC: 0x"); Serial.println(TVOC_base, HEX);
  }

}

void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(wifi_ssid);

  WiFi.begin(wifi_ssid, wifi_password);
  bool val = false;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    digitalWrite(LED_BUILTIN, val);
    val = !val;
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    // If you do not want to use a username and password, change next line to
    if (client.connect("ESP8266Client")) {
      //if (client.connect("ESP8266Client", mqtt_user, mqtt_password)) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);

  Serial.println("SHT31 test");
  if (! sht31.begin(0x44)) {   // Set to 0x45 for alternate i2c addr
    Serial.println("Couldn't find SHT31");
    while (1) delay(1);
  }

  if (! sgp.begin()) {
    Serial.println("Sensor not found :(");
    while (1);
  }
  Serial.print("Found SGP30 serial #");
  Serial.print(sgp.serialnumber[0], HEX);
  Serial.print(sgp.serialnumber[1], HEX);
  Serial.println(sgp.serialnumber[2], HEX);

  setup_wifi();
  client.setServer(mqtt_server, 1883);

}


bool firstTime = true;
long lastTime = 0;
void loop() {
  if (!client.connected()) {
    digitalWrite(LED_BUILTIN, LOW);
    reconnect();
  }
  else {
    digitalWrite(LED_BUILTIN, HIGH);
  }
  if (client.connected() && firstTime) {
    firstTime = false;
    // publish with retain flag set
    client.publish("homeassistant/sensor/"UNIQUE"_t/config",
                   "{\"name\":\""UNIQUE" temperature\", \"stat_t\": \"/"UNIQUE"/state\", \"uniq_id\": \""UNIQUE"_t\", \"val_tpl\": \"{{ value_json.t}}\", \"unit_of_meas\": \"°C\"}",
                   1);
    client.publish("homeassistant/sensor/"UNIQUE"_h/config",
                   "{\"name\":\""UNIQUE" humidity\", \"stat_t\": \"/"UNIQUE"/state\", \"uniq_id\": \""UNIQUE"_h\", \"val_tpl\": \"{{ value_json.h}}\", \"unit_of_meas\": \"%RH\"}",
                   1);
    client.publish("homeassistant/sensor/"UNIQUE"_v/config",
                   "{\"name\":\""UNIQUE" TVOC\", \"stat_t\": \"/"UNIQUE"/state\", \"uniq_id\": \""UNIQUE"_v\", \"val_tpl\": \"{{ value_json.v}}\", \"unit_of_meas\": \"ppb\"}",
                   1);
    client.publish("homeassistant/sensor/"UNIQUE"_c/config",
                   "{\"name\":\""UNIQUE" eCO2\", \"stat_t\": \"/"UNIQUE"/state\", \"uniq_id\": \""UNIQUE"_c\", \"val_tpl\": \"{{ value_json.c}}\", \"unit_of_meas\": \"ppm\"}",
                   1);
  }

  client.loop();
  if (millis() - lastTime > 30000) {
    measure();
    lastTime = millis();
  }
  delay(200); // give the esp some rest to lower power consumption! (almost halves it! went from 120mA to 60mA with 120mA peaks measured at USB)

}
