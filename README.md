<p align="center">
  <img src="./image.png" alt="SPL06 Poster" />
</p>

<h1 align="center">🌡️ SPL06 Driver</h1>

<p align="center">
一个适用于 ESP-IDF 的 SPL06 气压/温度传感器组件<br/>
支持新 I2C 驱动、标准补偿流程和低耦合接入方式
</p>

<p align="center">
<a href="./README_EN.md">English</a>
· 简体中文
· <a href="https://github.com/NingZiXi/spl06/releases">更新日志</a>
· <a href="https://github.com/NingZiXi/spl06/issues">反馈问题</a>
</p>

<p align="center">
  <a href="./LICENSE">
    <img alt="License" src="https://img.shields.io/badge/License-MIT-blue.svg" />
  </a>
  <a href="https://docs.espressif.com/projects/esp-idf/">
    <img alt="ESP-IDF" src="https://img.shields.io/badge/ESP--IDF-v5.5.1+-orange.svg" />
  </a>
  <a href="https://www.espressif.com/">
    <img alt="Platform" src="https://img.shields.io/badge/Platform-ESP32-green.svg" />
  </a>
  <a href="./idf_component.yml">
    <img alt="Version" src="https://img.shields.io/badge/Version-v0.1.0-brightgreen.svg" />
  </a>
  <a href="https://github.com/NingZiXi/spl06/stargazers">
    <img alt="GitHub Stars" src="https://img.shields.io/github/stars/NingZiXi/spl06.svg?style=social&label=Stars" />
  </a>
</p>

---

## 📌 概述

本组件适用于 `ESP-IDF 5.5.1+`，基于 `driver/i2c_master.h` 新 I2C 主机接口实现，支持 `0x76` / `0x77` 两个地址，提供 SPL06 标准校准参数解析、温度与气压补偿计算以及海拔估算接口。组件采用应用层管理 I2C bus、驱动层管理设备句柄的方式，便于在正式项目中复用和集成。

## 🛠️ 快速开始

### 1. 获取组件

要将组件添加到项目中请在 IDF 终端执行下方命令：

```bash
idf.py add-dependency "ningzixi/spl06^0.1.0"
```

或者直接克隆到项目 `components` 目录：

```bash
git clone https://github.com/NingZiXi/spl06.git
```

### 2. 基本用法

```c
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "spl06.h"

#define EXAMPLE_I2C_SDA  GPIO_NUM_4
#define EXAMPLE_I2C_SCL  GPIO_NUM_5
#define EXAMPLE_I2C_PORT I2C_NUM_0
#define EXAMPLE_I2C_FREQ 100000

static i2c_master_bus_handle_t bus_handle;
static spl06_t spl06;
static const char *TAG = "spl06";

static void init_i2c(void)
{
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = EXAMPLE_I2C_PORT,
        .sda_io_num = EXAMPLE_I2C_SDA,
        .scl_io_num = EXAMPLE_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus_handle));
}

void app_main(void)
{
    spl06_config_t cfg;
    float temperature_c = 0.0f;
    float pressure_pa = 0.0f;
    float altitude_m = 0.0f;

    init_i2c();

    spl06_init_default_config(&cfg);
    cfg.i2c_address = SPL06_I2C_ADDRESS_HIGH;
    cfg.scl_speed_hz = EXAMPLE_I2C_FREQ;

    ESP_ERROR_CHECK(spl06_init(&spl06, bus_handle, &cfg));

    while (true) {
        if (spl06_read_temperature_pressure(&spl06, &temperature_c, &pressure_pa) == ESP_OK) {
            altitude_m = spl06_calculate_altitude(pressure_pa, SPL06_SEA_LEVEL_PA_DEFAULT);
            ESP_LOGI(TAG, "Temperature: %.2f C, Pressure: %.2f Pa, Altitude: %.2f m",
                     temperature_c, pressure_pa, altitude_m);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

### 3. 示例输出

```text
I (1234) spl06: Temperature: 31.76 C, Pressure: 100814.94 Pa, Altitude: 42.55 m
```

更多接口请查看 `include/spl06.h`。

## 📚 主要接口

- `spl06_init_default_config()`：填充默认配置
- `spl06_init()`：创建设备并完成初始化
- `spl06_deinit()`：释放设备句柄
- `spl06_read_temperature()`：读取温度，单位 `C`
- `spl06_read_pressure()`：读取气压，单位 `Pa`
- `spl06_read_temperature_pressure()`：同时读取温度和气压
- `spl06_calculate_altitude()`：按海平面气压估算海拔

## ⚙️ 默认配置

- 地址：`0x76`
- 超时：`100 ms`
- 模式：连续测量温度和气压
- 气压：`4 Hz`，`16x`
- 温度：`4 Hz`，`2x`
- 温度源：外部温度传感器

## 📝 说明

- 组件默认校验 `chip id = 0x10`
- I2C bus 由应用层创建并管理
- 若模块地址不是 `0x76`，请切换到 `SPL06_I2C_ADDRESS_HIGH`

## 📄 许可证

本项目采用 MIT 许可证，详情请参阅 [LICENSE](./LICENSE)。

<p align="center">
感谢您使用 SPL06 Driver！🌡️<br/>
如果觉得项目对您有帮助，请给个 ⭐ Star 支持一下！
</p>
