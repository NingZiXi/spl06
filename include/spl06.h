/*
 * SPDX-FileCopyrightText: 2026 NingZiXi
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SPL06_I2C_ADDRESS_LOW      0x76
#define SPL06_I2C_ADDRESS_HIGH     0x77
#define SPL06_CHIP_ID              0x10
#define SPL06_RESET_VALUE          0x09
#define SPL06_SEA_LEVEL_PA_DEFAULT 101325.0f

typedef enum {
    SPL06_MEAS_RATE_1_HZ   = 0,
    SPL06_MEAS_RATE_2_HZ   = 1,
    SPL06_MEAS_RATE_4_HZ   = 2,
    SPL06_MEAS_RATE_8_HZ   = 3,
    SPL06_MEAS_RATE_16_HZ  = 4,
    SPL06_MEAS_RATE_32_HZ  = 5,
    SPL06_MEAS_RATE_64_HZ  = 6,
    SPL06_MEAS_RATE_128_HZ = 7,
} spl06_measurement_rate_t;

typedef enum {
    SPL06_OVERSAMPLING_1   = 0,
    SPL06_OVERSAMPLING_2   = 1,
    SPL06_OVERSAMPLING_4   = 2,
    SPL06_OVERSAMPLING_8   = 3,
    SPL06_OVERSAMPLING_16  = 4,
    SPL06_OVERSAMPLING_32  = 5,
    SPL06_OVERSAMPLING_64  = 6,
    SPL06_OVERSAMPLING_128 = 7,
} spl06_oversampling_t;

typedef enum {
    SPL06_TEMPERATURE_SENSOR_INTERNAL = 0,
    SPL06_TEMPERATURE_SENSOR_EXTERNAL = 1,
} spl06_temperature_sensor_t;

typedef enum {
    SPL06_MODE_STANDBY                         = 0x00,
    SPL06_MODE_COMMAND_PRESSURE               = 0x01,
    SPL06_MODE_COMMAND_TEMPERATURE            = 0x02,
    SPL06_MODE_CONTINUOUS_PRESSURE            = 0x05,
    SPL06_MODE_CONTINUOUS_TEMPERATURE         = 0x06,
    SPL06_MODE_CONTINUOUS_PRESSURE_TEMPERATURE = 0x07,
} spl06_mode_t;

typedef struct {
    int16_t c0;
    int16_t c1;
    int32_t c00;
    int32_t c10;
    int16_t c01;
    int16_t c11;
    int16_t c20;
    int16_t c21;
    int16_t c30;
} spl06_calibration_t;

typedef struct {
    uint8_t i2c_address;
    uint32_t timeout_ms;
    uint32_t scl_speed_hz;
    uint32_t scl_wait_us;
    spl06_mode_t mode;
    spl06_measurement_rate_t pressure_rate;
    spl06_oversampling_t pressure_oversampling;
    spl06_measurement_rate_t temperature_rate;
    spl06_oversampling_t temperature_oversampling;
    spl06_temperature_sensor_t temperature_sensor;
} spl06_config_t;

typedef struct {
    i2c_master_bus_handle_t i2c_bus;
    i2c_master_dev_handle_t i2c_dev;
    uint8_t i2c_address;
    uint8_t chip_id;
    uint32_t timeout_ms;
    uint32_t scl_speed_hz;
    float pressure_scale_factor;
    float temperature_scale_factor;
    spl06_mode_t mode;
    spl06_calibration_t calibration;
    bool initialized;
} spl06_t;

void spl06_init_default_config(spl06_config_t *config);
esp_err_t spl06_init(spl06_t *dev, i2c_master_bus_handle_t bus_handle, const spl06_config_t *config);
esp_err_t spl06_deinit(spl06_t *dev);
esp_err_t spl06_reset(spl06_t *dev);
esp_err_t spl06_is_ready(spl06_t *dev, bool *ready);
esp_err_t spl06_read_raw(spl06_t *dev, int32_t *raw_temperature, int32_t *raw_pressure);
esp_err_t spl06_read_temperature(spl06_t *dev, float *temperature_c);
esp_err_t spl06_read_pressure(spl06_t *dev, float *pressure_pa);
esp_err_t spl06_read_temperature_pressure(spl06_t *dev, float *temperature_c, float *pressure_pa);
float spl06_calculate_altitude(float pressure_pa, float sea_level_pressure_pa);

#ifdef __cplusplus
}
#endif
