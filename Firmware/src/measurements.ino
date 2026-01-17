float gReadingMax = 0.0;            // Holds maximum reading
float gReadingMin = 99999.9;        // Holds minimum reading
float gReadingAvg = 0.0;            // Holds running average
float gReadingLast = 0.0;           // Holds value of last reading
uint32_t nLastADC = 0;              // Last sampled ADC value
uint32_t numMeasurements = 0;       // Counts how many readings we had
uint32_t nNextMeasurementTick = 0;  // When next measurement should occur

// LED timing
static uint32_t ledFlashEndTime = 0;
#define LED_BRIEF_FLASH_MS          30      // Brief flash on each measurement
#define LED_CHANGE_FLASH_MS         120     // Longer flash on MQTT publish

float get_last(void)
{
    return gReadingLast;
}
float get_min(void)
{
    return gReadingMin;
}
float get_max(void)
{
    return gReadingMax;
}
float get_avg(void)
{
    return gReadingAvg;
}
uint32_t get_adc(void)
{
    return nLastADC;
}
uint32_t get_measurements_count(void)
{
    return numMeasurements;
}

void reset_stats(void)
{
    gReadingMax = 0.0;
    gReadingMin = 99999.9;
    gReadingAvg = 0.0;
    numMeasurements = 0;
}


void Measurements_Tick(void)
{
    // Turn off LED after flash duration
    if (ledFlashEndTime > 0 && millis() >= ledFlashEndTime)
    {
        LED_MEASUREMENT_OFF();
        ledFlashEndTime = 0;
    }

    if (millis() >= nNextMeasurementTick)
    {
        // Take `ADC_SAMPLES_PER_MEASUREMENT_CYCLE` number of ADC samples
        nLastADC = adc_sample_data(ADC_SAMPLES_PER_MEASUREMENT_CYCLE);

        // Should we use mean or average (already calculated above)?
        #ifdef ADC_FINAL_ADC_VALUE_USING_MEAN
        // Use ADC mean
        nLastADC = adc_get_mean();
        #endif

        if (nLastADC >= ADC_MAX)
        {
            Serial.print("Invalid ADC value of `");
            Serial.print(nLastADC);
            Serial.print("`. Clipping to `");
            Serial.print(ADC_MAX);
            Serial.println("`.");
            nLastADC = ADC_MAX;
        }

        gReadingLast = adc_to_mm(nLastADC);

        // Update running average: new_avg = (old_avg * n + new_sample) / (n + 1)
        gReadingAvg = (gReadingAvg * numMeasurements + gReadingLast) / (numMeasurements + 1);

        // Update min
        if (gReadingLast < gReadingMin)
        {
            gReadingMin = gReadingLast;
        }

        // Update max
        if (gReadingLast > gReadingMax)
        {
            gReadingMax = gReadingLast;
        }

        // Update reading counter
        numMeasurements++;

        // Publish measurement via MQTT and send Voron flow update
        bool mqttPublished = false;
        bool voronUpdated = false;
#if CONFIG_ENABLE_MQTT
        mqttPublished = MQTT_Publish_Measurement();
#endif // CONFIG_ENABLE_MQTT

#if CONFIG_ENABLE_VORON
        voronUpdated = Voron_ProcessMeasurement(gReadingLast);
#endif // CONFIG_ENABLE_VORON

        // Update the status LED - dim brief flash normally, bright longer on MQTT/Voron update
        if (mqttPublished || voronUpdated)
        {
            // MQTT published or Voron updated - longer bright flash
            ledFlashEndTime = millis() + LED_CHANGE_FLASH_MS;
            LED_MEASUREMENT_ON();  // Full brightness
        }
        else
        {
            // Normal measurement - brief dim flash
            ledFlashEndTime = millis() + LED_BRIEF_FLASH_MS;
            LED_MEASUREMENT_DIM_ON();  // Dimmed
        }

        // Measurement printing over USB-CDC
        #ifdef CONFIG_PRINT_MEASUREMENTS_USB_CDC
        if (bSerialPrintoutRequested && (Serial.availableForWrite()>9))
        {
            Serial.print(">");
            Serial.print(gReadingLast);
            Serial.print("mm\r\n");

            // Toggle serial LED on each printout
            if (numMeasurements & 1)
            {
                LED_SERIAL_ON();
            }
            else
            {
                LED_SERIAL_OFF();
            }
        }
        #endif // CONFIG_PRINT_MEASUREMENTS_USB_CDC

        // Measurement printing over Serial
        #ifdef CONFIG_PRINT_MEASUREMENTS_UART_GPIO
        Serial0.print(">");
        Serial0.print(gReadingLast);
        Serial0.print("mm\r\n");
        #endif //CONFIG_PRINT_MEASUREMENTS_UART_GPIO


        // Set next update timestamp
        nNextMeasurementTick = millis() + 200;
    }
}
