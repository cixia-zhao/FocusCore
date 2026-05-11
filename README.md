# FocusCore

> ⚠️ **项目状态：开发中（未完成）**  
> 这是一个基于 ESP32-S3 的电子墨水屏知识管理设备原型，目前处于早期开发阶段。

## 项目简介

FocusCore 是一款专注于沉浸式阅读与知识管理的嵌入式设备，使用微雪 4.2 寸全反射黑白屏（400×300），旨在打造无干扰的"数字花园"阅读体验。

**核心理念**：将 Obsidian 笔记通过自动化渲染管线降维为单片机可直接显示的图像，实现离线、低功耗的知识回顾。

## 硬件平台

- **主控**：ESP32-S3（16MB Flash + 8MB PSRAM）
- **屏幕**：微雪 4.2 寸 RLCD 全反射屏（400×300，黑白显示）
- **存储**：Micro SD 卡（通过 SDMMC 1-line 模式挂载）
- **传感器**：SHTC3（温湿度）、PCF85063（RTC 时钟）
- **交互**：EC11 旋转编码器 + 物理按键

## 软件架构

### 开发环境
- **框架**：ESP-IDF v6.0
- **UI 引擎**：LVGL v9（1-bit 高对比度渲染）
- **蓝牙**：NimBLE（低功耗待办同步与授时）
- **IDE**：VS Code + ESP-IDF 插件

### 核心功能（已实现）

✅ **主页 Bento Box 布局**  
- 实时温度/电量/时钟显示
- 6 行待办事项（支持删除线）
- 动态日期/星期显示

✅ **Web Bluetooth 同步**  
- 长按 3 秒唤醒蓝牙广播（60 秒超时）
- 手机网页一键下发时间与待办
- 无需 Wi-Fi，极致省电

✅ **SD 卡文件浏览器**  
- 树形目录导航（基于 `index.json`）
- 中文文件名支持
- 语义切片渲染（按 Markdown H1/H2/H3 分割）

✅ **Digital Garden 阅读器**  
- 支持超长图滚动（最高 8000px）
- LaTeX 公式完美渲染
- 旋转编码器丝滑翻页

### 上位机工具链

**`md_to_bin_pipeline.py`**：Markdown → LVGL 裸像素流  
- 基于 Playwright 无头浏览器渲染
- MathJax 3 数学公式支持
- 自动生成树形 `index.json` 索引
- 输出 400px 定宽、1-bit 灰度 `.bin` 文件

**`gen_lvgl_fonts.py`**：中文字库裁剪工具  
- 基于 `lv_font_conv` 生成 1bpp 字体
- 支持 3500 常用汉字 + ASCII 全字符集

## 项目结构

```
FocusCore/
├── main/                    # 主程序
│   ├── main.cpp            # 核心逻辑与 UI 路由
│   ├── ui_home.c/h         # 主页 UI
│   ├── ui_menu.c/h         # 菜单系统
│   ├── ble_memo.c/h        # 蓝牙同步
│   ├── pcf85063.c/h        # RTC 驱动
│   ├── shtc3.c/h           # 温湿度传感器
│   ├── battery_monitor.c/h # 电池监控
│   ├── sd_card.c/h         # SD 卡驱动
│   └── cJSON.c/h           # JSON 解析器
├── components/              # 自定义组件
│   ├── app_bsp/
│   ├── port_bsp/
│   ├── ui_bsp/
│   └── user_app/
├── ui_assets/               # UI 图标资源
├── md_to_bin_pipeline.py   # Markdown 转换脚本
├── gen_lvgl_fonts.py       # 字体生成脚本
├── batch_img_to_lvgl_c.py  # 图标转换脚本
└── ds_memo_log.md          # 详细开发日志
```

## 快速开始

### 环境准备

```bash
# 1. 安装 ESP-IDF v6.0
# 参考：https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/get-started/

# 2. 安装 Python 依赖
pip install markdown pymdown-extensions playwright pillow pypinyin
playwright install chromium

# 3. 安装字体工具
npm install -g lv_font_conv
```

### 编译与烧录

```bash
# 激活 ESP-IDF 环境
. $HOME/esp/esp-idf/export.sh  # Linux/Mac
# 或
. C:\Espressif\frameworks\esp-idf-v6.0\export.ps1  # Windows

# 编译
idf.py build

# 烧录
idf.py flash monitor
```

### 生成字库

```bash
python gen_lvgl_fonts.py
```

### 转换笔记

```bash
python md_to_bin_pipeline.py "D:\path\to\your\notes"
# 输出到 /sdcard/_bin_output/
```

## Web Bluetooth 控制台

访问：[https://focuscorebleweb.netlify.app/](https://focuscorebleweb.netlify.app/)

**使用方法**：
1. 长按设备按键 3 秒（屏幕显示 `[BLE]`）
2. 点击网页"连接设备"
3. 自动同步时间与待办事项

## 已知问题与限制

- ⚠️ 字库仅包含 3500 常用汉字，生僻字显示为方框
- ⚠️ 待办区域物理限制：单行最多 11 字，最多 6 行
- ⚠️ RTC 需要 CR2032 备用电池，否则每次断电后时间重置
- ⚠️ SD 卡必须在开机前插入，暂不支持热插拔
- ⚠️ 树形导航最后一级显示 `"?"` 的 bug 待修复

## 开发日志

详细的开发过程、踩坑记录与架构演进请参考：
- **[ds_memo_log.md](./ds_memo_log.md)** - 完整开发日志（2026-04-20 至今）
- **[FocusCore_DevLog.md](D:\code\digital-garden\content\电子\FocusCore_DevLog（开发日志）.md)** - 外部详细文档

## 技术亮点

- 🎯 **语义切片渲染**：按 Markdown 大纲结构智能分割，避免公式/段落被截断
- 🔋 **极致省电**：默认关闭 Wi-Fi，仅在需要时通过 BLE 同步
- 🧠 **PSRAM 强制分配**：大图加载直接使用 8MB 外挂内存，避免 OOM
- 🎨 **1-bit 完美渲染**：无灰度抗锯齿，纯黑白高对比度显示
- 🔄 **增量转换**：基于 mtime 缓存，仅重新渲染修改过的笔记

## 资源审计

**当前预估占用**：
- RAM：约 100-120 KB（含 LVGL + FreeRTOS 任务栈）
- Flash：约 2.5-3 MB（含 LVGL v9 + NimBLE + 字库）
- PSRAM：动态分配（最大单图约 3 MB）

## 致谢

- 微雪电子提供的硬件平台与示例代码
- LVGL 社区的 v9 文档与支持
- Cursor AI 在开发过程中的深度协助

## 许可证

MIT License

---

**注意**：本项目仍在积极开发中，代码结构与功能可能随时变化。不建议用于生产环境。
