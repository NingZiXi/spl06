# SPL06 ESP-IDF 组件

适用于 ESP-IDF 5.5.1 的 SPL06 气压/温度传感器驱动组件。

本组件的设计目标有两个：

- 保证补偿算法正确、完整、可复用
- 保持驱动和应用层低耦合

I2C 总线由应用层负责初始化和管理。组件内部只保存与单个 SPL06 设备通信所需的 `i2c_port_t`、设备地址、超时参数、校准系数和运行状态。

英文版文档见 `README_EN.md`。

## 特性

- 支持 SPL06 的 I2C 通信
- 同时支持 `0x76` 和 `0x77` 两个地址
- 读取并解析 18 字节原厂校准参数
- 正确处理 12 位、20 位、24 位有符号数据的符号扩展
- 提供补偿后的温度和气压输出
- 提供基于气压的海拔估算接口
- 提供清晰的设备句柄和配置接口

## 目录结构

```text
components/spl06/
|-- CMakeLists.txt
|-- README.md
|-- README_EN.md
|-- include/
|   `-- spl06.h
`-- spl06.c
```

## 设计说明

本组件刻意不在内部创建或销毁 I2C 驱动。

这样做的好处是：

- 如果你的板子有多条 I2C 总线，应用层可以自由选择任意 `i2c_port_t`
- 如果多个传感器共用一条总线，总线资源仍由应用层统一管理
- SPL06 组件本身只专注于 SPL06 的寄存器访问与补偿计算

这也是比较规范的 ESP-IDF 传感器组件常见做法：总线归应用层管理，设备驱动只处理设备本身。

## 公共 API

公共接口声明位于 `include/spl06.h`。

### 主要类型

- `spl06_t`：设备句柄和运行时状态
- `spl06_config_t`：初始化与配置参数
- `spl06_calibration_t`：解析后的原厂校准系数
- `spl06_measurement_rate_t`：测量速率配置
- `spl06_oversampling_t`：过采样配置
- `spl06_temperature_sensor_t`：温度源选择，内部或外部
- `spl06_mode_t`：待机、命令模式或连续测量模式

### 主要函数

- `spl06_init_default_config()`：填充一份默认配置
- `spl06_init()`：复位设备、等待就绪、读取校准参数并应用配置
- `spl06_reset()`：执行软复位
- `spl06_is_ready()`：读取传感器 ready 标志
- `spl06_read_raw()`：读取原始 24 位温度和气压值
- `spl06_read_temperature()`：读取补偿后的温度，单位摄氏度
- `spl06_read_pressure()`：读取补偿后的气压，单位帕斯卡
- `spl06_read_temperature_pressure()`：一次性读取温度和气压
- `spl06_calculate_altitude()`：根据气压和海平面气压估算海拔

## 默认配置

`spl06_init_default_config()` 当前设置如下：

- I2C 端口：`I2C_NUM_0`
- I2C 地址：`0x76`
- 通信超时：`100 ms`
- 工作模式：连续测量温度和气压
- 气压测量速率：`4 Hz`
- 气压过采样：`16x`
- 温度测量速率：`4 Hz`
- 温度过采样：`2x`
- 温度源：外部温度传感器

这些参数是一个通用起点，不一定是所有场景下的最优值。

## 初始化流程

`spl06_init()` 会按以下顺序执行：

1. 清空并初始化设备句柄
2. 发送软复位命令
3. 轮询 `MEAS_CFG`，直到传感器 ready 和系数 ready 标志都置位
4. 读取芯片 ID，并校验是否等于 `SPL06_CHIP_ID`
5. 读取传感器内部 18 字节校准数据块
6. 解析全部补偿系数
7. 写入测量速率、过采样、shift 位和工作模式配置

## 补偿逻辑

驱动内部实现了 SPL06 标准补偿流程：

- 读取原始温度和原始气压
- 根据当前过采样配置应用 SPL06 对应的缩放因子
- 使用原厂系数 `c0`、`c1`、`c00`、`c10`、`c01`、`c11`、`c20`、`c21`、`c30`
- 使用浮点多项式计算补偿后的温度和气压

实现中显式处理了以下有符号扩展：

- `c0`、`c1`：12 位有符号数
- `c00`、`c10`：20 位有符号数
- 原始温度和原始气压：24 位有符号数

这样可以避免 SPL06 驱动里最常见的一类错误：校准参数解析时的符号位处理错误。

## 应用层需要负责什么

在调用 `spl06_init()` 之前，应用层必须已经完成：

- 选择 I2C 引脚
- 配置 I2C 主机参数
- 安装 ESP-IDF 的 I2C 驱动

本组件当前使用的是 `driver/i2c.h` 提供的经典 ESP-IDF I2C 主机 API。

## 最小使用示例

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

## 海拔计算

使用方式如下：

```c
float altitude_m = spl06_calculate_altitude(pressure_pa, SPL06_SEA_LEVEL_PA_DEFAULT);
```

`SPL06_SEA_LEVEL_PA_DEFAULT` 对应标准海平面气压 `101325 Pa`。

如果你希望海拔估算更准确，建议使用你所在地区的当地海平面气压，而不是标准大气压常量。

## 地址选择

组件中定义了：

- `SPL06_I2C_ADDRESS_LOW` = `0x76`
- `SPL06_I2C_ADDRESS_HIGH` = `0x77`

请根据模块硬件接法选择对应地址。

## 关于 ESP-IDF 5.5.1

- 本组件按 ESP-IDF 5.5.1 编写
- 当前使用 `driver/i2c.h` 的经典 I2C 主机接口
- 工程里的 `main.c` 已带一个简单轮询示例，可直接作为接入起点

## 当前限制

- 暂未实现中断模式
- 暂未实现 FIFO 支持
- 暂未封装 forced measurement 接口
- 暂未支持 SPI
- 这个 demo 工程里暂未补自动化单元测试

## 后续建议

- 增加专门的 forced measurement API
- 增加 `examples/` 示例目录
- 增加 Kconfig 配置，例如默认地址和超时时间
- 增加压力单位换算辅助函数，例如 Pa 转 hPa
- 在具备 ESP-IDF 环境时增加 CI 编译校验
