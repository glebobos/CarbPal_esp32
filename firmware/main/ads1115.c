#include "ads1115.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "ads1115";

// Spinlock protecting shared state read by multiple tasks
static portMUX_TYPE s_data_mux = portMUX_INITIALIZER_UNLOCKED;

// Calibration Offsets (in kPa) — protected by s_data_mux
static float s_offsets[3] = {0.0f, 0.0f, 0.0f};

// Current filtered pressure values (in kPa) — protected by s_data_mux
static float s_pressures[3] = {0.0f, 0.0f, 0.0f};

// EMA filter initialization flags (true = seeded with first valid sample)
static bool s_ema_initialized[3] = {false, false, false};

// Calibration state flags
static bool s_is_calibrated = false;
static volatile bool s_trigger_cal = false;

// Write a 16-bit register on ADS1115 (handling Big Endian byte swap)
static esp_err_t ads1115_write_reg(uint8_t dev_addr, uint8_t reg_addr, uint16_t val) {
    uint8_t write_buf[3];
    write_buf[0] = reg_addr;
    write_buf[1] = (val >> 8) & 0xFF;
    write_buf[2] = val & 0xFF;
    return i2c_master_write_to_device(ADS1115_I2C_PORT, dev_addr, write_buf, 3, pdMS_TO_TICKS(100));
}

// Read a 16-bit register from ADS1115 (handling Big Endian byte swap)
static esp_err_t ads1115_read_reg(uint8_t dev_addr, uint8_t reg_addr, uint16_t *val) {
    uint8_t data[2];
    esp_err_t err = i2c_master_write_read_device(ADS1115_I2C_PORT, dev_addr, &reg_addr, 1, data, 2, pdMS_TO_TICKS(100));
    if (err == ESP_OK) {
        *val = (data[0] << 8) | data[1];
    }
    return err;
}

