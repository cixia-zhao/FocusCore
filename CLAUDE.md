# FocusCore Agentic Protocol (ESP32-S3)

## 1. 物理底座限制 (CRITICAL)
- **环境**：ESP-IDF v6.0 + C/C++。
- **存储**：16MB Flash + 8MB PSRAM（极度受限）。禁止使用庞大的 C++ 标准库（STL）特性，优先使用 FreeRTOS 原生队列/任务和 C 标准库。
- **显示**：LVGL v9，微雪 4.2 寸全反射黑白屏。必须严格遵循 `LV_COLOR_FORMAT_I1` 或 `L8` 高对比度格式，杜绝任何引起灰色抗锯齿的数学缩放。宽度死锁 400px。

## 2. 代码生成铁律
- **网络与并发**：严禁在中断 (ISR) 或网络/蓝牙回调 (Callback) 中直接操作 LVGL UI 或延时等待。必须通过 `Queue` 或 `EventGroup` 将数据抛给 UI 独立任务异步处理。
- **内存安全**：动态内存 (`malloc`/`heap_caps_malloc`) 必须有明确的释放生命周期。LVGL 图像对象释放必须遵循“先解绑对象，再 free 裸数据”的顺序。

## 3. 强制审计输出
每次修改代码后，回复的末尾必须严格附带以下格式的报告：
📊 嵌入式资源审计预估
- 预估增加 RAM 占用：[xx KB] 
- 预估增加 Flash 占用：[xx KB]
- 架构师警告：[无 / 标红警告具体的风险]