#include "spl06.h"

#include <math.h>
#include <string.h>

#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SPL06_REG_PRESSURE_MSB   0x00
#define SPL06_REG_TEMPERATURE_MSB 0x03
#define SPL06_REG_PRS_CFG        0x06
#define SPL06_REG_TMP_CFG        0x07
#define SPL06_REG_MEAS_CFG       0x08
#define SPL06_REG_CFG_REG        0x09
#define SPL06_REG_RESET          0x0C
#define SPL06_REG_CHIP_ID        0x0D
#define SPL06_REG_CALIB          0x10

#define SPL06_MEAS_CFG_COEF_RDY  BIT(7)
#define SPL06_MEAS_CFG_SENSOR_RDY BIT(6)
#define SPL06_CFG_T_SHIFT        BIT(3)
#define SPL06_CFG_P_SHIFT        BIT(2)

#define SPL06_READY_RETRIES      20
#define SPL06_READY_DELAY_MS     10

#define SPL06_CHECK_ARG(expr) do { \
    if (!(expr)) { \
        return ESP_ERR_INVALID_ARG; \
    } \
} while (0)

static const float s_scale_factors[] = {
    524288.0f,
    1572864.0f,
    3670016.0f,
    7864320.0f,
    253952.0f,
    516096.0f,
    1040384.0f,
    2088960.0f,
};

static TickType_t spl06_timeout_ticks(const spl06_t *dev)
{
    return pdMS_TO_TICKS(dev->timeout_ms);
}

static int32_t spl06_sign_extend(uint32_t value, uint8_t bits)
{
    const uint32_t sign_bit = 1UL << (bits - 1U);

    return (int32_t)((value ^ sign_bit) - sign_bit);
}

static float spl06_scale_factor(spl06_oversampling_t oversampling)
{
    return s_scale_factors[(int)oversampling];
}

static esp_err_t spl06_write_reg(spl06_t *dev, uint8_t reg, uint8_t value)
{
    uint8_t buffer[2] = { reg, value };

    return i2c_master_write_to_device(dev->i2c_port, dev->i2c_address, buffer, sizeof(buffer),
                                      spl06_timeout_ticks(dev));
}

static esp_err_t spl06_read_reg(spl06_t *dev, uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_write_read_device(dev->i2c_port, dev->i2c_address, &reg, sizeof(reg), data, len,
                                        spl06_timeout_ticks(dev));
}

static esp_err_t spl06_read_chip_id(spl06_t *dev, uint8_t *chip_id)
{
    return spl06_read_reg(dev, SPL06_REG_CHIP_ID, chip_id, 1);
}

static esp_err_t spl06_wait_ready(spl06_t *dev)
{
    uint8_t meas_cfg = 0;

    for (int retry = 0; retry < SPL06_READY_RETRIES; retry++) {
        ESP_RETURN_ON_ERROR(spl06_read_reg(dev, SPL06_REG_MEAS_CFG, &meas_cfg, 1), "spl06",
                            "failed to read measurement status");

        if ((meas_cfg & (SPL06_MEAS_CFG_COEF_RDY | SPL06_MEAS_CFG_SENSOR_RDY)) ==
            (SPL06_MEAS_CFG_COEF_RDY | SPL06_MEAS_CFG_SENSOR_RDY)) {
            return ESP_OK;
        }

        vTaskDelay(pdMS_TO_TICKS(SPL06_READY_DELAY_MS));
    }

    return ESP_ERR_TIMEOUT;
}

static esp_err_t spl06_read_calibration(spl06_t *dev)
{
    uint8_t buf[18] = { 0 };

    ESP_RETURN_ON_ERROR(spl06_read_reg(dev, SPL06_REG_CALIB, buf, sizeof(buf)), "spl06",
                        "failed to read calibration data");

    dev->calibration.c0 = (int16_t)spl06_sign_extend(((uint32_t)buf[0] << 4) | (buf[1] >> 4), 12);
    dev->calibration.c1 = (int16_t)spl06_sign_extend((((uint32_t)buf[1] & 0x0fU) << 8) | buf[2], 12);
    dev->calibration.c00 = spl06_sign_extend(((uint32_t)buf[3] << 12) | ((uint32_t)buf[4] << 4) |
                                                 ((uint32_t)buf[5] >> 4),
                                             20);
    dev->calibration.c10 = spl06_sign_extend((((uint32_t)buf[5] & 0x0fU) << 16) | ((uint32_t)buf[6] << 8) |
                                                 buf[7],
                                             20);
    dev->calibration.c01 = (int16_t)(((uint16_t)buf[8] << 8) | buf[9]);
    dev->calibration.c11 = (int16_t)(((uint16_t)buf[10] << 8) | buf[11]);
    dev->calibration.c20 = (int16_t)(((uint16_t)buf[12] << 8) | buf[13]);
    dev->calibration.c21 = (int16_t)(((uint16_t)buf[14] << 8) | buf[15]);
    dev->calibration.c30 = (int16_t)(((uint16_t)buf[16] << 8) | buf[17]);

    return ESP_OK;
}

