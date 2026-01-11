/*
Winfidel - Web
Description: This file contains all the functions we use for receiving and responding to HTTP requests
*/

#include "version.h"
#include <Update.h>

// Simple OTA update page
const char* ota_html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>WInFiDEL Firmware Update</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial, sans-serif; margin: 40px; text-align: center; }
        h1 { color: #333; }
        form { margin: 20px auto; max-width: 400px; }
        input[type=file] { margin: 20px 0; }
        input[type=submit] { background: #007bff; color: white; padding: 10px 30px;
                             border: none; cursor: pointer; font-size: 16px; }
        input[type=submit]:hover { background: #0056b3; }
        #progress { margin-top: 20px; display: none; }
        .bar { height: 20px; background: #007bff; width: 0%; transition: width 0.3s; }
        .bar-bg { background: #eee; border-radius: 4px; overflow: hidden; }
    </style>
</head>
<body>
    <h1>WInFiDEL Firmware Update</h1>
    <form method="POST" action="/update" enctype="multipart/form-data" id="upload_form">
        <input type="file" name="firmware" accept=".bin" required><br>
        <input type="submit" value="Update Firmware">
    </form>
    <div id="progress"><div class="bar-bg"><div class="bar" id="bar"></div></div><span id="pct">0%</span></div>
    <p><a href="/">Back to Home</a></p>
</body>
</html>
)rawliteral";

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
            Serial.println("Device reboot request recevied. Rebooting...");
            ESP.restart();
        }
        else
        {
            request->send(400, "application/json", "{\"status\":\"fail\", \"message\": \"Missing argument `confirm`!\"}");
        }
    });

    // Web-based OTA update page
    server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/html", ota_html);
    });

    // Handle firmware upload
    server.on("/update", HTTP_POST,
        [](AsyncWebServerRequest *request) {
            bool success = !Update.hasError();
            AsyncWebServerResponse *response = request->beginResponse(200, "text/plain",
                success ? "Update successful! Rebooting..." : "Update failed!");
            response->addHeader("Connection", "close");
            request->send(response);
            if (success) {
                delay(1000);
                ESP.restart();
            }
        },
        [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            if (!index) {
                Serial.printf("Update Start: %s\n", filename.c_str());
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                    Update.printError(Serial);
                }
            }
            if (Update.write(data, len) != len) {
                Update.printError(Serial);
            }
            if (final) {
                if (Update.end(true)) {
                    Serial.printf("Update Success: %u bytes\n", index + len);
                } else {
                    Update.printError(Serial);
                }
            }
        }
    );

}

void format_sensor_data(void)
{
    snprintf(sensor_data, SENSOR_STR_MAX_LEN, "{\"status\":\"ok\",\"data\":{\"diameter\":%.2f,\"min\":%.2f,\"max\":%.2f,\"avg\":%.2f,\"adc\":%d,\"count\":%d}}",
             get_last(), get_min(), get_max(), get_avg(), get_adc(), get_measurements_count());
}
