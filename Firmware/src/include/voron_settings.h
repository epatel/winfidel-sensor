#ifndef __VORON_SETTINGS_H__
#define __VORON_SETTINGS_H__

#include "../config_winfidel.h"
#include <stdint.h>
#include <string.h>

// Forward declaration - PersistSettings template is defined in PersistSettings.h
// which is already included by main.ino
template <class T> class PersistSettings;

// Voron Settings version - increment when struct changes
#define VORON_SETTINGS_VERSION 1

// Voron Settings structure
struct VoronSettingsConfig {
    bool enabled;
    char printer_host[64];
    uint16_t printer_port;
    float reference_diameter;
    float reference_flow;
    float update_threshold;
    float min_flow;
    float max_flow;
    uint32_t update_interval_ms;

    // Default constructor with sensible defaults
    VoronSettingsConfig() :
        enabled(false),
        printer_port(80),
        reference_diameter(1.75f),
        reference_flow(100.0f),
        update_threshold(0.01f),
        min_flow(85.0f),
        max_flow(115.0f),
        update_interval_ms(1000)
    {
        memset(printer_host, 0, sizeof(printer_host));
    }
};

// Global Voron settings instance
extern PersistSettings<VoronSettingsConfig> voronSettings;

#endif // __VORON_SETTINGS_H__
