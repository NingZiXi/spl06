# SPL06 ESP-IDF Component

SPL06 pressure and temperature sensor driver component for ESP-IDF 5.5.1.

This component is designed around two goals:

- Keep the compensation algorithm correct and self-contained.
- Keep the driver low-coupled with the rest of the application.

The application owns the I2C bus. The component only stores the `i2c_port_t`, device address, timeout, calibration data, and runtime state required to talk to one SPL06 sensor.

For the Chinese version, see `README.md`.

## Features

- Supports SPL06 over I2C
- Supports both `0x76` and `0x77` addresses
- Reads and parses the 18-byte factory calibration block
- Applies correct sign extension for 12-bit, 20-bit, and 24-bit values
- Provides compensated temperature and pressure output
- Provides altitude estimation from pressure
- Exposes a clean device handle and configuration API

## Directory Layout

```text
components/spl06/
|-- CMakeLists.txt
|-- README.md
|-- README_EN.md
|-- include/
|   `-- spl06.h
`-- spl06.c
```

## Design Notes

This component intentionally does not create or destroy the I2C driver internally.

That means:

- If your board has multiple I2C buses, the application can choose any `i2c_port_t`
- If multiple sensors share the same bus, bus ownership remains in the application layer
- The SPL06 component stays focused on SPL06 register access and compensation logic

This is the same general direction often used by well-structured ESP-IDF sensor drivers: the bus is configured by the app, and the device driver is only responsible for the device.

## Public API

The public API is declared in `include/spl06.h`.

### Main Types

- `spl06_t`: device handle and runtime state
- `spl06_config_t`: initialization/configuration parameters
- `spl06_calibration_t`: parsed factory calibration coefficients
- `spl06_measurement_rate_t`: output data rate setting
- `spl06_oversampling_t`: oversampling setting
- `spl06_temperature_sensor_t`: internal or external temperature source
- `spl06_mode_t`: standby, command mode, or continuous mode

### Main Functions

- `spl06_init_default_config()`: fill a config struct with safe defaults
- `spl06_init()`: reset device, wait until ready, read calibration, apply configuration
- `spl06_reset()`: issue sensor soft reset
- `spl06_is_ready()`: read the sensor ready flags
- `spl06_read_raw()`: read raw 24-bit temperature/pressure values
- `spl06_read_temperature()`: read compensated temperature in Celsius
- `spl06_read_pressure()`: read compensated pressure in Pascal
- `spl06_read_temperature_pressure()`: read both values in one call
- `spl06_calculate_altitude()`: estimate altitude using pressure and sea-level pressure

## Default Configuration

`spl06_init_default_config()` currently sets:

- I2C port: `I2C_NUM_0`
- I2C address: `0x76`
- Timeout: `100 ms`
- Mode: continuous pressure + temperature
- Pressure rate: `4 Hz`
- Pressure oversampling: `16x`
- Temperature rate: `4 Hz`
- Temperature oversampling: `2x`
- Temperature source: external sensor

These defaults are intended to be a reasonable starting point, not a universal optimum.

## Initialization Flow

`spl06_init()` performs the following steps:

1. Clear and initialize the device handle
2. Issue a soft reset
3. Poll `MEAS_CFG` until both sensor-ready and coefficient-ready flags are set
4. Read chip ID and verify it matches `SPL06_CHIP_ID`
5. Read the 18-byte calibration data block from the sensor
6. Parse all compensation coefficients
7. Apply register configuration for rate, oversampling, shift bits, and mode

## Compensation Logic

The driver implements the standard SPL06 compensation chain:

- Read raw temperature and pressure
- Apply the SPL06 scaling factor that depends on oversampling
- Use the factory coefficients `c0`, `c1`, `c00`, `c10`, `c01`, `c11`, `c20`, `c21`, and `c30`
- Compute compensated temperature and pressure using the floating-point polynomial

The implementation includes explicit sign extension for:

- `c0`, `c1`: 12-bit signed values
- `c00`, `c10`: 20-bit signed values
- raw temperature and raw pressure: 24-bit signed values

This avoids a very common SPL06 bug source: incorrect sign handling during coefficient parsing.

## Application Responsibility

Before calling `spl06_init()`, the application must already have:

- selected the I2C pins
- configured the I2C master parameters
- installed the ESP-IDF I2C driver

This component uses the classic ESP-IDF I2C API from `driver/i2c.h`.

## Minimal Usage Example

```c
#include "driver/i2c.h"
#include "esp_log.h"
#include "spl06.h"

#define EXAMPLE_I2C_PORT I2C_NUM_0

static void app_init_i2c(void)
{
    const i2c_config_t i2c_cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = GPIO_NUM_8,
        .scl_io_num = GPIO_NUM_9,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };

    ESP_ERROR_CHECK(i2c_param_config(EXAMPLE_I2C_PORT, &i2c_cfg));
    ESP_ERROR_CHECK(i2c_driver_install(EXAMPLE_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0));
}

void app_main(void)
{
    spl06_t dev;
    spl06_config_t cfg;
    float temperature_c;
    float pressure_pa;

    app_init_i2c();

    spl06_init_default_config(&cfg);
    cfg.i2c_port = EXAMPLE_I2C_PORT;
    cfg.i2c_address = SPL06_I2C_ADDRESS_LOW;

    ESP_ERROR_CHECK(spl06_init(&dev, &cfg));

    while (1) {
        if (spl06_read_temperature_pressure(&dev, &temperature_c, &pressure_pa) == ESP_OK) {
            ESP_LOGI("spl06", "T=%.2f C, P=%.2f Pa", temperature_c, pressure_pa);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

## Altitude Calculation

Use:

```c
float altitude_m = spl06_calculate_altitude(pressure_pa, SPL06_SEA_LEVEL_PA_DEFAULT);
```

`SPL06_SEA_LEVEL_PA_DEFAULT` is the standard sea-level pressure value `101325 Pa`.

For better altitude accuracy, use the local sea-level pressure for your area instead of the standard atmosphere constant.

## Address Selection

The component defines:

- `SPL06_I2C_ADDRESS_LOW` = `0x76`
- `SPL06_I2C_ADDRESS_HIGH` = `0x77`

Choose the address that matches your board wiring.

## Notes For ESP-IDF 5.5.1

- This component is written for ESP-IDF 5.5.1
- It currently uses the classic master I2C API from `driver/i2c.h`
- The project `main.c` already includes a simple polling demo that can be used as a starting point

## Limitations

- No interrupt mode
- No FIFO support
- No forced-conversion helper yet
- No SPI support yet
- No automated unit tests in this demo project yet

## Suggested Next Improvements

- Add a dedicated forced-measurement API
- Add an example under `examples/`
- Add Kconfig options for default address and timeout
- Add a helper for pressure unit conversion, such as Pa to hPa
- Add CI build validation when the ESP-IDF environment is available