// Trigger and read a single-shot conversion on a specific differential channel
static esp_err_t ads1115_read_differential(uint8_t dev_addr, uint16_t mux_channel, int16_t *raw_val) {
    // Configure with base parameters plus mux channel
    uint16_t config = ADS1115_CONFIG_BASE | mux_channel;
    
    esp_err_t err = ads1115_write_reg(dev_addr, ADS1115_REG_CONFIG, config);
    if (err != ESP_OK) {
        return err;
    }
    
    // Wait for the conversion to complete.
    // 128 SPS conversion takes ~7.8ms. We sleep for 9ms then poll if necessary.
    vTaskDelay(pdMS_TO_TICKS(9));
    
    uint16_t current_config = 0;
    int retries = 10;
    while (retries-- > 0) {
        err = ads1115_read_reg(dev_addr, ADS1115_REG_CONFIG, &current_config);
        if (err != ESP_OK) {
            return err; // I2C communication error, fail fast!
        }
        if (current_config & 0x8000) {
            break; // OS bit is 1, indicating conversion is ready
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    if (retries < 0) {
        ESP_LOGE(TAG, "Timeout waiting for conversion (addr 0x%02X, mux 0x%04X)", dev_addr, mux_channel);
        return ESP_ERR_TIMEOUT;
    }
    
    // Read conversion results
    uint16_t val_unsigned = 0;
    err = ads1115_read_reg(dev_addr, ADS1115_REG_CONVERSION, &val_unsigned);
    if (err == ESP_OK) {
        *raw_val = (int16_t)val_unsigned;
    }
    return err;
}

// Background task to continuously poll pressure sensors, filter signals and run calibration
static void ads1115_task(void *pvParameters) {
    ESP_LOGI(TAG, "ADS1115 acquisition task started.");
    
    // Run the initial calibration on startup
    ads1115_calibrate();
    
    float alpha = 0.05f; // Exponential moving average coefficient
    
    while (1) {
        if (s_trigger_cal) {
            s_trigger_cal = false;
            ads1115_calibrate();
        }
        
        int16_t raw1 = 0, raw2 = 0, raw3 = 0;
        esp_err_t err1 = ads1115_read_differential(ADS1115_ADDR_2, ADS1115_MUX_DIFF_0_1, &raw1);
        esp_err_t err2 = ads1115_read_differential(ADS1115_ADDR_2, ADS1115_MUX_DIFF_2_3, &raw2);
        esp_err_t err3 = ads1115_read_differential(ADS1115_ADDR_1, ADS1115_MUX_DIFF_0_1, &raw3);
        
        // Read current offsets under lock (they may be updated by calibration)
        taskENTER_CRITICAL(&s_data_mux);
        float off0 = s_offsets[0];
        float off1 = s_offsets[1];
        float off2 = s_offsets[2];
        taskEXIT_CRITICAL(&s_data_mux);
        
        if (err1 == ESP_OK) {
            float val1 = ((float)raw1 * ADS1115_COUNTS_TO_KPA) - off0;
            taskENTER_CRITICAL(&s_data_mux);
            if (!s_ema_initialized[0]) {
                s_pressures[0] = val1;
                s_ema_initialized[0] = true;
            } else {
                s_pressures[0] = alpha * val1 + (1.0f - alpha) * s_pressures[0];
            }
            taskEXIT_CRITICAL(&s_data_mux);
        } else {
            // Keep the last known good value; don't reset the EMA filter
            ESP_LOGW(TAG, "Sensor 1 read failed, holding last value");
        }
        
        if (err2 == ESP_OK) {
            float val2 = ((float)raw2 * ADS1115_COUNTS_TO_KPA) - off1;
            taskENTER_CRITICAL(&s_data_mux);
            if (!s_ema_initialized[1]) {
                s_pressures[1] = val2;
                s_ema_initialized[1] = true;
            } else {
                s_pressures[1] = alpha * val2 + (1.0f - alpha) * s_pressures[1];
            }
            taskEXIT_CRITICAL(&s_data_mux);
        } else {
            ESP_LOGW(TAG, "Sensor 2 read failed, holding last value");
        }
        
        if (err3 == ESP_OK) {
            float val3 = ((float)raw3 * ADS1115_COUNTS_TO_KPA) - off2;
            taskENTER_CRITICAL(&s_data_mux);
            if (!s_ema_initialized[2]) {
                s_pressures[2] = val3;
                s_ema_initialized[2] = true;
            } else {
                s_pressures[2] = alpha * val3 + (1.0f - alpha) * s_pressures[2];
            }
            taskEXIT_CRITICAL(&s_data_mux);
        } else {
            ESP_LOGW(TAG, "Sensor 3 read failed, holding last value");
        }
        
        // Run at 20Hz (50ms interval) to collect data faster than the 10Hz WS update rate
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

esp_err_t ads1115_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = ADS1115_SDA_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = ADS1115_SCL_GPIO,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = ADS1115_I2C_FREQ_HZ,
    };
    
    esp_err_t err = i2c_param_config(ADS1115_I2C_PORT, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_param_config failed: %s", esp_err_to_name(err));
        return err;
    }
    
    err = i2c_driver_install(ADS1115_I2C_PORT, conf.mode, 0, 0, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }
    
    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, "I2C Master Driver initialized successfully.");
    ESP_LOGI(TAG, "Scanning bus for ADS1115 A/D Converters...");
    ESP_LOGI(TAG, "=================================================");
    
    bool found_adc1 = false;
    bool found_adc2 = false;
    
    uint16_t dummy_cfg;
    // Simple read check for ADS1115 #1 (Address 0x48)
    err = ads1115_read_reg(ADS1115_ADDR_1, ADS1115_REG_CONFIG, &dummy_cfg);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "[I2C] Found ADS1115 #1 at Address 0x%02X (Sensor 3)", ADS1115_ADDR_1);
        found_adc1 = true;
    } else {
        ESP_LOGE(TAG, "[I2C] MISSING ADS1115 #1 at Address 0x%02X! Check SCL/SDA lines and ADDR pin connection. (err=%s)", ADS1115_ADDR_1, esp_err_to_name(err));
    }
    
    // Simple read check for ADS1115 #2 (Address 0x49)
    err = ads1115_read_reg(ADS1115_ADDR_2, ADS1115_REG_CONFIG, &dummy_cfg);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "[I2C] Found ADS1115 #2 at Address 0x%02X (Sensor 1 & Sensor 2)", ADS1115_ADDR_2);
        found_adc2 = true;
    } else {
        ESP_LOGE(TAG, "[I2C] MISSING ADS1115 #2 at Address 0x%02X! Check SCL/SDA lines and ADDR pin connection. (err=%s)", ADS1115_ADDR_2, esp_err_to_name(err));
    }
    
    if (found_adc1 && found_adc2) {
        ESP_LOGI(TAG, "[I2C] Debug Status: OK (All I2C devices detected on startup)");
    } else {
        ESP_LOGE(TAG, "[I2C] Debug Status: FAIL (Some I2C devices did not respond. Check connections)");
    }
    ESP_LOGI(TAG, "=================================================");
    
    // Spawn the background data acquisition and filtering task
    xTaskCreate(ads1115_task, "ads1115_acq", 4096, NULL, 5, NULL);
    
    return ESP_OK;
}

