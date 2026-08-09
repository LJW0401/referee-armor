# armor

Armor module for robot accessories.

## 开发环境

本项目已配置为 **ESP32-C3 SuperMini**（ESP32-C3FN4，4 MB Flash）。在
PlatformIO 中它使用兼容的 `esp32-c3-devkitm-1` 开发板定义，默认 Arduino 框架。

板载蓝色 LED 接在 GPIO8；`src/main.cpp` 是一个串口输出和 LED 闪烁的硬件自检程序。

1. 在 VS Code 安装 **PlatformIO IDE** 扩展，并打开本目录。
2. 用可传输数据的 USB 线连接开发板；首次上传时如无法自动进入下载模式，按住 **BOOT**，点击上传，出现连接提示后松开。
3. 在 PlatformIO 的环境 `esp32-c3-supermini` 中执行 Upload，再打开串口监视器（115200 baud）。

配置启用了 ESP32-C3 原生 USB CDC，因此程序日志通过开发板的 USB 串口输出，无需额外的 USB-TTL 转换器。
