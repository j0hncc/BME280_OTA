// ota from Arduino BasicOTA
// updates by jcc 5/26/23 - define topic highlevel ("misc","misc2", etc), and add humidity
// Also fix "missing Spi.h" per https://github.com/finitespace/BME280/issues/128 
//      and "missing return from non-void function" in BME280.cpp library
//
// 5/29/23_005 Add "startup" mq message denoting type and version
// 6/08/23_006 Make setsec trigger measurement
#include <stdio.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>

// bme280
// From https://github.com/finitespace/BME280
// see also https://github.com/jainrk/i2c_port_address_scanner  for diagnostics
// In platformio library I think this is "BME280 by Tyler Glenn"
#include <EnvironmentCalculations.h>
#include <Wire.h>
#include <BME280I2C.h>

#define TOPICTOP "misc2"
#define VERSION "v2023_005"

#include "secrets.h"
/*  in secrets.h:
const char* ssid = "bogus";
const char* password = "bogus";
const char* mqtt_server = "bogus";
*/

// forward decl
void readAndPublish1();
int i=0;

BME280I2C::Settings settings(
   BME280::OSR_X16,  // Temperature default x1
   BME280::OSR_X16,  // Pressure default x1
   BME280::OSR_X16,  // Humidity default x1
   BME280::Mode_Forced,
   BME280::StandbyTime_1000ms,
   BME280::Filter_Off,
   BME280::SpiEnable_False,
   BME280I2C::I2CAddr_0x76
);
BME280I2C::Settings settings2(
   BME280::OSR_X16,  // Temperature default x1
   BME280::OSR_X16,  // Pressure default x1
   BME280::OSR_X16,  // Humidity default x1
   BME280::Mode_Forced,
   BME280::StandbyTime_1000ms,
   BME280::Filter_Off,
   BME280::SpiEnable_False,
   BME280I2C::I2CAddr_0x77
);

BME280I2C bme( settings);    // Default : forced mode, standby time = 1000 ms
                  // Oversampling = pressure ×1, temperature ×1, humidity ×1, filter off,
BME280I2C bme2( settings2);
int bmeDetected=0, bmeDetected2=0;

WiFiClient espClient;
PubSubClient mqclient(espClient);
long pubWait=60000, lastPub= - pubWait + 5000;  // pubWait = milliseconds between publish

// MQTT functions
void mqcallback(char* topic, byte* payload, unsigned int length) {
  // handle message arrived
  char msg[40] ; strncpy( msg, (const char *) payload, length); msg[length]=0;
  Serial.printf("Topic :%s: , Msg :%s:\n", topic, msg);
  if ( !strcmp ( TOPICTOP"/temp/cmd", topic) )
  {
      if (!strcmp( "stop", msg))
      {
        Serial.println( "Stopping");
        pubWait = -1;         
      }
      if (!strcmp( "start", msg))
      {
        Serial.println( "Starting");
        lastPub=0;         // force measurement
      }
      return;
  }
  if ( !strcmp ( TOPICTOP"/temp/setsec", topic) )
  {
      long sec = String(msg).toInt();
      if ( 0 < sec  && sec < 6000)
      {
         sec *=1000;
         pubWait = sec;
         lastPub=0;
      }
      else
        mqclient.publish( TOPICTOP"/temp/status", "invalid setsec");
  }
}

boolean mqreconnect() {
  if (mqclient.connect(TOPICTOP "TempClient", TOPICTOP"/temp/lwt", 0, 1, "offline") ) {
    mqclient.publish(TOPICTOP"/temp/lwt","online", 1);
    mqclient.subscribe(TOPICTOP"/temp/cmd");
    mqclient.subscribe(TOPICTOP"/temp/setsec");
  }
  return mqclient.connected();
}


/**************************************************************** SETUP ************************/
void setup() {
  Serial.begin(115200);
  Serial.println("Booting");

  ////////////////////////////////////  Wifi
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    if (WiFi.waitForConnectResult() != WL_CONNECTED) {
      Serial.println("Connection Failed! Rebooting...");
      delay(5000);
      ESP.restart();
    }
  /////////////////////////////////////  OTA
    // Port defaults to 8266
    // ArduinoOTA.setPort(8266);
  
    // Hostname defaults to esp8266-[ChipID]
    // ArduinoOTA.setHostname("myesp8266");
  
    // No authentication by default
    // ArduinoOTA.setPassword("admin");
  
    // Password can be set with it's md5 value as well
    // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
    // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");
  
    ArduinoOTA.onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";
  
      // NOTE: if updating SPIFFS this would be the pBME280
              Serial.println("Start updating " + type);
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
    Serial.println( "Ready " TOPICTOP );
    Serial.print( "IP address: ");
    Serial.println( WiFi.localIP() );

  ////////////////////////////////////////////////////// BME280
  // 
     Wire.begin( 4, 5);    // (sda,scl) "wemos" d1 mini using sda=gpio4 (D2) , scl=gpio5 (D1)
     if (bme.begin())
     { 
        // bme.chipID(); // Deprecated. See chipModel().
        switch(bme.chipModel())
        {
           case BME280::ChipModel_BME280:
             Serial.println("Found BME280 sensor! Success.");
             bmeDetected = 1;
             break;
           case BME280::ChipModel_BMP280:
             Serial.println("Found BMP280 sensor! No Humidity available.");
             bmeDetected = 1;
             break;
           default:
             Serial.println("Found UNKNOWN sensor! Error!");
        }
     }
     else
      {
        Serial.println("Could not find BME280 sensor!");
        delay(1000);
      }
     if (bme2.begin())
     { 
        // bme.chipID(); // Deprecated. See chipModel().
        switch(bme2.chipModel())
        {
           case BME280::ChipModel_BME280:
             Serial.println("Found BME280-2 sensor! Success.");
             bmeDetected2 = 1;
             break;
           case BME280::ChipModel_BMP280:
             Serial.println("Found BMP280-2 sensor! No Humidity available."); // what?  j;lkj;lkj
             bmeDetected2 = 1;
             break;
           default:
             Serial.println("Found UNKNOWN sensor! Error!");
        }
     }
     else
      {
        Serial.println("Could not find BME280-2 sensor!");
        delay(1000);
      }
    
  // Led blinking
  pinMode( LED_BUILTIN, OUTPUT); // flash once on startup
  delay(1000);  
  digitalWrite( LED_BUILTIN, HIGH);  // turn it off
  
  // MQTT
  mqclient.setServer(mqtt_server, 1883);
  mqclient.setCallback(mqcallback);

  if (!mqclient.connected()) mqreconnect();
  mqclient.publish(TOPICTOP "/type/", ( bme.chipModel() == BME280::ChipModel_BME280 ? "BME280" : "BMP280"));
  mqclient.publish(TOPICTOP "/version/", VERSION );

}  // setup


