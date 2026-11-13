# 软件与固件

## 当前资料状态

仓库现已包含 `firmware/esp32_s3_single_channel/emg_single_channel.ino`：ESP32-S3 单通道采集原型，GPIO3 ADC 输入，目标采样率 1 kHz，依次执行 50 Hz 陷波、20 Hz 高通、250 Hz 低通，计算 100 ms RMS，每 20 ms 更新，并用 2 秒静息基线、阈值回差与连续窗口计数判断肌肉活动。串口输出波特率为 115200。

另新增 `firmware/esp32_s3_8ch/esp32_s3_8ch.ino` 和 `firmware/esp8266_wifi_udp/esp8266_wifi_udp.ino`，分别作为 ADS131M08 八通道采集原型及 ESP8266EX UART→UDP 转发原型。采集端输出一行 CSV：毫秒时间戳、8 路 RMS、8 路状态；Wi-Fi 端忽略 `#` 开头的配置/标题行，并逐行转成 UDP 数据包。UDP 地址、端口、Wi-Fi 凭据需按实际网络修改。

单通道程序顶部注释写“GPIO4”，实际常量 `EMG_PIN` 为 GPIO3；接线前应按实际硬件确认并统一。八通道原型使用 ADS131M08 SPI，候选映射按 ADC 工程接口网上的 GPIO 标签设置为 SCLK=10、DIN=4、DOUT=5、CS=6、DRDY=7、SYNC/RESET=3。此映射需在最新主板原理图和实际 PCB 走线中复核；ADC 输出字长/采样率寄存器配置和 SPI 帧行为也需上板验证。八通道与 Wi-Fi 代码尚未在本环境编译或实机测试，视为原型而非已验证固件。

## 待加入的源码

建议后续按以下目录组织其余源码，并按项目使用的工具链调整：

```text
firmware/
├── esp32_s3_single_channel/  已提供的 Arduino 单通道原型
├── esp32_s3/                 多通道主控与 ADS131M08 程序（待提供）
└── wifi_esp8266/             UART 接收、Wi-Fi 与 UDP 转发程序（待提供）
examples/
└── data_receiver/  上位机 UDP 接收/可视化程序
```

## 发布时应记录

- ESP-IDF/Arduino/其他框架名称与准确版本、依赖清单及构建命令。
- ADC 初始化配置、采样率、SPI 连接和通道数据排列。
- Wi-Fi 模块烧录工具/固件版本、UART 参数和配置步骤。
- 固件烧录地址/分区表、设备启动日志、已验证硬件修订版。
- 接收端运行环境、UDP 帧解析说明和公开测试样例。

以上信息当前未知，不能仅凭 EDA 原理图推断软件已经实现。
