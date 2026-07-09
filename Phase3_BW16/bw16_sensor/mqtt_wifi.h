#ifndef MQTT_WIFI_H
#define MQTT_WIFI_H

#include "config.h"

String parseJsonField(const String& json, const String& key);
void callback(char* topic, byte* payload, unsigned int length);
void connectWiFi();
void connectMQTT();

#endif
