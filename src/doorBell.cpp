//-----------------------------------------------------------------------------
// Door Bell
// By James Battersby
//-----------------------------------------------------------------------------

// For encryption
#include <xxtea-lib.h>

// For OTA
#include <ArduinoOTA.h>

// Config for WiFi
#include "wifiConfig.h"

// For MQTT messaging
#include <PubSubClient.h>

// mapping suggestion from Waveshare SPI e-Paper to Wemos D1 mini
// BUSY -> D2, RST -> D4, DC -> D3, CS -> D8, CLK -> D5, DIN -> D7, GND -> GND, 3.3V -> 3.3V
#include <GxEPD.h>
#include <GxGDEP015OC1/GxGDEP015OC1.h>    // 1.54" b/w
#include <Fonts/FreeMonoBold12pt7b.h>
#include <GxIO/GxIO_SPI/GxIO_SPI.h>
#include <GxIO/GxIO.h>

// Local functions
void setUpWifi();
void mqttCallback(char*, byte*, unsigned int);
void connectToMqtt();
void notifyPress();
void updateScreen();

#define DOOR_PUSH D1 // Digital IO pin for door push
#define RINGER D0    // Digital IO pin for door ringer relay

WiFiClient espClient;
PubSubClient mqttClient(espClient);

GxIO_Class io(SPI, /*CS=D8*/ SS, /*DC=D3*/ 0, /*RST=D4*/ 2); // arbitrary selection of D3(=0), D4(=2), selected for default of GxEPD_Class
GxEPD_Class display(io /*RST=D4*/ /*BUSY=D2*/); // default selection of D4(=2), D2(=4)

int count = 0;

//-----------------------------------------------------------------------------
// setUp
//
// Configure the serial port, ultrasonic sensor pins and the FastLED library.
//-----------------------------------------------------------------------------
void setup()
{
  Serial.begin (115200);
  setUpWifi();
  pinMode(DOOR_PUSH, INPUT_PULLUP);
  pinMode(RINGER, OUTPUT);
  display.init(115200);
  updateScreen();
  delay(500);
}

//-----------------------------------------------------------------------------
// loop
//
// This is the main loop, which will check the distance every 250ms and up-date
// the LEDs.  It will also check for OTA download and MQTT messages if WIFI is
// enabled.
//-----------------------------------------------------------------------------
void loop()
{
  static int lastState = 1;
  ArduinoOTA.handle();
  if (!mqttClient.connected())
  {
    connectToMqtt();
  }
  int state = digitalRead(DOOR_PUSH);
  if (!state && state != lastState)
  {
    digitalWrite(RINGER, 1);
    delay(500);
    digitalWrite(RINGER, 0);
    notifyPress();
  }
  lastState = state;
  mqttClient.loop();
  delay(10);
}

//-----------------------------------------------------------------------------
// notify
//
// Send notification of the door bell being pushed.
//-----------------------------------------------------------------------------
void notifyPress()
{
  count++;
  printf("Bing bong..\n");
  mqttClient.publish("DoorBell", "binBong");
  updateScreen();
}

//-----------------------------------------------------------------------------
// setUpWifi
//
// Responsible for connecting to Wifi, initialising the over-air-download
// handlers and connecting to the MQTT server.
//
// If GENERATE_ENCRYPTED_WIFI_CONFIG is set to true, will also generate
// the encrypted wifi configuration data.
//-----------------------------------------------------------------------------
void setUpWifi()
{
  String ssid = SSID;
  String password = WIFI_PASSWORD;

  // Set the key
  xxtea.setKey(ENCRYPTION_KEY);

  // Perform Encryption on the Data
#if GENERATE_ENCRYPTED_WIFI_CONFIG
  Serial.printf("--Encrypted Wifi SSID: %s\n", xxtea.encrypt(SSID).c_str());
  Serial.printf("--Encrypted Wifi password: %s\n", xxtea.encrypt(WIFI_PASSWORD).c_str());
  Serial.printf("--Encrypted MQTT username: %s\n", xxtea.encrypt(MQTT_USERNAME).c_str());
  Serial.printf("--Encrypted MQTT password: %s\n", xxtea.encrypt(MQTT_PASSWORD).c_str());
#endif // GENERATE_ENCRYPTED_WIFI_CONFIG

  unsigned char pw[MAX_PW_LEN];
  unsigned char ss[MAX_PW_LEN];
  // Connect to Wifi
  WiFi.mode(WIFI_STA);
  xxtea.decrypt(password).getBytes(pw, MAX_PW_LEN);
  xxtea.decrypt(ssid).getBytes(ss, MAX_PW_LEN);

  WiFi.begin((const char*)(ss), (const char*)(pw));
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

//-----------------------------------------------------------------------------
// connectToMqtt
//
// Connect the the MQTT server
//-----------------------------------------------------------------------------
void connectToMqtt()
{
  unsigned char mqttUser[MAX_PW_LEN];
  unsigned char mqttPassword[MAX_PW_LEN];
  String username = MQTT_USERNAME;
  String passwordmqtt = MQTT_PASSWORD;
  xxtea.decrypt(username).getBytes(mqttUser, MAX_PW_LEN);
  xxtea.decrypt(passwordmqtt).getBytes(mqttPassword, MAX_PW_LEN);
  int retry = 20;

  while (!mqttClient.connected() && --retry)
  {
    Serial.println("Connecting to MQTT...");

    if (mqttClient.connect("DoorBell", reinterpret_cast<const char *>(mqttUser), reinterpret_cast<const char *>(mqttPassword)))
    {
      Serial.println("connected");
    }
    else
    {
      Serial.print("failed with state ");
      Serial.println(mqttClient.state());
      delay(2000);
    }
  }

  if (retry == 0)
  {
    printf("Failed to connect to MQTT server on %s:%d", MQTT_SERVER, MQTT_PORT);
  }
}

//-----------------------------------------------------------------------------
// mqttCallback
//
// Process a received message
//-----------------------------------------------------------------------------
void mqttCallback(char* topic, byte* payload, unsigned int length)
{
}

//-----------------------------------------------------------------------------
// updateScreen
//
// Update the e-ink screen
//-----------------------------------------------------------------------------
void updateScreen()
{
  printf("Count:%d\n", count);
  const GFXfont* f = &FreeMonoBold12pt7b;
  display.setRotation(3);
  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);
  display.setFont(f);
  display.setCursor(0, 0);
  display.println();
  display.printf("Count:%d", count);
  display.update();
}