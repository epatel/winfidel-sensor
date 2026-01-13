#if CONFIG_ENABLE_MQTT

#include <PubSubClient.h>
#include <WiFi.h>
#include "include/mqtt_settings.h"

// MQTT client instance
WiFiClient mqttWifiClient;
PubSubClient mqttClient(mqttWifiClient);

// Persistent settings instance
PersistSettings<MqttSettingsConfig> mqttSettings(MQTT_SETTINGS_VERSION);

// State tracking
static uint32_t mqtt_last_reconnect_attempt = 0;
static float mqtt_last_published_diameter = -1.0f;
static bool mqtt_connected_prev = false;

// Topic buffers
static char mqtt_topic_diameter[96];
static char mqtt_topic_status[96];
static char mqtt_topic_cmd_reset[96];
static char mqtt_topic_cmd_calibrate[96];

// Forward declarations
void mqtt_callback(char* topic, byte* payload, unsigned int length);
void mqtt_build_topics(void);
void mqtt_publish_status(bool online);

void MQTT_Setup(void)
{
    // Initialize persistent settings
    mqttSettings.Begin();

    if (!mqttSettings.Config.enabled)
    {
        Serial.println("MQTT: Disabled in settings");
        return;
    }

    if (strlen(mqttSettings.Config.broker_host) == 0)
    {
        Serial.println("MQTT: No broker configured");
        return;
    }

    // Build topic strings
    mqtt_build_topics();

    // Configure MQTT client
    mqttClient.setServer(mqttSettings.Config.broker_host, mqttSettings.Config.broker_port);
    mqttClient.setCallback(mqtt_callback);
    mqttClient.setBufferSize(MQTT_BUFFER_SIZE);

    Serial.print("MQTT: Configured for ");
    Serial.print(mqttSettings.Config.broker_host);
    Serial.print(":");
    Serial.println(mqttSettings.Config.broker_port);
}

void mqtt_build_topics(void)
{
    const char* device_id = mqttSettings.Config.device_id;
    if (strlen(device_id) == 0)
    {
        device_id = "winfidel";
    }

    snprintf(mqtt_topic_diameter, sizeof(mqtt_topic_diameter), "%s/%s/diameter", MQTT_TOPIC_PREFIX, device_id);
    snprintf(mqtt_topic_status, sizeof(mqtt_topic_status), "%s/%s/status", MQTT_TOPIC_PREFIX, device_id);
    snprintf(mqtt_topic_cmd_reset, sizeof(mqtt_topic_cmd_reset), "%s/%s/cmd/reset", MQTT_TOPIC_PREFIX, device_id);
    snprintf(mqtt_topic_cmd_calibrate, sizeof(mqtt_topic_cmd_calibrate), "%s/%s/cmd/calibrate", MQTT_TOPIC_PREFIX, device_id);
}

bool mqtt_connect(void)
{
    if (!mqttSettings.Config.enabled)
    {
        return false;
    }

    if (strlen(mqttSettings.Config.broker_host) == 0)
    {
        return false;
    }

    Serial.print("MQTT: Connecting to ");
    Serial.print(mqttSettings.Config.broker_host);
    Serial.print("...");

    // Build client ID from device_id
    char clientId[48];
    snprintf(clientId, sizeof(clientId), "winfidel-%s", mqttSettings.Config.device_id);

    // Set Last Will Testament (LWT) for offline status
    bool connected = false;
    if (strlen(mqttSettings.Config.username) > 0)
    {
        connected = mqttClient.connect(
            clientId,
            mqttSettings.Config.username,
            mqttSettings.Config.password,
            mqtt_topic_status,  // LWT topic
            0,                  // LWT QoS
            true,               // LWT retain
            "{\"online\":false}" // LWT message
        );
    }
    else
    {
        connected = mqttClient.connect(
            clientId,
            mqtt_topic_status,  // LWT topic
            0,                  // LWT QoS
            true,               // LWT retain
            "{\"online\":false}" // LWT message
        );
    }

    if (connected)
    {
        Serial.println("connected!");

        // Subscribe to command topics
        mqttClient.subscribe(mqtt_topic_cmd_reset);
        mqttClient.subscribe(mqtt_topic_cmd_calibrate);

        // Publish online status
        mqtt_publish_status(true);

        // Force publish current reading
        mqtt_last_published_diameter = -1.0f;

        return true;
    }
    else
    {
        Serial.print("failed, rc=");
        Serial.println(mqttClient.state());
        return false;
    }
}

void MQTT_Loop(void)
{
    if (!mqttSettings.Config.enabled)
    {
        return;
    }

    if (strlen(mqttSettings.Config.broker_host) == 0)
    {
        return;
    }

    if (!mqttClient.connected())
    {
        // Track connection state change
        if (mqtt_connected_prev)
        {
            Serial.println("MQTT: Disconnected");
            mqtt_connected_prev = false;
        }

        // Attempt reconnection with backoff
        uint32_t now = millis();
        if (now - mqtt_last_reconnect_attempt >= MQTT_RECONNECT_INTERVAL_MS)
        {
            mqtt_last_reconnect_attempt = now;
            if (mqtt_connect())
            {
                mqtt_connected_prev = true;
            }
        }
    }
    else
    {
        mqttClient.loop();
    }
}

