#if CONFIG_ENABLE_VORON

#include <HTTPClient.h>
#include <WiFi.h>
#include "include/voron_settings.h"

// Persistent settings instance (unique namespace to avoid collision with other settings)
PersistSettings<VoronSettingsConfig> voronSettings(VORON_SETTINGS_VERSION, "VoronSettings");

// State tracking
static float voron_last_diameter = -1.0f;
static float voron_diameter_avg = -1.0f;  // Moving average of diameter
static float voron_last_flow = -1.0f;
static uint32_t voron_last_update_time = 0;
static bool voron_last_error = false;

// Moving average smoothing: avg = (sample + avg * N) / (N + 1)
// N=30 gives ~3% weight to new samples for smoother response
#define VORON_AVG_SMOOTHING 30

// LED error flash state
static uint32_t voron_led_error_end_time = 0;
#define VORON_LED_ERROR_FLASH_MS 200

// Delay buffer constants
#define VORON_BUFFER_SIZE 40
#define VORON_POLL_INTERVAL_MS 2000
#define VORON_POLL_TIMEOUT_MS 2000
#define VORON_SWAP_IDLE_TIME_MS 60000  // 1 minute idle = assume swap possible

// Circular buffer for delayed diameter values
static float voron_diameter_buffer[VORON_BUFFER_SIZE];
static uint8_t voron_buffer_head = 0;      // Next write position
static uint8_t voron_buffer_count = 0;     // Entries in buffer
static float voron_segment_size_mm = 50.0f; // Calculated from distance/40

// Filament tracking
static float voron_cumulative_filament = 0.0f;  // Total across prints
static float voron_last_filament_used = 0.0f;   // Last polled value
static float voron_next_store_threshold = 0.0f; // When to store next sample
static uint32_t voron_last_poll_time = 0;
static uint32_t voron_last_print_end_time = 0;  // For swap detection

// Buffer state
static bool voron_buffer_primed = false;   // Buffer full, ready to apply
static bool voron_polling_failed = false;  // Fallback to real-time mode
static uint8_t voron_poll_fail_count = 0;

// Buffer management functions
static void Voron_BufferReset(float initial_diameter)
{
    for (int i = 0; i < VORON_BUFFER_SIZE; i++)
    {
        voron_diameter_buffer[i] = initial_diameter;
    }
    voron_buffer_head = 0;
    voron_buffer_count = 0;
    voron_buffer_primed = false;
    voron_cumulative_filament = 0.0f;
    voron_last_filament_used = 0.0f;
    voron_next_store_threshold = 0.0f;
    voron_poll_fail_count = 0;
    voron_polling_failed = false;
}

static void Voron_BufferPush(float diameter)
{
    voron_diameter_buffer[voron_buffer_head] = diameter;
    voron_buffer_head = (voron_buffer_head + 1) % VORON_BUFFER_SIZE;
    if (voron_buffer_count < VORON_BUFFER_SIZE)
    {
        voron_buffer_count++;
        if (voron_buffer_count >= VORON_BUFFER_SIZE)
        {
            voron_buffer_primed = true;
            Serial.println("Voron: Delay buffer primed, applying delayed flow rates");
        }
    }
}

static float Voron_BufferPeekOldest(void)
{
    if (voron_buffer_count == 0)
    {
        return voronSettings.Config.reference_diameter;
    }
    uint8_t tail = (voron_buffer_head - voron_buffer_count + VORON_BUFFER_SIZE) % VORON_BUFFER_SIZE;
    return voron_diameter_buffer[tail];
}

static void Voron_CalculateSegmentSize(void)
{
    if (voronSettings.Config.distance_to_extruder_mm > 0)
    {
        voron_segment_size_mm = voronSettings.Config.distance_to_extruder_mm / VORON_BUFFER_SIZE;
        Serial.print("Voron: Delay buffer enabled, segment size=");
        Serial.print(voron_segment_size_mm);
        Serial.println("mm");
    }
}