/****************************************************************************** Loop ************/


void loop() {
   // ota
   ArduinoOTA.handle();
   // mqtt
   if (!mqclient.connected()) mqreconnect();
   mqclient.loop();
  
   // bme
   // publish every so often
   long now=millis();
   if ( pubWait > 0 && (now - lastPub > pubWait)) 
   {
      lastPub = now;
      readAndPublish1();   
   }      

}  // loop

// Two Sensors installed
void readAndPublish2()
{
   BME280::TempUnit tempUnit(BME280::TempUnit_Fahrenheit);
   BME280::PresUnit presUnit(BME280::PresUnit_Pa);  // PresUnit_inHg, PresUnit_Pa
   digitalWrite( LED_BUILTIN, LOW);
   float temp(NAN), hum(NAN), pres(NAN);
   float temp2(NAN), hum2(NAN), pres2(NAN);
   bme.read(pres, temp, hum, tempUnit, presUnit);
   bme2.read(pres2, temp2, hum2, tempUnit, presUnit);

   char topic [40];
   char msg [80] ;
   snprintf( topic, 40, TOPICTOP"/temp/summary");
   snprintf( msg, 80, "%.2f, %.2f, %.2f, %.2f, %.2f", temp,temp2,pres,pres2, pres2 - pres );
   mqclient.publish( topic,msg);
   digitalWrite( LED_BUILTIN, LOW);

}

// Only One sensore installed
void readAndPublish1()
{
   digitalWrite( LED_BUILTIN, LOW);
   BME280::TempUnit tempUnit(BME280::TempUnit_Fahrenheit);
   BME280::PresUnit presUnit(BME280::PresUnit_inHg);  // PresUnit_inHg, PresUnit_Pa
   float temp(NAN), hum(NAN), pres(NAN);
   bme.read(pres, temp, hum, tempUnit, presUnit);

  char topic [40];
   char msg [80] ;
   // snprintf( topic, 40, TOPICTOP"/temp/degF");
   snprintf( msg, 80, "%.2f", temp);
   mqclient.publish( TOPICTOP"/temp/degF",msg);
   snprintf( topic, 40, TOPICTOP"/press/inHg");
   snprintf( msg, 80, "%.4f", pres);
   mqclient.publish( topic,msg);
   if (bme.chipModel() == BME280::ChipModel_BME280)  // support humidity?
   {
      snprintf( topic, 40, TOPICTOP"/humid/rh");
      snprintf( msg, 80, "%.1f", hum);
      mqclient.publish( topic,msg);
   }
   
   digitalWrite( LED_BUILTIN, HIGH);
}


void printBME280Data( char * label, BME280I2C* bme, Stream* client, int newline)
{

   digitalWrite( LED_BUILTIN, LOW);
   float temp(NAN), hum(NAN), pres(NAN);

   BME280::TempUnit tempUnit(BME280::TempUnit_Fahrenheit);
   BME280::PresUnit presUnit(BME280::PresUnit_Pa);  // PresUnit_inHg, PresUnit_Pa
   bme->read(pres, temp, hum, tempUnit, presUnit);

   client->printf( "%s:   ", label);
   client->print("Temp: ");
   client->print(temp);
   client->print(" "+ String(tempUnit == BME280::TempUnit_Celsius ? 'C' :'F'));
   client->print("\t\tPressure: ");
   client->print(pres);
   client->print(" Inch Hg");
   if (newline) client->println();
   else client->print("   ");

  char msg [40] ;
  char topic [40];
  // temperature
  snprintf( topic, 40, TOPICTOP"/temp/%s/degF", label);
  snprintf( msg, 40, "%.1f", temp );
  mqclient.publish( topic,msg);

  // pressure 
  snprintf( topic, 40, TOPICTOP"/temp/%s/Pa", label);
  snprintf( msg, 40, "%.2f", pres );
  mqclient.publish( topic,msg);

  
  digitalWrite( LED_BUILTIN, HIGH);

}