static esp_err_t spl06_apply_config(spl06_t *dev, const spl06_config_t *config)
{
    uint8_t prs_cfg = ((uint8_t)config->pressure_rate << 4) | (uint8_t)config->pressure_oversampling;
    uint8_t tmp_cfg = ((uint8_t)config->temperature_rate << 4) | (uint8_t)config->temperature_oversampling;
    uint8_t cfg_reg = 0;

    if (config->temperature_sensor == SPL06_TEMPERATURE_SENSOR_EXTERNAL) {
        tmp_cfg |= BIT(7);
    }

    if (config->pressure_oversampling >= SPL06_OVERSAMPLING_16) {
        cfg_reg |= SPL06_CFG_P_SHIFT;
    }

    if (config->temperature_oversampling >= SPL06_OVERSAMPLING_16) {
        cfg_reg |= SPL06_CFG_T_SHIFT;
    }

    ESP_RETURN_ON_ERROR(spl06_write_reg(dev, SPL06_REG_PRS_CFG, prs_cfg), "spl06",
                        "failed to write pressure config");
    ESP_RETURN_ON_ERROR(spl06_write_reg(dev, SPL06_REG_TMP_CFG, tmp_cfg), "spl06",
                        "failed to write temperature config");
    ESP_RETURN_ON_ERROR(spl06_write_reg(dev, SPL06_REG_CFG_REG, cfg_reg), "spl06",
                        "failed to write shift config");
    ESP_RETURN_ON_ERROR(spl06_write_reg(dev, SPL06_REG_MEAS_CFG, (uint8_t)config->mode), "spl06",
                        "failed to write measurement mode");

    dev->mode = config->mode;
    dev->pressure_scale_factor = spl06_scale_factor(config->pressure_oversampling);
    dev->temperature_scale_factor = spl06_scale_factor(config->temperature_oversampling);

    return ESP_OK;
}

static void spl06_compensate(spl06_t *dev, int32_t raw_temperature, int32_t raw_pressure,
                             float *temperature_c, float *pressure_pa)
{
    const float t_sc = (float)raw_temperature / dev->temperature_scale_factor;
    const float p_sc = (float)raw_pressure / dev->pressure_scale_factor;
    const spl06_calibration_t *cal = &dev->calibration;

    if (temperature_c) {
        *temperature_c = ((float)cal->c0 * 0.5f) + ((float)cal->c1 * t_sc);
    }

    if (pressure_pa) {
        *pressure_pa = (float)cal->c00 +
                       p_sc * ((float)cal->c10 + p_sc * ((float)cal->c20 + p_sc * (float)cal->c30)) +
                       t_sc * (float)cal->c01 +
                       t_sc * p_sc * ((float)cal->c11 + p_sc * (float)cal->c21);
    }
}

void spl06_init_default_config(spl06_config_t *config)
{
    if (!config) {
        return;
    }

    *config = (spl06_config_t) {
        .i2c_port = I2C_NUM_0,
        .i2c_address = SPL06_I2C_ADDRESS_LOW,
        .timeout_ms = 100,
        .mode = SPL06_MODE_CONTINUOUS_PRESSURE_TEMPERATURE,
        .pressure_rate = SPL06_MEAS_RATE_4_HZ,
        .pressure_oversampling = SPL06_OVERSAMPLING_16,
        .temperature_rate = SPL06_MEAS_RATE_4_HZ,
        .temperature_oversampling = SPL06_OVERSAMPLING_2,
        .temperature_sensor = SPL06_TEMPERATURE_SENSOR_EXTERNAL,
    };
}