// Poll Moonraker for print stats
static bool Voron_PollPrintStats(float* filament_used_out, char* state_out, size_t state_len)
{
    char url[192];
    snprintf(url, sizeof(url), "http://%s:%d/printer/objects/query?print_stats",
             voronSettings.Config.printer_host,
             voronSettings.Config.printer_port);

    HTTPClient http;
    http.setTimeout(VORON_POLL_TIMEOUT_MS);
    http.begin(url);

    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK)
    {
        String payload = http.getString();
        http.end();

        // Parse filament_used
        int idx = payload.indexOf("\"filament_used\"");
        if (idx >= 0)
        {
            int colon = payload.indexOf(':', idx);
            if (colon >= 0)
            {
                *filament_used_out = payload.substring(colon + 1).toFloat();
            }
        }

        // Parse state
        if (state_out)
        {
            idx = payload.indexOf("\"state\"");
            if (idx >= 0)
            {
                int quote1 = payload.indexOf('"', idx + 7);
                if (quote1 >= 0)
                {
                    int quote2 = payload.indexOf('"', quote1 + 1);
                    if (quote2 >= 0)
                    {
                        String state = payload.substring(quote1 + 1, quote2);
                        strncpy(state_out, state.c_str(), state_len - 1);
                        state_out[state_len - 1] = '\0';
                    }
                }
            }
        }
        return true;
    }

    http.end();
    return false;
}

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

    // Initialize delay buffer if enabled
    if (voronSettings.Config.distance_to_extruder_mm > 0)
    {
        Voron_CalculateSegmentSize();
        Voron_BufferReset(voronSettings.Config.reference_diameter);
        Serial.print("Voron: Delay buffer distance=");
        Serial.print(voronSettings.Config.distance_to_extruder_mm);
        Serial.println("mm");
    }
    else
    {
        Serial.println("Voron: Real-time mode (no delay buffer)");
    }
}

