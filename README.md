# armor

Armor module for robot accessories.

> 暂停开发一段时间，目前已经实现了灯光效果，击打检测待后续有空闲时间了再做。结构设计也有待优化，目前的结构还不适合做CNC。

## 仓库结构

- `firmware/esp32`：ESP32-C3 SuperMini 的 PlatformIO 固件工程。
- `armor.code-workspace`：VS Code 多根工作区入口，包含仓库根目录与 ESP32 固件工程。

## 开发环境

在 VS Code 中打开 `armor.code-workspace`，资源管理器会显示 `repository` 和
`esp32-firmware` 两个工作区。固件使用 **ESP32-C3 SuperMini**（ESP32-C3FN4，4 MB
Flash）；PlatformIO 采用兼容的 `esp32-c3-devkitm-1` 开发板定义和 Arduino 框架。

板载蓝色 LED 接在 GPIO8；`firmware/esp32/src/main.cpp` 是一个串口输出和 LED 闪烁的硬件自检程序。

1. 在 VS Code 安装 **PlatformIO IDE** 扩展，并打开 `armor.code-workspace`。
2. 用可传输数据的 USB 线连接开发板；首次上传时如无法自动进入下载模式，按住 **BOOT**，点击上传，出现连接提示后松开。
3. 在 PlatformIO 的环境 `esp32-c3-supermini` 中执行 Upload，再打开串口监视器（115200 baud）。

配置启用了 ESP32-C3 原生 USB CDC，因此程序日志通过开发板的 USB 串口输出，无需额外的 USB-TTL 转换器。