void mqtt_callback(char* topic, byte* payload, unsigned int length)
{
    // Null-terminate the payload
    char message[128];
    size_t copyLen = (length < sizeof(message) - 1) ? length : sizeof(message) - 1;
    memcpy(message, payload, copyLen);
    message[copyLen] = '\0';

    Serial.print("MQTT: Received [");
    Serial.print(topic);
    Serial.print("] ");
    Serial.println(message);

    // Handle reset command
    if (strcmp(topic, mqtt_topic_cmd_reset) == 0)
    {
        reset_stats();
        Serial.println("MQTT: Stats reset via command");
        // Force republish after reset
        mqtt_last_published_diameter = -1.0f;
    }
    // Handle calibrate command (expects JSON: {"mm": 1.75} or {"mm": 1.75, "adc": 2048})
    else if (strcmp(topic, mqtt_topic_cmd_calibrate) == 0)
    {
        // Simple parsing - look for "mm": value
        char* mmPtr = strstr(message, "\"mm\"");
        if (mmPtr)
        {
            char* colonPtr = strchr(mmPtr, ':');
            if (colonPtr)
            {
                float mm = atof(colonPtr + 1);
                if (mm > 0.0f && mm < 10.0f)
                {
                    // Check if ADC value is provided
                    char* adcPtr = strstr(message, "\"adc\"");
                    if (adcPtr)
                    {
                        char* adcColonPtr = strchr(adcPtr, ':');
                        if (adcColonPtr)
                        {
                            uint32_t adc = atoi(adcColonPtr + 1);
                            manually_create_calibration_point(adc, mm);
                            Serial.print("MQTT: Calibration point created at ");
                            Serial.print(mm);
                            Serial.print("mm = ");
                            Serial.println(adc);
                        }
                    }
                    else
                    {
                        // Use current ADC value
                        manually_create_calibration_point(get_adc(), mm);
                        Serial.print("MQTT: Calibration point created at ");
                        Serial.print(mm);
                        Serial.println("mm");
                    }
                }
            }
        }
    }
}

void mqtt_publish_status(bool online)
{
    char payload[64];
    snprintf(payload, sizeof(payload), "{\"online\":%s}", online ? "true" : "false");
    mqttClient.publish(mqtt_topic_status, payload, true);  // retained
}

bool MQTT_Publish_Measurement(void)
{
    if (!mqttSettings.Config.enabled || !mqttClient.connected())
    {
        return false;
    }

    float currentDiameter = get_last();

    // Check if change exceeds threshold
    float threshold = mqttSettings.Config.publish_threshold;
    if (threshold <= 0.0f)
    {
        threshold = MQTT_PUBLISH_THRESHOLD;
    }

    if (mqtt_last_published_diameter >= 0.0f)
    {
        float change = currentDiameter - mqtt_last_published_diameter;
        if (change < 0) change = -change;  // abs
        if (change < threshold)
        {
            return false;  // No significant change
        }
    }

    // Build JSON payload
    char payload[MQTT_BUFFER_SIZE];
    snprintf(payload, sizeof(payload),
        "{\"diameter\":%.3f,\"adc\":%lu,\"min\":%.3f,\"max\":%.3f,\"avg\":%.3f,\"count\":%lu}",
        currentDiameter,
        (unsigned long)get_adc(),
        get_min(),
        get_max(),
        get_avg(),
        (unsigned long)get_measurements_count()
    );

    // Publish with retain flag
    if (mqttClient.publish(mqtt_topic_diameter, payload, true))
    {
        mqtt_last_published_diameter = currentDiameter;
        return true;
    }
    return false;
}

bool MQTT_IsConnected(void)
{
    return mqttSettings.Config.enabled && mqttClient.connected();
}

bool MQTT_IsEnabled(void)
{
    return mqttSettings.Config.enabled;
}

// Function to update MQTT settings and reconnect
void MQTT_UpdateSettings(void)
{
    // Disconnect if currently connected
    if (mqttClient.connected())
    {
        mqtt_publish_status(false);
        mqttClient.disconnect();
    }

    // Rebuild topics with new device_id
    mqtt_build_topics();

    // Reconfigure server
    if (mqttSettings.Config.enabled && strlen(mqttSettings.Config.broker_host) > 0)
    {
        mqttClient.setServer(mqttSettings.Config.broker_host, mqttSettings.Config.broker_port);
    }

    // Reset state
    mqtt_last_reconnect_attempt = 0;
    mqtt_last_published_diameter = -1.0f;
    mqtt_connected_prev = false;
}

#else // CONFIG_ENABLE_MQTT

// Stub functions when MQTT is disabled
void MQTT_Setup(void) {}
void MQTT_Loop(void) {}
bool MQTT_Publish_Measurement(void) { return false; }
bool MQTT_IsConnected(void) { return false; }
bool MQTT_IsEnabled(void) { return false; }
void MQTT_UpdateSettings(void) {}

#endif // CONFIG_ENABLE_MQTT
