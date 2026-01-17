/*
Winfidel - Web
Description: This file contains all the functions we use for receiving and responding to HTTP requests
*/

#include "version.h"
#if CONFIG_ENABLE_MQTT
#include "include/mqtt_settings.h"
#endif
#if CONFIG_ENABLE_VORON
#include "include/voron_settings.h"
#endif

char sensor_data[SENSOR_STR_MAX_LEN] = {0};

// Helper function that allows us to replace template variable in .html file
// with a value from our code.
String template_const_processor(const String& var)
{
    if (var == "FW_VERSION") {
        return String(String(VERSION_MAJOR) + "." + String(VERSION_MINOR) + "." + String(VERSION_PATCH));
    }
    else if (var == "CALIBRATION_POINT_SAMPLE_COUNT")
    {
        return String(CALIBRATION_POINT_SAMPLE_COUNT);

    }
    else if (var == "CALIBRATION_POINT_ACCURACY_POINT")
    {
        return String(CALIBRATION_POINT_ACCURACY_POINT);

    }
    else if (var == "MAX_CALIBRATION_POINTS")
    {
        return String(MAX_CALIBRATION_POINTS);

    }
    else if (var == "ADC_MIN")
    {
        return String(ADC_MIN);

    }
    else if (var == "ADC_MIN_EQUALS_MM")
    {
        return String(ADC_MIN_EQUALS_MM);

    }
    else if (var == "ADC_MAX")
    {
        return String(ADC_MAX);

    }
    else if (var == "ADC_MAX_EQUALS_MM")
    {
        return String(ADC_MAX_EQUALS_MM);

    }
    else if (var == "BUILD_DATE")
    {
        return String(__DATE__);

    }
    else if (var == "BUILD_TIME")
    {
        return String(__TIME__);

    }
    else if (var == "ADC_ALGO")
    {
        return adc_get_algo();
    }
    else if (var == "ADC_CHIP")
    {
        return adc_get_chip();
    }

    Serial.print("Unknown template variable: ");
    Serial.println(var);
    return String();
}

