#ifndef __MQTT_SETTINGS_H__
#define __MQTT_SETTINGS_H__

#include "../config_winfidel.h"
#include <stdint.h>
#include <string.h>

// Forward declaration - PersistSettings template is defined in PersistSettings.h
// which is already included by main.ino
template <class T> class PersistSettings;

// MQTT Settings version - increment when struct changes
#define MQTT_SETTINGS_VERSION 1

// MQTT Settings structure
struct MqttSettingsConfig {
    bool enabled;
    char broker_host[64];
    uint16_t broker_port;
    char username[32];
    char password[32];
    float publish_threshold;
    char device_id[32];

    // Default constructor with sensible defaults
    MqttSettingsConfig() :
        enabled(false),
        broker_port(MQTT_DEFAULT_PORT),
        publish_threshold(MQTT_PUBLISH_THRESHOLD)
    {
        memset(broker_host, 0, sizeof(broker_host));
        memset(username, 0, sizeof(username));
        memset(password, 0, sizeof(password));
        memset(device_id, 0, sizeof(device_id));
        strncpy(device_id, "winfidel", sizeof(device_id) - 1);
    }
};

// Global MQTT settings instance
extern PersistSettings<MqttSettingsConfig> mqttSettings;

#endif // __MQTT_SETTINGS_H__