esp_err_t spl06_init(spl06_t *dev, const spl06_config_t *config)
{
    SPL06_CHECK_ARG(dev);
    SPL06_CHECK_ARG(config);

    memset(dev, 0, sizeof(*dev));
    dev->i2c_port = config->i2c_port;
    dev->i2c_address = config->i2c_address;
    dev->timeout_ms = config->timeout_ms;

    ESP_RETURN_ON_ERROR(spl06_reset(dev), "spl06", "failed to reset sensor");
    ESP_RETURN_ON_ERROR(spl06_wait_ready(dev), "spl06", "sensor is not ready");
    ESP_RETURN_ON_ERROR(spl06_read_chip_id(dev, &dev->chip_id), "spl06", "failed to read chip id");

    if (dev->chip_id != SPL06_CHIP_ID) {
        return ESP_ERR_NOT_FOUND;
    }

    ESP_RETURN_ON_ERROR(spl06_read_calibration(dev), "spl06", "failed to load calibration");
    ESP_RETURN_ON_ERROR(spl06_apply_config(dev, config), "spl06", "failed to configure sensor");

    dev->initialized = true;

    return ESP_OK;
}

esp_err_t spl06_reset(spl06_t *dev)
{
    SPL06_CHECK_ARG(dev);

    ESP_RETURN_ON_ERROR(spl06_write_reg(dev, SPL06_REG_RESET, SPL06_RESET_VALUE), "spl06",
                        "failed to write reset");
    vTaskDelay(pdMS_TO_TICKS(SPL06_READY_DELAY_MS));

    return ESP_OK;
}

esp_err_t spl06_is_ready(spl06_t *dev, bool *ready)
{
    uint8_t meas_cfg = 0;

    SPL06_CHECK_ARG(dev);
    SPL06_CHECK_ARG(ready);

    ESP_RETURN_ON_ERROR(spl06_read_reg(dev, SPL06_REG_MEAS_CFG, &meas_cfg, 1), "spl06",
                        "failed to read status");

    *ready = ((meas_cfg & (SPL06_MEAS_CFG_COEF_RDY | SPL06_MEAS_CFG_SENSOR_RDY)) ==
              (SPL06_MEAS_CFG_COEF_RDY | SPL06_MEAS_CFG_SENSOR_RDY));

    return ESP_OK;
}

esp_err_t spl06_read_raw(spl06_t *dev, int32_t *raw_temperature, int32_t *raw_pressure)
{
    uint8_t data[6] = { 0 };

    SPL06_CHECK_ARG(dev);
    SPL06_CHECK_ARG(dev->initialized);
    SPL06_CHECK_ARG(raw_temperature || raw_pressure);

    ESP_RETURN_ON_ERROR(spl06_read_reg(dev, SPL06_REG_PRESSURE_MSB, data, sizeof(data)), "spl06",
                        "failed to read raw data");

    if (raw_pressure) {
        *raw_pressure = spl06_sign_extend(((uint32_t)data[0] << 16) | ((uint32_t)data[1] << 8) | data[2], 24);
    }

    if (raw_temperature) {
        *raw_temperature =
            spl06_sign_extend(((uint32_t)data[3] << 16) | ((uint32_t)data[4] << 8) | data[5], 24);
    }

    return ESP_OK;
}

esp_err_t spl06_read_temperature(spl06_t *dev, float *temperature_c)
{
    return spl06_read_temperature_pressure(dev, temperature_c, NULL);
}

esp_err_t spl06_read_pressure(spl06_t *dev, float *pressure_pa)
{
    return spl06_read_temperature_pressure(dev, NULL, pressure_pa);
}

esp_err_t spl06_read_temperature_pressure(spl06_t *dev, float *temperature_c, float *pressure_pa)
{
    int32_t raw_temperature = 0;
    int32_t raw_pressure = 0;

    SPL06_CHECK_ARG(dev);
    SPL06_CHECK_ARG(dev->initialized);
    SPL06_CHECK_ARG(temperature_c || pressure_pa);

    ESP_RETURN_ON_ERROR(spl06_read_raw(dev, &raw_temperature, &raw_pressure), "spl06",
                        "failed to read sensor sample");

    spl06_compensate(dev, raw_temperature, raw_pressure, temperature_c, pressure_pa);

    return ESP_OK;
}

float spl06_calculate_altitude(float pressure_pa, float sea_level_pressure_pa)
{
    if (pressure_pa <= 0.0f || sea_level_pressure_pa <= 0.0f) {
        return NAN;
    }

    return 44330.0f * (1.0f - powf(pressure_pa / sea_level_pressure_pa, 0.19029495f));
}