void setupWebServer(void)
{
    server.onNotFound([](AsyncWebServerRequest *request) {
        Serial.println("404:");
        Serial.println(request->url());
        request->send(404);
    });

    server_init_handlers();

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        request->redirect("/index.html");
    });


    server.on("/api/v0/diameter/read", HTTP_GET, [] (AsyncWebServerRequest *request) {
        format_sensor_data();
        request->send(200, "application/json", sensor_data);
    });

    server.on("/api/v0/diameter/reset", HTTP_GET, [] (AsyncWebServerRequest *request) {
        reset_stats();
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    server.on("/api/v0/calibration/read", HTTP_GET, [] (AsyncWebServerRequest *request) {
        request->send(200, "application/json", get_calibration_json());
    });

    server.on("/api/v0/calibration/create", HTTP_GET, [] (AsyncWebServerRequest *request) {
        if ( request->hasParam("mm") )
        {
            winfidel_status_t status;
            bool adc_provided = false;
            float cal_mm = request->getParam("mm")->value().toFloat();
            uint32_t cal_adc = 0;

            // Check if ADC count was provided
            if (request->hasParam("adc"))
            {
                adc_provided = true;
                cal_adc = request->getParam("adc")->value().toInt();
            }

            if (adc_provided == false)
            {
                status = create_calibration_point(cal_mm);
            }
            else
            {
                status = manually_create_calibration_point(cal_adc, cal_mm);
            }

            if (status == WINFIDEL_OK)
            {
                request->send(200, "application/json", "{\"status\":\"ok\"}");
            }
            else if (status == WINFIDEL_NO_SPACE)
            {
                request->send(500, "application/json", "{\"status\":\"fail\", \"message\": \"Maximum number of calibration points reached!\"}");
            }
            else if (status == WINFIDEL_LOW_ACCURACY)
            {
                request->send(500, "application/json", "{\"status\":\"fail\", \"message\": \"ADC failed to produce samples with sufficient accuracy!\"}");
            }
        }
        else
        {
            request->send(400, "application/json", "{\"status\":\"fail\", \"message\": \"Missing argument `mm`!\"}");
        }
    });

    server.on("/api/v0/calibration/remove", HTTP_GET, [] (AsyncWebServerRequest *request) {
        if ( request->hasParam("mm") )
        {
            winfidel_status_t status;
            float rem_mm = request->getParam("mm")->value().toFloat();
            status = remove_calibration_point_mm(rem_mm);
            if (status == WINFIDEL_OK)
            {
                request->send(200, "application/json", "{\"status\":\"ok\"}");
            }
            else if (status == WINFIDEL_MISSING)
            {
                request->send(500, "application/json", "{\"status\":\"fail\", \"message\": \"Could not find calibration point!\"}");
            }
            else if (status == WINFIDEL_FORBIDDEN)
            {
                request->send(403, "application/json", "{\"status\":\"fail\", \"message\": \"Can not remove boundary points!\"}");
            }
        }
        else
        {
            request->send(400, "application/json", "{\"status\":\"fail\", \"message\": \"Missing argument `mm`!\"}");
        }
    });

    server.on("/api/v0/calibration/reset", HTTP_GET, [] (AsyncWebServerRequest *request) {
        if ( request->hasParam("confirm") )
        {
            calibration_reset();
            request->send(200, "application/json", "{\"status\":\"ok\"}");
        }
        else
        {
            request->send(400, "application/json", "{\"status\":\"fail\", \"message\": \"Missing argument `confirm`!\"}");
        }
    });


    server.on("/api/v0/wifi/reset", HTTP_GET, [] (AsyncWebServerRequest *request) {
        if ( request->hasParam("confirm") )
        {
            Serial.println("WiFi reset request recevied.");
            request->send(200, "application/json", "{\"status\":\"ok\", \"message\": \"WiFi configuration has been reset. Please connect to WInFiDEL WiFi and re-configure the device.\"}");
            wifiManager.resetSettings();
        }
        else
        {
            request->send(400, "application/json", "{\"status\":\"fail\", \"message\": \"Missing argument `confirm`!\"}");
        }
    });

    server.on("/api/v0/device/reboot", HTTP_GET, [] (AsyncWebServerRequest *request) {
        if ( request->hasParam("confirm") )
        {
            Serial.println("Device reboot request received. Rebooting...");
            request->send(200, "application/json", "{\"status\":\"ok\", \"message\": \"Rebooting...\"}");
            delay(500);
            ESP.restart();
        }
        else
        {
            request->send(400, "application/json", "{\"status\":\"fail\", \"message\": \"Missing argument `confirm`!\"}");
        }
    });

#if CONFIG_ENABLE_MQTT
    // MQTT Configuration - Read
    server.on("/api/v0/mqtt/config", HTTP_GET, [] (AsyncWebServerRequest *request) {
        char response[512];
        snprintf(response, sizeof(response),
            "{\"status\":\"ok\",\"data\":{\"enabled\":%s,\"broker_host\":\"%s\",\"broker_port\":%d,\"username\":\"%s\",\"publish_threshold\":%.3f,\"device_id\":\"%s\"}}",
            mqttSettings.Config.enabled ? "true" : "false",
            mqttSettings.Config.broker_host,
            mqttSettings.Config.broker_port,
            mqttSettings.Config.username,
            mqttSettings.Config.publish_threshold,
            mqttSettings.Config.device_id
        );
        request->send(200, "application/json", response);
    });

    // MQTT Configuration - Update
    server.on("/api/v0/mqtt/config", HTTP_POST, [] (AsyncWebServerRequest *request) {
        bool changed = false;

        if (request->hasParam("enabled", true))
        {
            String val = request->getParam("enabled", true)->value();
            mqttSettings.Config.enabled = (val == "true" || val == "1");
            changed = true;
        }

        if (request->hasParam("broker_host", true))
        {
            String val = request->getParam("broker_host", true)->value();
            strncpy(mqttSettings.Config.broker_host, val.c_str(), sizeof(mqttSettings.Config.broker_host) - 1);
            mqttSettings.Config.broker_host[sizeof(mqttSettings.Config.broker_host) - 1] = '\0';
            changed = true;
        }

        if (request->hasParam("broker_port", true))
        {
            mqttSettings.Config.broker_port = request->getParam("broker_port", true)->value().toInt();
            changed = true;
        }

        if (request->hasParam("username", true))
        {
            String val = request->getParam("username", true)->value();
            strncpy(mqttSettings.Config.username, val.c_str(), sizeof(mqttSettings.Config.username) - 1);
            mqttSettings.Config.username[sizeof(mqttSettings.Config.username) - 1] = '\0';
            changed = true;
        }

        if (request->hasParam("password", true))
        {
            String val = request->getParam("password", true)->value();
            strncpy(mqttSettings.Config.password, val.c_str(), sizeof(mqttSettings.Config.password) - 1);
            mqttSettings.Config.password[sizeof(mqttSettings.Config.password) - 1] = '\0';
            changed = true;
        }

        if (request->hasParam("publish_threshold", true))
        {
            mqttSettings.Config.publish_threshold = request->getParam("publish_threshold", true)->value().toFloat();
            changed = true;
        }

        if (request->hasParam("device_id", true))
        {
            String val = request->getParam("device_id", true)->value();
            strncpy(mqttSettings.Config.device_id, val.c_str(), sizeof(mqttSettings.Config.device_id) - 1);
            mqttSettings.Config.device_id[sizeof(mqttSettings.Config.device_id) - 1] = '\0';
            changed = true;
        }

        if (changed)
        {
            mqttSettings.Write();
            MQTT_UpdateSettings();
            request->send(200, "application/json", "{\"status\":\"ok\", \"message\": \"MQTT settings updated. Reconnecting...\"}");
        }
        else
        {
            request->send(400, "application/json", "{\"status\":\"fail\", \"message\": \"No parameters provided.\"}");
        }
    });

    // MQTT Status
    server.on("/api/v0/mqtt/status", HTTP_GET, [] (AsyncWebServerRequest *request) {
        char response[256];
        snprintf(response, sizeof(response),
            "{\"status\":\"ok\",\"data\":{\"enabled\":%s,\"connected\":%s}}",
            MQTT_IsEnabled() ? "true" : "false",
            MQTT_IsConnected() ? "true" : "false"
        );
        request->send(200, "application/json", response);
    });
#endif // CONFIG_ENABLE_MQTT

#if CONFIG_ENABLE_VORON
    // Voron Configuration - Read
    server.on("/api/v0/voron/config", HTTP_GET, [] (AsyncWebServerRequest *request) {
        char response[512];
        snprintf(response, sizeof(response),
            "{\"status\":\"ok\",\"data\":{\"enabled\":%s,\"printer_host\":\"%s\",\"printer_port\":%d,\"reference_diameter\":%.3f,\"reference_flow\":%.1f,\"update_threshold\":%.3f,\"min_flow\":%.1f,\"max_flow\":%.1f,\"update_interval_ms\":%lu}}",
            voronSettings.Config.enabled ? "true" : "false",
            voronSettings.Config.printer_host,
            voronSettings.Config.printer_port,
            voronSettings.Config.reference_diameter,
            voronSettings.Config.reference_flow,
            voronSettings.Config.update_threshold,
            voronSettings.Config.min_flow,
            voronSettings.Config.max_flow,
            (unsigned long)voronSettings.Config.update_interval_ms
        );
        request->send(200, "application/json", response);
    });

    // Voron Configuration - Update
    server.on("/api/v0/voron/config", HTTP_POST, [] (AsyncWebServerRequest *request) {
        bool changed = false;

        if (request->hasParam("enabled", true))
        {
            String val = request->getParam("enabled", true)->value();
            voronSettings.Config.enabled = (val == "true" || val == "1");
            changed = true;
        }

        if (request->hasParam("printer_host", true))
        {
            String val = request->getParam("printer_host", true)->value();
            strncpy(voronSettings.Config.printer_host, val.c_str(), sizeof(voronSettings.Config.printer_host) - 1);
            voronSettings.Config.printer_host[sizeof(voronSettings.Config.printer_host) - 1] = '\0';
            changed = true;
        }

        if (request->hasParam("printer_port", true))
        {
            voronSettings.Config.printer_port = request->getParam("printer_port", true)->value().toInt();
            changed = true;
        }

        if (request->hasParam("reference_diameter", true))
        {
            voronSettings.Config.reference_diameter = request->getParam("reference_diameter", true)->value().toFloat();
            changed = true;
        }

        if (request->hasParam("reference_flow", true))
        {
            voronSettings.Config.reference_flow = request->getParam("reference_flow", true)->value().toFloat();
            changed = true;
        }

        if (request->hasParam("update_threshold", true))
        {
            voronSettings.Config.update_threshold = request->getParam("update_threshold", true)->value().toFloat();
            changed = true;
        }

        if (request->hasParam("min_flow", true))
        {
            voronSettings.Config.min_flow = request->getParam("min_flow", true)->value().toFloat();
            changed = true;
        }

        if (request->hasParam("max_flow", true))
        {
            voronSettings.Config.max_flow = request->getParam("max_flow", true)->value().toFloat();
            changed = true;
        }

        if (request->hasParam("update_interval_ms", true))
        {
            voronSettings.Config.update_interval_ms = request->getParam("update_interval_ms", true)->value().toInt();
            changed = true;
        }

        if (changed)
        {
            voronSettings.Write();
            Voron_UpdateSettings();
            request->send(200, "application/json", "{\"status\":\"ok\", \"message\": \"Voron settings updated.\"}");
        }
        else
        {
            request->send(400, "application/json", "{\"status\":\"fail\", \"message\": \"No parameters provided.\"}");
        }
    });

    // Voron Status
    server.on("/api/v0/voron/status", HTTP_GET, [] (AsyncWebServerRequest *request) {
        char response[256];
        snprintf(response, sizeof(response),
            "{\"status\":\"ok\",\"data\":{\"enabled\":%s,\"last_diameter\":%.3f,\"last_flow\":%d,\"last_error\":%s}}",
            Voron_IsEnabled() ? "true" : "false",
            Voron_GetLastDiameter(),
            Voron_GetLastFlowInt(),
            Voron_GetLastError() ? "true" : "false"
        );
        request->send(200, "application/json", response);
    });

    // Voron Test Connection
    server.on("/api/v0/voron/test", HTTP_POST, [] (AsyncWebServerRequest *request) {
        bool success = Voron_TestConnection();
        if (success)
        {
            request->send(200, "application/json", "{\"status\":\"ok\", \"message\": \"Connection successful.\"}");
        }
        else
        {
            request->send(500, "application/json", "{\"status\":\"fail\", \"message\": \"Connection failed.\"}");
        }
    });
#endif // CONFIG_ENABLE_VORON

}

void format_sensor_data(void)
{
    snprintf(sensor_data, SENSOR_STR_MAX_LEN, "{\"status\":\"ok\",\"data\":{\"diameter\":%.2f,\"min\":%.2f,\"max\":%.2f,\"avg\":%.2f,\"adc\":%d,\"count\":%d}}",
             get_last(), get_min(), get_max(), get_avg(), get_adc(), get_measurements_count());
}