void ads1115_calibrate(void) {
    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, "[CALIBRATION] Starting automatic zero-offset calibration (2 seconds)...");
    ESP_LOGI(TAG, "[CALIBRATION] Please ensure engine is OFF and sensors are at atmospheric pressure.");
    
    int total_samples = 50;
    
    // Store individual samples for outlier rejection (second pass)
    float samples[3][50];
    int valid_samples[3] = {0, 0, 0};
    
    // First pass: collect all samples
    for (int step = 0; step < total_samples; step++) {
        int16_t raw1 = 0, raw2 = 0, raw3 = 0;
        esp_err_t err1 = ads1115_read_differential(ADS1115_ADDR_2, ADS1115_MUX_DIFF_0_1, &raw1);
        esp_err_t err2 = ads1115_read_differential(ADS1115_ADDR_2, ADS1115_MUX_DIFF_2_3, &raw2);
        esp_err_t err3 = ads1115_read_differential(ADS1115_ADDR_1, ADS1115_MUX_DIFF_0_1, &raw3);
        
        if (err1 == ESP_OK && valid_samples[0] < total_samples) {
            samples[0][valid_samples[0]++] = (float)raw1 * ADS1115_COUNTS_TO_KPA;
        }
        if (err2 == ESP_OK && valid_samples[1] < total_samples) {
            samples[1][valid_samples[1]++] = (float)raw2 * ADS1115_COUNTS_TO_KPA;
        }
        if (err3 == ESP_OK && valid_samples[2] < total_samples) {
            samples[2][valid_samples[2]++] = (float)raw3 * ADS1115_COUNTS_TO_KPA;
        }
        vTaskDelay(pdMS_TO_TICKS(40)); // 40ms step
    }
    
    ESP_LOGI(TAG, "[CALIBRATION] Calibration results:");
    bool any_calibrated = false;
    for (int i = 0; i < 3; i++) {
        if (valid_samples[i] > 25) {  // Require majority of samples (>50%)
            // First pass: compute mean and stddev from all valid samples
            double sum = 0.0;
            double sum_sq = 0.0;
            float min_val = 9999.0f;
            float max_val = -9999.0f;
            
            for (int j = 0; j < valid_samples[i]; j++) {
                float v = samples[i][j];
                sum += v;
                sum_sq += (double)v * v;
                if (v < min_val) min_val = v;
                if (v > max_val) max_val = v;
            }
            
            double mean = sum / valid_samples[i];
            double variance = (sum_sq / valid_samples[i]) - (mean * mean);
            if (variance < 0) variance = 0.0;
            double stddev = sqrt(variance);
            
            // Second pass: trimmed mean — exclude samples outside [mean ± 2*stddev]
            double trimmed_sum = 0.0;
            int trimmed_count = 0;
            double lo_bound = mean - 2.0 * stddev;
            double hi_bound = mean + 2.0 * stddev;
            
            for (int j = 0; j < valid_samples[i]; j++) {
                float v = samples[i][j];
                if (v >= lo_bound && v <= hi_bound) {
                    trimmed_sum += v;
                    trimmed_count++;
                }
            }
            
            // Use trimmed mean if we still have enough samples, otherwise fall back to raw mean
            float offset;
            if (trimmed_count > valid_samples[i] / 2) {
                offset = (float)(trimmed_sum / trimmed_count);
            } else {
                offset = (float)mean;
                ESP_LOGW(TAG, "  Sensor %d: Too many outliers, using raw mean", i + 1);
            }
            
            taskENTER_CRITICAL(&s_data_mux);
            s_offsets[i] = offset;
            taskEXIT_CRITICAL(&s_data_mux);
            
            ESP_LOGI(TAG, "  Sensor %d (Channel %s on Addr 0x%02X):", 
                     i + 1, 
                     (i == 0) ? "0-1" : ((i == 1) ? "2-3" : "0-1"),
                     (i < 2) ? ADS1115_ADDR_2 : ADS1115_ADDR_1);
            ESP_LOGI(TAG, "    - Calculated Offset : %.4f kPa (trimmed %d/%d samples)", offset, trimmed_count, valid_samples[i]);
            ESP_LOGI(TAG, "    - Min / Max Range   : [%.4f, %.4f] kPa (Span: %.4f kPa)", min_val, max_val, max_val - min_val);
            ESP_LOGI(TAG, "    - Standard Dev      : %.4f kPa", stddev);
            
            // Check stability
            if (stddev > 0.3f || (max_val - min_val) > 1.0f) {
                ESP_LOGW(TAG, "    [WARNING] Sensor %d readings are unstable! Standard deviation is high. Verify hoses are closed.", i + 1);
            } else {
                ESP_LOGI(TAG, "    - Sensor %d check: STABLE / READY", i + 1);
            }
            any_calibrated = true;
        } else {
            taskENTER_CRITICAL(&s_data_mux);
            s_offsets[i] = 0.0f;
            taskEXIT_CRITICAL(&s_data_mux);
            ESP_LOGE(TAG, "  Sensor %d (Addr 0x%02X): CALIBRATION FAILED (only %d/%d valid samples)", 
                     i + 1, (i < 2) ? ADS1115_ADDR_2 : ADS1115_ADDR_1,
                     valid_samples[i], total_samples);
        }
    }
    
    // Reset EMA filter state so it re-seeds with the new offsets
    taskENTER_CRITICAL(&s_data_mux);
    for (int i = 0; i < 3; i++) {
        s_pressures[i] = 0.0f;
        s_ema_initialized[i] = false;
    }
    taskEXIT_CRITICAL(&s_data_mux);
    
    s_is_calibrated = any_calibrated;
    ESP_LOGI(TAG, "=================================================");
}

void ads1115_trigger_calibration(void) {
    s_trigger_cal = true;
    ESP_LOGI(TAG, "Remote calibration requested. It will execute on the next acquisition cycle.");
}

float ads1115_get_pressure(int sensor_idx) {
    if (sensor_idx >= 0 && sensor_idx < 3) {
        taskENTER_CRITICAL(&s_data_mux);
        float val = s_pressures[sensor_idx];
        taskEXIT_CRITICAL(&s_data_mux);
        return val;
    }
    return 0.0f;
}

bool ads1115_is_calibrated(void) {
    return s_is_calibrated;
}
