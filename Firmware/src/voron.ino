#if CONFIG_ENABLE_VORON

#include <HTTPClient.h>
#include <WiFi.h>
#include "include/voron_settings.h"

// Persistent settings instance
PersistSettings<VoronSettingsConfig> voronSettings(VORON_SETTINGS_VERSION);

// State tracking
static float voron_last_diameter = -1.0f;
static int voron_last_flow_int = -1;
static uint32_t voron_last_update_time = 0;
static bool voron_last_error = false;

// LED error flash state
static uint32_t voron_led_error_end_time = 0;
#define VORON_LED_ERROR_FLASH_MS 200

void Voron_Setup(void)
{
    // Initialize persistent settings
    voronSettings.Begin();

    if (!voronSettings.Config.enabled)
    {
        Serial.println("Voron: Disabled in settings");
        return;
    }

    if (strlen(voronSettings.Config.printer_host) == 0)
    {
        Serial.println("Voron: No printer host configured");
        return;
    }

    Serial.print("Voron: Configured for ");
    Serial.print(voronSettings.Config.printer_host);
    Serial.print(":");
    Serial.println(voronSettings.Config.printer_port);
    Serial.print("Voron: Reference diameter=");
    Serial.print(voronSettings.Config.reference_diameter);
    Serial.print("mm, base flow=");
    Serial.print(voronSettings.Config.reference_flow);
    Serial.println("%");
}

void Voron_Loop(void)
{
    // Handle LED error flash timing
    if (voron_led_error_end_time > 0 && millis() >= voron_led_error_end_time)
    {
        LED_RED_OFF();
        voron_led_error_end_time = 0;
    }
}

// Calculate compensated flow rate based on measured diameter
// Formula: new_flow = base_flow × (reference_diameter / measured_diameter)²
static float Voron_CalculateFlow(float measured_diameter)
{
    if (measured_diameter <= 0.0f)
    {
        return voronSettings.Config.reference_flow;
    }

    float ratio = voronSettings.Config.reference_diameter / measured_diameter;
    float flow = voronSettings.Config.reference_flow * ratio * ratio;

    // Clamp to safety limits
    if (flow < voronSettings.Config.min_flow)
    {
        flow = voronSettings.Config.min_flow;
    }
    if (flow > voronSettings.Config.max_flow)
    {
        flow = voronSettings.Config.max_flow;
    }

    return flow;
}

// Signal error by flashing red LED
void Voron_SignalError(void)
{
    LED_RED_ON();
    voron_led_error_end_time = millis() + VORON_LED_ERROR_FLASH_MS;
    voron_last_error = true;
}

// Send flow rate to Voron printer via Moonraker API
bool Voron_SendFlowRate(float flow)
{
    if (strlen(voronSettings.Config.printer_host) == 0)
    {
        return false;
    }

    // Round flow to integer for M221 command
    int flow_int = (int)(flow + 0.5f);

    // Build URL: POST http://<host>/printer/gcode/script?script=M221%20S<flow_int>
    char url[192];
    snprintf(url, sizeof(url), "http://%s:%d/printer/gcode/script?script=M221%%20S%d",
             voronSettings.Config.printer_host,
             voronSettings.Config.printer_port,
             flow_int);

    HTTPClient http;
    http.setTimeout(VORON_HTTP_TIMEOUT_MS);
    http.begin(url);

    int httpCode = http.POST("");

    http.end();

    if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_NO_CONTENT)
    {
        Serial.print("Voron: Flow rate set to ");
        Serial.print(flow_int);
        Serial.println("%");
        voron_last_error = false;
        return true;
    }
    else
    {
        Serial.print("Voron: HTTP POST failed, code=");
        Serial.println(httpCode);
        Voron_SignalError();
        return false;
    }
}

// Process a new measurement and send flow update if needed
// Returns true if a flow rate update was sent to the printer
bool Voron_ProcessMeasurement(float diameter)
{
    if (!voronSettings.Config.enabled)
    {
        return false;
    }

    if (strlen(voronSettings.Config.printer_host) == 0)
    {
        return false;
    }

    uint32_t now = millis();

    // Rate limiting - check if enough time has passed
    if (voron_last_update_time > 0 &&
        (now - voron_last_update_time) < voronSettings.Config.update_interval_ms)
    {
        return false;
    }

    // Check threshold - only send if diameter changed significantly
    if (voron_last_diameter >= 0.0f)
    {
        float change = diameter - voron_last_diameter;
        if (change < 0) change = -change;  // abs
        if (change < voronSettings.Config.update_threshold)
        {
            return false;
        }
    }

    // Calculate new flow rate
    float flow = Voron_CalculateFlow(diameter);
    int flow_int = (int)(flow + 0.5f);

    // Skip if rounded integer is the same as last sent
    if (flow_int == voron_last_flow_int)
    {
        // Update diameter tracking but don't send
        voron_last_diameter = diameter;
        return false;
    }

    // Send flow rate update
    if (Voron_SendFlowRate(flow))
    {
        voron_last_diameter = diameter;
        voron_last_flow_int = flow_int;
        voron_last_update_time = now;
        return true;
    }

    return false;
}

// Test connection to printer
bool Voron_TestConnection(void)
{
    if (strlen(voronSettings.Config.printer_host) == 0)
    {
        return false;
    }

    // Test with a simple query to Moonraker
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/printer/info",
             voronSettings.Config.printer_host,
             voronSettings.Config.printer_port);

    HTTPClient http;
    http.setTimeout(VORON_HTTP_TIMEOUT_MS);
    http.begin(url);

    int httpCode = http.GET();

    http.end();

    bool success = (httpCode == HTTP_CODE_OK);
    if (!success)
    {
        Voron_SignalError();
    }
    return success;
}

// Get current status information
float Voron_GetLastDiameter(void)
{
    return voron_last_diameter;
}

int Voron_GetLastFlowInt(void)
{
    return voron_last_flow_int;
}

bool Voron_GetLastError(void)
{
    return voron_last_error;
}

bool Voron_IsEnabled(void)
{
    return voronSettings.Config.enabled;
}

// Update settings and reset state
void Voron_UpdateSettings(void)
{
    // Reset state when settings change
    voron_last_diameter = -1.0f;
    voron_last_flow_int = -1;
    voron_last_update_time = 0;
    voron_last_error = false;
}

#else // CONFIG_ENABLE_VORON

// Stub functions when Voron is disabled
void Voron_Setup(void) {}
void Voron_Loop(void) {}
bool Voron_ProcessMeasurement(float diameter) { (void)diameter; return false; }
bool Voron_SendFlowRate(float flow) { (void)flow; return false; }
void Voron_SignalError(void) {}
bool Voron_TestConnection(void) { return false; }
float Voron_GetLastDiameter(void) { return -1.0f; }
int Voron_GetLastFlowInt(void) { return -1; }
bool Voron_GetLastError(void) { return false; }
bool Voron_IsEnabled(void) { return false; }
void Voron_UpdateSettings(void) {}

#endif // CONFIG_ENABLE_VORON
