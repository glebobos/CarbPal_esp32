#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// I2C configuration settings
#define ADS1115_I2C_PORT         0 // I2C_NUM_0
#define ADS1115_SDA_GPIO         6
#define ADS1115_SCL_GPIO         7
#define ADS1115_I2C_FREQ_HZ      100000

// I2C addresses for the two ADS1115 converters
#define ADS1115_ADDR_1           0x48 // ADDR pin to GND (Sensor 1 & 2)
#define ADS1115_ADDR_2           0x49 // ADDR pin to VDD (Sensor 3)

// Register pointers
#define ADS1115_REG_CONVERSION   0x00
#define ADS1115_REG_CONFIG       0x01
#define ADS1115_REG_LO_THRESH    0x02
#define ADS1115_REG_HI_THRESH    0x03

// Configuration defaults
// OS (Bit 15): 1 = Start single-shot conversion
// MODE (Bit 8): 1 = Single-shot mode
// PGA (Bits 11-9): 101 = +/- 0.256V (maximum gain/resolution)
// DR (Bits 7-5): 100 = 128 SPS
// Comparator bits (Bits 4-0): 0x03 = traditional, active low, non-latching, disable comparator queue
#define ADS1115_CONFIG_BASE      0x8B83 // Single-shot, +/-0.256V PGA, 128 SPS, disable comparator

// Multiplexer settings (Bits 14-12)
#define ADS1115_MUX_DIFF_0_1     0x0000 // Differential AIN0 - AIN1
#define ADS1115_MUX_DIFF_2_3     0x3000 // Differential AIN2 - AIN3

// ADC counts to kPa conversion factor
// Derivation: (±0.256V full scale / 32768 counts) * sensor_kPa_per_volt
//           = 7.8125 µV/count * 757.576 kPa/V = 0.00591856 kPa/count
#define ADS1115_COUNTS_TO_KPA    0.00591856f

/**
 * @brief Initialize the I2C master interface and verify ADS1115 sensors presence.
 * @return ESP_OK if I2C successfully initialized.
 */
esp_err_t ads1115_init(void);

/**
 * @brief Run a 2-second zero-offset calibration routine (calculates baseline offsets).
 * Note: Must be run when engine is OFF (atmospheric pressure).
 */
void ads1115_calibrate(void);

/**
 * @brief Triggers an asynchronous zero-offset calibration run (e.g. from UI command).
 */
void ads1115_trigger_calibration(void);

/**
 * @brief Get the latest filtered, calibrated pressure measurement for a sensor.
 * @param sensor_idx Sensor index (0 = Left balance, 1 = Center balance, 2 = Right balance)
 * @return Calibrated pressure in kPa.
 */
float ads1115_get_pressure(int sensor_idx);

/**
 * @brief Check if the sensors have completed zero-offset calibration.
 * @return true if calibrated.
 */
bool ads1115_is_calibrated(void);

#ifdef __cplusplus
}
#endif
