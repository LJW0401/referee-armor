# Armor 上位机

这是 ESP32 装甲模块的 Python 串口上位机。它实现
[`docs/serial-protocol.md`](../docs/serial-protocol.md) 中的握手与状态读取协议。

## 安装

在仓库根目录执行：

```powershell
python -m venv .venv
.venv\Scripts\python -m pip install -e host
```

## 使用

先列出可选串口：

```powershell
.venv\Scripts\armor-host ports
```

选择 ESP32 所在端口并连接，例如：

```powershell
.venv\Scripts\armor-host connect --port COM5
```

命令只有在完整握手（序列号、随机数、CRC 和协议版本均匹配）后才报告连接成功，并立即输出一次状态。持续刷新状态：

```powershell
.venv\Scripts\armor-host connect --port COM5 --watch
```

烧录固件前应先退出该程序，避免占用 USB CDC 串口。