void Voron_Loop(void)
{
    // Handle LED error flash timing
    if (voron_led_error_end_time > 0 && millis() >= voron_led_error_end_time)
    {
        LED_RED_OFF();
        voron_led_error_end_time = 0;
    }

    // If delay buffer is not enabled or Voron is disabled, nothing to do here
    if (!voronSettings.Config.enabled ||
        voronSettings.Config.distance_to_extruder_mm <= 0 ||
        strlen(voronSettings.Config.printer_host) == 0)
    {
        return;
    }

    uint32_t now = millis();

    // Poll Moonraker at regular intervals
    if (now - voron_last_poll_time >= VORON_POLL_INTERVAL_MS)
    {
        voron_last_poll_time = now;

        float filament_used = 0.0f;
        char state[16] = {0};

        if (Voron_PollPrintStats(&filament_used, state, sizeof(state)))
        {
            voron_poll_fail_count = 0;
            if (voron_polling_failed)
            {
                voron_polling_failed = false;
                Serial.println("Voron: Polling recovered, resuming delay buffer mode");
            }

            // Detect new print (filament_used dropped significantly)
            if (filament_used < voron_last_filament_used - 100.0f)
            {
                uint32_t idle_time = now - voron_last_print_end_time;

                if (idle_time > VORON_SWAP_IDLE_TIME_MS)
                {
                    // Assume filament swap - reset buffer
                    Serial.println("Voron: Detected filament swap, resetting buffer");
                    Voron_BufferReset(voron_diameter_avg > 0 ? voron_diameter_avg : voronSettings.Config.reference_diameter);
                }
                else
                {
                    // Same filament, new print - accumulate
                    Serial.print("Voron: New print detected, accumulating ");
                    Serial.print(voron_last_filament_used);
                    Serial.println("mm");
                    voron_cumulative_filament += voron_last_filament_used;
                }
                voron_next_store_threshold = voron_cumulative_filament + voron_segment_size_mm;
            }

            // Track print completion for swap detection
            if (strcmp(state, "complete") == 0 || strcmp(state, "cancelled") == 0 || strcmp(state, "error") == 0)
            {
                voron_last_print_end_time = now;
            }

            // Calculate effective consumed filament
            float effective_consumed = voron_cumulative_filament + filament_used;

            // Process consumption - advance buffer for each segment consumed
            while (effective_consumed >= voron_next_store_threshold && voron_diameter_avg > 0)
            {
                if (voron_buffer_primed)
                {
                    // Apply oldest measurement's flow rate
                    float delayed_diameter = Voron_BufferPeekOldest();
                    float flow = Voron_CalculateFlow(delayed_diameter);

                    // Round to 1 decimal place
                    float flow_rounded = ((int)(flow * 10.0f + 0.5f)) / 10.0f;

                    // Only send if flow changed
                    if (voron_last_flow < 0 || flow_rounded != voron_last_flow)
                    {
                        if (Voron_SendFlowRate(flow_rounded))
                        {
                            voron_last_diameter = delayed_diameter;
                            voron_last_flow = flow_rounded;
                        }
                    }
                }

                // Push current averaged diameter to buffer
                Voron_BufferPush(voron_diameter_avg);
                voron_next_store_threshold += voron_segment_size_mm;
            }

            voron_last_filament_used = filament_used;
        }
        else
        {
            voron_poll_fail_count++;
            if (voron_poll_fail_count >= 5 && !voron_polling_failed)
            {
                voron_polling_failed = true;
                Serial.println("Voron: Polling failed, falling back to real-time mode");
            }
        }
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

    // Build URL: POST http://<host>/printer/gcode/script?script=M221%20S<flow>
    // Klipper's M221 accepts float values (e.g., M221 S97.3)
    char url[192];
    snprintf(url, sizeof(url), "http://%s:%d/printer/gcode/script?script=M221%%20S%.1f",
             voronSettings.Config.printer_host,
             voronSettings.Config.printer_port,
             flow);

    HTTPClient http;
    http.setTimeout(VORON_HTTP_TIMEOUT_MS);
    http.begin(url);

    int httpCode = http.POST("");

    http.end();

    if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_NO_CONTENT)
    {
        Serial.print("Voron: Flow rate set to ");
        Serial.print(flow, 1);
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

    // Update moving average (always, even if we don't send an update)
    if (voron_diameter_avg < 0.0f)
    {
        // Initialize average with first sample
        voron_diameter_avg = diameter;
    }
    else
    {
        // Exponential moving average: avg = (sample + avg * N) / (N + 1)
        voron_diameter_avg = (diameter + voron_diameter_avg * VORON_AVG_SMOOTHING) / (VORON_AVG_SMOOTHING + 1);
    }

    // If delay buffer is active and polling is working, don't process here
    // Flow updates are handled in Voron_Loop() based on filament consumption
    if (voronSettings.Config.distance_to_extruder_mm > 0 && !voron_polling_failed)
    {
        return false;
    }

    // Real-time mode: send flow updates directly based on measurements

    uint32_t now = millis();

    // Rate limiting - check if enough time has passed
    if (voron_last_update_time > 0 &&
        (now - voron_last_update_time) < voronSettings.Config.update_interval_ms)
    {
        return false;
    }

    // Check threshold - only send if averaged diameter changed significantly
    if (voron_last_diameter >= 0.0f)
    {
        float change = voron_diameter_avg - voron_last_diameter;
        if (change < 0) change = -change;  // abs
        if (change < voronSettings.Config.update_threshold)
        {
            return false;
        }
    }

    // Calculate new flow rate using averaged diameter
    float flow = Voron_CalculateFlow(voron_diameter_avg);

    // Round to 1 decimal place for comparison (0.1% resolution)
    float flow_rounded = ((int)(flow * 10.0f + 0.5f)) / 10.0f;

    // Skip if flow (to 1 decimal) is the same as last sent
    if (voron_last_flow >= 0.0f)
    {
        float last_rounded = ((int)(voron_last_flow * 10.0f + 0.5f)) / 10.0f;
        if (flow_rounded == last_rounded)
        {
            // Update diameter tracking but don't send
            voron_last_diameter = voron_diameter_avg;
            return false;
        }
    }

    // Send flow rate update
    if (Voron_SendFlowRate(flow_rounded))
    {
        voron_last_diameter = voron_diameter_avg;
        voron_last_flow = flow_rounded;
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

float Voron_GetLastFlow(void)
{
    return voron_last_flow;
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
    voron_diameter_avg = -1.0f;
    voron_last_flow = -1.0f;
    voron_last_update_time = 0;
    voron_last_error = false;

    // Recalculate and reset delay buffer if enabled
    if (voronSettings.Config.distance_to_extruder_mm > 0)
    {
        Voron_CalculateSegmentSize();
        Voron_BufferReset(voronSettings.Config.reference_diameter);
    }
}

// Buffer state getters for API
bool Voron_GetBufferPrimed(void)
{
    return voron_buffer_primed;
}

uint8_t Voron_GetBufferCount(void)
{
    return voron_buffer_count;
}

float Voron_GetCumulativeFilament(void)
{
    return voron_cumulative_filament;
}

bool Voron_GetPollingFailed(void)
{
    return voron_polling_failed;
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
float Voron_GetLastFlow(void) { return -1.0f; }
bool Voron_GetLastError(void) { return false; }
bool Voron_IsEnabled(void) { return false; }
void Voron_UpdateSettings(void) {}
bool Voron_GetBufferPrimed(void) { return false; }
uint8_t Voron_GetBufferCount(void) { return 0; }
float Voron_GetCumulativeFilament(void) { return 0.0f; }
bool Voron_GetPollingFailed(void) { return false; }

#endif // CONFIG_ENABLE_VORON
