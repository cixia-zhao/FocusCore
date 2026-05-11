# 2026年04月25日上午


## 1. 📁 核心文件变更

- `batch_img_to_lvgl_c.py`（重构）
  - 实现 `icons_png` 批量转 `ui_assets`，输出 LVGL v9 `LV_COLOR_FORMAT_I1`。
  - 增加 8 Bytes 调色板头（白/黑 ARGB8888），修复 I1 显示问题。
  - 增加中文文件名处理（拼音/映射），统一英文别名（`note/read/photo/word/music`）。
  - 增加双轨预渲染：每张图生成 `64x64` 普通版 + `96x96` `*_large` 版（Nearest Neighbor）。
- `main/ui_menu.c`（重构）
  - 从“物理滚动长列表”重构为“固定 3 项虚拟窗口走马灯”。
  - 再升级为“双轨指针轮换”：上/下用 normal 图，中间用 large 图。
  - 中间选中框视觉强化（边框加粗，尺寸适配大图）。
- `main/ui_home.c`（新建）
  - 新增 400x300 Bento Box 首页骨架（状态栏 + 4 卡片）。
  - 集成主页文案（温度、日期、MEMO）和中文字体应用点。
  - 预留并接入页面路由：Home 可 `lv_screen_load()` 跳转 Menu。
- `main/ui_home.h`（新建）
  - 首页创建、路由绑定、路由触发接口声明。
- `main/ui_font_custom.c`（新建，自动生成）
  - 使用 `lv_font_conv` 生成 1bpp 裁剪中文字库（含你补充的一二三四五六日月年待办等字符）。
- `main/main.cpp`（改动）
  - 启动流程改为 Home 首屏 + Menu 目标屏。
  - 编码器/按键事件增加 Home→Menu / Menu→Home 路由分流。
- `main/CMakeLists.txt`（改动）
  - 接入 `ui_home.c`、`ui_font_custom.c`、以及 10 个图标资源（5 normal + 5 large）。

---

## 2. 🤖 高价值 Prompt 记录

今天最“高命中率”的提示词片段（非常适合沉淀为你以后和 AI 协作模板）：

- **资源转换规范明确型**
  - “生成的 C 数组必须严格符合 LVGL v9 的 `LV_COLOR_FORMAT_I1` 规范；黑像素=1，白/透明=0；每行字节数和整图大小精确约束。”
- **架构重构明确型**
  - “从长列表物理滚动改为固定 3 项无限循环虚拟窗口，保留编码器触发，使用 `lv_image_set_src()` 动态轮换 top/mid/bottom。”
- **双轨性能策略型**
  - “同一张图生成 normal + large 双轨资源，middle 绑定 large、上下绑定 normal，以预渲染换运行时性能。”

这三类 Prompt 的共同点：**约束清晰（格式/尺寸/API/状态机）、目标可验证（字节数/对象数量/路由行为）**。

---

## 3. ⚙️ 编译与依赖状态

- `main/CMakeLists.txt` 当前新增并已接入：
  - `ui_home.c`
  - `ui_font_custom.c`
  - `../ui_assets/ui_icon_note.c`
  - `../ui_assets/ui_icon_read.c`
  - `../ui_assets/ui_icon_photo.c`
  - `../ui_assets/ui_icon_word.c`
  - `../ui_assets/ui_icon_music.c`
  - `../ui_assets/ui_icon_note_large.c`
  - `../ui_assets/ui_icon_read_large.c`
  - `../ui_assets/ui_icon_photo_large.c`
  - `../ui_assets/ui_icon_word_large.c`
  - `../ui_assets/ui_icon_music_large.c`
- 字体与宏定义状态（项目是 ESP-IDF managed LVGL，走 `sdkconfig` 而非本地 `lv_conf.h`）：
  - `CONFIG_LV_FONT_MONTSERRAT_20=y`
  - `CONFIG_LV_FONT_MONTSERRAT_24=y`
  - `CONFIG_LV_FONT_MONTSERRAT_48=y`
  - `CONFIG_LV_FONT_CUSTOM_DECLARE="LV_FONT_DECLARE(ui_font_custom);"`
- 工具链状态：
  - 已安装 `pypinyin`
  - 已安装 `lv_font_conv`
  - 已生成 `main/ui_font_custom.c`

---

## 4. ⚠️ 悬而未决的代码债（明日待接入）

以下为当前“写死 Mock 数据/测试配置”，建议明天接真实数据源：

- `main/ui_home.c` `ui_home_create()`
  - `温度: 24.5°C`（固定温度）
  - `[Wi-Fi] [BAT] 97%`（固定电量/图标占位）
  - `21:51`（固定时钟）
  - `4月25日`、`2026年`（固定日期）
  - `[Avatar]`（头像占位）
  - `21:48 洗澡 / 22:10 整理桌面 / 23:00 复盘`（固定待办文本）
- `main/main.cpp` `WifiStatusTimerCb()`
  - `start_test_download("http://192.168.40.192:8080/test.bin")`（写死测试 URL）
  - `WiFi: Connecting/Connected/Error`（英文固定状态文案，可后续本地化+状态机抽象）
- `main/ui_menu.c`
  - 图标顺序与数据源映射目前写死在数组，后续可改为配置表驱动（便于扩展菜单项）。
- `batch_img_to_lvgl_c.py`
  - 英文别名映射字典（`ENGLISH_ALIAS_MAP`）是手工维护，后续可抽离成外部配置文件。

如果你愿意，我明天可以先从“**Home 页时间/日期和电量接入实时数据**”开始，优先把可视数据从 Mock 改成真实输入。

# 2026年04月25日下午

## 1. 📁 核心文件变更

- `main/ui_home.c`
  - 完成 Home Bento 布局微调：上半区时钟/日期卡片去边框，日期区三段文案按 `TOP_MID / CENTER / BOTTOM_MID` 重排。
  - 日期中间文本（`4月25日`）显式切换到 `ui_font_date_large`。
  - 下半区完成左右交换与 60/40 宽度分配：左侧为“待办”大区，右侧为 `Avatar`。
  - 修复“待办”方块字问题：为标题 `todo_title` 显式绑定 `ui_font_custom`。
- `main/ui_menu.c`
  - 采用固定 3 槽位虚拟窗口（top/mid/bottom）+ 无限索引轮换。
  - 双轨图标策略已落地：中间使用 `*_large`，上下使用 normal。
- `main/main.cpp`
  - 启动路径为 Home 首屏，Menu 作为目标页。
  - 编码器/返回键路由分流：Home 可进 Menu，非 Home 可回 Home。
  - 保留 Wi-Fi 状态与下载显示逻辑（当前仍含测试路径与硬编码内容）。
- `main/CMakeLists.txt`
  - `SRCS` 已包含 `ui_font_date_large.c`、`ui_font_custom.c`、`ui_home.c`、`ui_menu.c` 及 10 个图标资源（5 normal + 5 large）。
- `main/ui_font_custom.c`、`main/ui_font_date_large.c`
  - 字体已生成并可用，且 include 已统一为 `#include "lvgl.h"`（适配 managed LVGL）。
- `gen_lvgl_fonts.py`（新增）
  - 封装 `lv_font_conv` 字体生成流程。
  - 生成后自动规范化 include，避免再次回退为 `lvgl/lvgl.h`。
  - `ui_font_custom` 默认 symbols 已纳入 `°C℃`。

## 2. 🤖 高价值 Prompt 记录

- **问题定位型（高复现率）**
  - “报错在链接阶段/编译阶段分别检查：声明、定义、CMake 注册三者是否一致。”
- **LVGL 字体强约束型**
  - “对中文 label 强制绑定字体：`lv_obj_set_style_text_font(label, &ui_font_custom, 0);`，不要依赖继承。”
- **工程化防回归型**
  - “把一次性修复变成生成后自动后处理（脚本化），避免手工改完又被工具覆盖。”
- **布局改造可验证型**
  - “给出明确 API 和目标状态：对齐枚举、宽度比例、对象数量、路由不变，便于快速验收。”

## 3. ⚙️ 编译与依赖状态

- **工程组件状态**
  - `main` 目录关键文件齐全：`ui_home.*`、`ui_menu.*`、`main.cpp`、`ui_font_custom.c`、`ui_font_date_large.c`、`CMakeLists.txt`。
- **字体与 LVGL 配置**
  - `sdkconfig.defaults` 包含：
    - `CONFIG_LV_FONT_MONTSERRAT_20=y`
    - `CONFIG_LV_FONT_MONTSERRAT_24=y`
    - `CONFIG_LV_FONT_MONTSERRAT_48=y`
    - `CONFIG_LV_FONT_CUSTOM_DECLARE="LV_FONT_DECLARE(ui_font_custom);"`
- **今日关键故障与现状**
  - 反复报错根因已确认：直接运行 `lv_font_conv` 会重写头部为 `lvgl/lvgl.h`，导致 ESP-IDF managed LVGL 找不到头文件。
  - 当前修复路径：使用 `python gen_lvgl_fonts.py` 生成字体，自动改回 `lvgl.h`。
- **运行态观测**
  - 串口监视器正在运行，日志显示系统可启动到应用层（ESP-IDF v6.0，项目 `FocusCore`，PSRAM 初始化通过）。
- **外部依赖工具**
  - 已使用并可用：`lv_font_conv`、Python 运行环境（Espressif venv）。

## 4. ⚠️ 悬而未决的代码债

- `main/ui_home.c`
  - Home 仍大量使用 Mock 文案（时间、日期、温度、电量、待办内容），未接实时数据源。
- `main/main.cpp`
  - `WifiStatusTimerCb()` 内仍有测试下载地址：`http://192.168.40.192:8080/test.bin`。
  - Wi-Fi 状态文本与流程为硬编码，尚未本地化/状态机抽象。
  - 图片渲染路径中存在“动态分配后不释放”注释与实现（`malloc` 后长期持有），需明确生命周期策略并评估长期运行风险。
- `main/ui_menu.c`
  - 菜单项与图标映射仍为静态数组写死，后续可改配置表驱动以便扩展。
- 字体生成流程
  - 若绕过 `gen_lvgl_fonts.py` 直接跑 `lv_font_conv`，仍有回归风险；建议统一团队流程只走脚本。
- 构建验证
  - 建议做一次“从干净构建目录”的全量构建与烧录回归，确认字体脚本流程与 UI 改动在 CI/新环境下也稳定。

# 2026-04-26 上午

## 1. 📁 核心文件变更

- `main/pcf85063.c`（新增）
  - 新增 PCF85063 RTC 驱动，地址 `0x51`，复用已有 I2C 总线。
  - 实现 `pcf85063_init()`、`pcf85063_read_time()`、`pcf85063_set_time()`、`pcf85063_sync_system_time()`。
  - 增加 BCD 编解码与 OS 标志恢复逻辑：当检测到振荡停止标志（OS）时，自动用系统时间或编译时间回种 RTC，再同步系统时钟。
- `main/pcf85063.h`（新增）
  - 暴露 RTC 初始化/读写/系统时间同步接口。
- `main/shtc3.c`（持续演进）
  - 仅温度读取流程稳定：`Wakeup -> 延时 -> Measure -> 延时 -> Read -> Sleep`。
  - 温度加入软件补偿偏移：减 `3.0°C`。
  - 已开启 I2C 内部上拉，并补充唤醒后延时。
- `main/shtc3.h`（改动）
  - 新增 `shtc3_i2c_get_bus_handle()`，用于给 RTC 设备复用同一 I2C master bus。
- `main/battery_monitor.c` / `main/battery_monitor.h`（新增）
  - 基于 `adc_oneshot + curve_fitting` 实现电压读取与百分比换算。
  - 10 次采样去极值平均，按分压比恢复电池电压，线性映射 3.3V~4.2V。
- `main/ui_home.c`（重构升级）
  - 状态栏接入动态温度/电量/时钟刷新。
  - 温度显示从 `%f` 改为整数拼接（`%d.%d`），规避 LVGL 浮点格式化依赖问题；异常显示 `--.-°C`。
  - 对温度 Label 每次更新前重绑 `ui_font_custom`（防样式覆盖）。
  - 新增 RTC 时钟 1s 刷新与冒号闪烁（`HH:MM` / `HH MM`）。
  - RTC 读取失败时保留上一次正确时间（并用本地秒递增兜底），避免时间跳变。
- `main/main.cpp`（改动）
  - 启动阶段接入 `shtc3_i2c_init()` 与 `pcf85063_sync_system_time()`。
  - 保持 Home 首屏路径、输入事件路由与 Wi-Fi 下载逻辑。
  - 图片内存治理已落地：先解除 LVGL 引用再释放动态资源，避免泄漏和野指针。
- `main/CMakeLists.txt`（改动）
  - `SRCS` 现已包含：`shtc3.c`、`pcf85063.c`、`battery_monitor.c`、`ui_home.c`、`ui_menu.c`、`main.cpp`、字体和 icon 资源。

---

## 2. 🤖 高价值 Prompt 记录

- **外设驱动约束型**
  - “必须使用 ESP-IDF v6 新 API（`driver/i2c_master.h` / `esp_adc/adc_oneshot.h`），禁止旧接口。”
- **时序闭环型**
  - “严格按器件协议执行完整流程（Wakeup→Measure→Read→Sleep），并补足唤醒延时。”
- **嵌入式容错型**
  - “失败返回特殊值（如 `-999.0f`），UI 侧按异常占位显示，不做危险跳变。”
- **运行态可验证型**
  - “UI 更新同时打 `ESP_LOGI`，保证界面失败时仍可从串口验证硬件链路。”
- **资源生命周期强约束型**
  - “释放顺序必须是先断开 LVGL 对象引用，再 free 动态内存，防野指针。”
- **低依赖显示策略型**
  - “避免 `%f` 格式化依赖，改成整数拼接显示，提升嵌入式稳定性。”

---

## 3. ⚙️ 编译与依赖状态

- **组件注册状态**
  - `main/CMakeLists.txt` 当前已注册 `shtc3.c / pcf85063.c / battery_monitor.c / ui_home.c / main.cpp` 及字体与图标资源。
- **I2C 外设状态**
  - SHTC3 与 PCF85063 共享同一 I2C bus（SDA13/SCL14）。
  - RTC 地址 `0x51`，SHTC3 地址 `0x70`。
- **显示链路状态**
  - 温度 UI 已从浮点格式化切换为整数格式化，且单位恢复为 `°C`。
  - 温度、时钟、电量均有定时刷新逻辑。
- **运行日志侧观察（最近）**
  - 温度采样有有效值（约 `24.x C`）；
  - 电池读数仍约 `1.82V / 0%`（待硬件侧确认是否接电池）；
  - RTC 出现过 `oscillator stop flag`，代码已补 OS 恢复写回逻辑。
- **构建工具链**
  - 项目仍为 ESP-IDF v6.0 路线；
  - 字体生成链（`gen_lvgl_fonts.py` + `lv_font_conv`）在工程内可用。

---

## 4. ⚠️ 悬而未决的代码债

- `main/ui_home.c`
  - 主页文本仍有较多 Mock 内容（待办、日期卡等）未全部接真实数据源。
- `main/main.cpp`
  - Wi-Fi 下载地址仍为测试 URL：`http://192.168.40.192:8080/test.bin`。
  - Wi-Fi 状态文案与流程尚未做本地化与状态机抽象。
- `main/pcf85063.c`
  - OS 恢复目前回种“系统时间/编译时间”，尚未接入网络授时后的“高可信回写 RTC”闭环。
- `main/ui_home.c`
  - 时钟当前秒级兜底通过本地递增，若长期 RTC 读失败，时间会漂移（建议叠加故障标识与重试策略）。
- `main/battery_monitor.c`
  - 电池百分比仍为线性模型，未做分段放电曲线校准（显示会与真实 SOC 有偏差）。
- 串口与烧录流程
  - 曾出现 `COM3` 被占用导致烧录失败；建议固化“先关 monitor 再 flash”的流程脚本化。

📊 嵌入式资源审计预估

本次累计预估增加 RAM 占用：约 `1~2 KB`（新增 RTC 状态缓存、驱动句柄、定时器回调状态）  
本次累计预估增加 Flash 占用：约 `8~16 KB`（RTC/SHTC3/电池驱动逻辑、日志字符串、UI 刷新分支）  
架构师警告：无重大新增泄漏风险；动态图片内存释放链路已补齐，但 Wi-Fi 下载与 RTC/NTP 仍建议做更系统化的错误恢复与状态抽象。

# 2026-04-27《代码与指令备忘录》

## 1. 📁 核心文件变更

- **`main/ble_memo.c`**
  - **NimBLE GATT Server**：新增自定义 Service/Characteristic（128-bit UUID），特征支持 `WRITE | WRITE_NO_RSP`，写入回调中把数据拷贝到 `FreeRTOS Queue`（禁止在回调里直接操作 LVGL）。
  - **MTU**：初始化中设置 `ble_att_set_preferred_mtu(256)`。
  - **广播修复**：为规避 31B 广播包限制，将 **Service UUID 放 ADV**，将 `"FocusCore_BLE"` **设备名放 Scan Response**（用 `ble_gap_adv_rsp_set_fields()`），修复运行时 `ble_gap_adv_set_fields failed: 4`。

- **`main/ble_memo.h`**
  - **FreeRTOS include 顺序修复**：确保 `FreeRTOS.h` 在 `queue.h` 之前包含，避免级联编译报错。

- **`main/main.cpp`**
  - **GPIO18 长按状态机**：保留 30ms 消抖短按；新增 3000ms 长按，通过 `EventGroup` 异步触发网络/对时（ISR 不做耗时操作）。
  - **Wi-Fi + SNTP 对时闭环**：长按触发后 `wifi_manager_start()` → 等待联网 → `SyncRtcFromNtp()`（含 `TZ=CST-8`）→ `pcf85063_set_time()` → `wifi_manager_stop()`。
  - **BLE→UI 解耦链路**：新增 `s_ble_memo_queue` + `BleMemoUiTask`：从队列取字符串，按 `|` 分割，持锁调用 `ui_home_set_todo_lines()` 刷新待办。

- **`main/ui_home.c` / `main/ui_home.h`**
  - **日期/星期/年份动态更新**：由 RTC 驱动刷新，不再静态写死。
  - **Dirty Check 优化**：仅跨天（`tm_mday` 变化）时才更新日期/星期/年份；时分仍每秒刷新。
  - **待办 UI 更新接口**：新增 `ui_home_set_todo_lines(line1,line2,line3)`，供 BLE UI task 调用。

- **`main/pcf85063.c`**
  - **RTC 写入增强**：`pcf85063_set_time()` 对输入 `struct tm` 做 `mktime()` 归一化 + 范围校验；写入前/读回后打日志便于对比；确保写入包含 `year/mon/mday/wday` 全字段。

- **`main/CMakeLists.txt`**
  - **依赖收敛**：`REQUIRES` 使用 `bt`（ESP-IDF v6 里 NimBLE 属于 `bt` 组件，不存在独立 `nimble` 组件）；补齐 `esp_driver_i2c/gpio/pcnt` 等。
  - **脚本模式兼容**：`set_source_files_properties()` 包在 `if(NOT CMAKE_SCRIPT_MODE_FILE)`，避免 “not scriptable”。
  - **编译标准**：对 `ble_memo.c` 单独指定 `-std=gnu17`。

- **`components/port_bsp/CMakeLists.txt`**
  - **传递依赖修复**：将 `esp_driver_spi`、`esp_lcd` 放入 `REQUIRES`（而非 `PRIV_REQUIRES`），解决 `display_bsp.h` 被上层包含时找不到 `spi_master.h / esp_lcd_panel_io.h` 的问题。

- **`main/user_config.h`**
  - **开发模式确认**：`DEV_MODE_NO_WIFI` 默认 `1`（断网 UI 调试模式），但长按触发网络任务不依赖“开机自启 Wi-Fi”。

---

## 2. 🤖 高价值 Prompt 记录

- **“只看第一条真实 error”策略**
  - NimBLE/FreeRTOS 报错经常级联（上游 include/语法失败会引爆大量“假错误”），排错必须从首个 `error:` 开始清。
- **ESP-IDF v6 组件命名要点**
  - NimBLE 不是独立组件，依赖写 `bt`，避免 `REQUIRES nimble` 触发 CMake 找不到组件。
- **组件依赖传递规则**
  - 若组件头文件（如 `port_bsp/display_bsp.h`）会被外部 include，其依赖必须放 `REQUIRES`，放 `PRIV_REQUIRES` 会导致上层编译找不到头文件。
- **BLE→UI 架构警告落地**
  - GATT write 回调禁止直接调 LVGL；必须 Queue/EventGroup 解耦，UI task 持锁刷新。
- **BLE 广播包 31B 限制**
  - 128-bit UUID + 完整设备名经常超 31B：UUID 放 ADV，Name 放 Scan Response。

---

## 3. ⚙️ 编译与依赖状态

- **工程不是 Git 仓库**
  - `git rev-parse --is-inside-work-tree` 返回非仓库（无法用 `git status/diff` 追溯变更，只能以文件内容为准）。

- **已解决的关键构建问题**
  - **`port_bsp` 缺传递依赖**：`esp_driver_spi`、`esp_lcd` 需在 `REQUIRES`。
  - **FreeRTOS 头顺序**：`FreeRTOS.h` 必须在 `queue.h/task.h` 前。
  - **NimBLE API 兼容**：`ble_svc_gap_init()` / `ble_svc_gatt_init()` 在当前版本是 `void`（不能赋值给 `ret`）。

- **运行时状态（已定位/已修）**
  - **`E (1519) BLE_MEMO: ble_gap_adv_set_fields failed: 4`**：ADV payload 超限导致 EINVAL；已改为 ADV 放 UUID + ScanRsp 放 name。

- **仍存在的构建“红色/黄色提示”**
  - **Kconfig warning**：`sdkconfig.defaults` 里存在 `LV_FONT_CUSTOM_DECLARE` 未知符号（属于 warning，不阻塞编译）。建议后续确认该配置是否写错或应改为 LVGL v9 支持的项。

---

## 4. ⚠️ 悬而未决的代码债

- **`sdkconfig.defaults` 的未知 Kconfig 符号**
  - `LV_FONT_CUSTOM_DECLARE` 警告未消除：长期可能导致团队误判配置是否生效。

- **安全/可维护性：Wi-Fi 凭据硬编码**
  - `main.cpp` 里通过 `#ifndef CONFIG_FOCUSCORE_WIFI_SSID/PASSWORD` 兜底了明文 SSID/密码（建议后续迁移到 `menuconfig` + NVS）。

- **BLE 安全性与低功耗策略未完善**
  - 当前为“可写入”服务，未做配对/权限控制/白名单；也未加入更深的低功耗（连接参数、广播间隔、断开策略等）。

- **组件依赖维护风险**
  - `port_bsp` 的 public header 依赖项一旦扩展，需要同步维护 `REQUIRES`，否则会再次出现“头文件找不到”的级联构建错误。

---

📊 嵌入式资源审计预估  
本功能预估增加 RAM 占用：约 **3–6 KB**  
- 主要来自：BLE memo 队列（`8 * sizeof(ble_memo_msg_t)`，每条 256B 级别）+ BLE UI task 栈（`6144B`）+ NimBLE host task（由 NimBLE 创建，栈/heap 依配置而定）。  

本功能预估增加 Flash 占用：约 **30–80 KB**  
- 主要来自：启用 `bt` + NimBLE host/services 的代码体积（取决于 menuconfig 勾选项、优化等级）。  

架构师警告：**中**  
- BLE/NimBLE 体积和 RAM 增量不可忽视；建议后续做一次 `idf.py size`/heap 运行时采样，确认在 8MB PSRAM 下是否稳定，并明确关闭不需要的 bt profile/features。

依据当前仓库内容整理备忘录；一级标题日期采用对话中的权威日期 **2026年05月05日**。

---

# 2026年05月05日

## 1. 📁 核心文件变更

- **`main/main.cpp`**
  - **BLE 无感授时**：在队列消费任务 `BleMemoUiTask` 中解析以 `TIME:` 开头的 payload；先将裸数据拷入 `char buffer[32]` 并在 `buffer[len]` 补 `\0`；使用 `sscanf(buffer, "TIME:%d-%d-%d-%d-%d-%d", ...)`（与前端的 `TIME:2026-04-29-23-27-46` 格式一致）；构造 `struct tm`（`tm_year = year - 1900`，`tm_mon = month - 1`）后调用 `pcf85063_set_time()` 与 `settimeofday()`；**不在** GATT write 回调里操作 I2C/RTC。
  - **废弃长按 Wi‑Fi NTP 省电路径**：`NetworkControlTask` 不再调用 `wifi_manager_start` / `SyncRtcFromNtp` / `wifi_manager_stop`，仅打日志提示改用 BLE 授时；`SyncRtcFromNtp()` 保留为 deprecated stub（`ESP_ERR_NOT_SUPPORTED`）。
  - 启动流程仍包含 `pcf85063_sync_system_time()`、传感器与 BLE memo 初始化；`#if !DEV_MODE_NO_WIFI` 分支下仍可 `wifi_manager_start`（与长按对时已解耦）。

- **`main/ble_memo.h` / `main/ble_memo.c`**
  - `ble_memo_msg_t` 增加 **`uint16_t len`**，记录写入长度（payload 可无 `\0`）；GATT 回调内仍只做拷贝与 `xQueueSend`，不写 RTC。

- **`main/pcf85063.c`**
  - 仍为 RTC 读写 / OS 标志恢复 / `pcf85063_sync_system_time()` 的实现基础；BLE 授时通过既有 `pcf85063_set_time()` 写入。

- **`gen_lvgl_fonts.py`（工程根目录）**
  - **去掉**在 Python 侧拼接整段 ASCII 可打印字符的逻辑（避免 Windows `cmd.exe` 对 `&`、`>`、`<`、`|` 等截断 `--symbols`）。
  - **`common_3500_chars.txt`** 仅经 `read_symbols_file()` 得到 **`symbols_str`**（去空白后的中文及原有附加符号）。
  - 生成 **`main/ui_font_custom.c`** 时增加 **`--range` `0x20-0x7E`**，由 `lv_font_conv` 原生包含标准 ASCII 可打印区（含空格）。
  - **`ui_font_date_large.c`** 仍为短 `--symbols`（如 `0123456789月日`），**不加** `--range`。
  - 保留生成后 **`#include "lvgl.h"`** 规范化逻辑。

- **`main/CMakeLists.txt`（当前）**
  - `SRCS`：`ble_memo.c`、`shtc3.c`、`pcf85063.c`、`battery_monitor.c`、`ui_font_date_large.c`、`ui_home.c`、`ui_menu.c`、`http_downloader.c`、`wifi_manager.c`、`main.cpp`、`ui_font_custom.c`，以及 `../ui_assets/` 下 10 个图标 C 文件。
  - `REQUIRES`：`bt`、`nvs_flash`、`esp_driver_i2c`、`esp_driver_gpio`、`esp_driver_pcnt`、`app_bsp`、`port_bsp`、`user_app`、`ui_bsp`；`PRIV_REQUIRES`：`esp_adc`、`esp_http_client`。
  - `ble_memo.c` 单独 **`COMPILE_OPTIONS "-std=gnu17"`**（包在 `if(NOT CMAKE_SCRIPT_MODE_FILE)` 内）。

- **资源与工具链相关（仓库内）**
  - 根目录存在 **`common_3500_chars.txt`**（UTF‑8，一级字表 3500 字 + 扩展符号等，供 `--symbols`）；字体依赖本机 **`simhei.ttf`** 路径（脚本内 Windows 默认路径）。

---

## 2. 🤖 高价值 Prompt 记录

- **BLE 与阻塞隔离（架构硬约束）**
  - 「禁止在 BLE GATT write 回调里直接 I2C 写 RTC；必须在队列消费 Task 里完成解析、`pcf85063_set_time`、`settimeofday`。」

- **无 `\0` 二进制 payload（内存安全）**
  - 「按真实长度拷贝到 `buffer[32]`，在 `buffer[len]` 手动写 `\0`，再 `sscanf`；消息结构体要带 `len`。」

- **协议格式钉死（可验收）**
  - 「`sscanf` 格式串必须与前端一致：`TIME:%d-%d-%d-%d-%d-%d`；`struct tm` 遵守 `tm_year = y - 1900`、`tm_mon = m - 1`。」

- **省电与路径裁剪**
  - 「长按 GPIO 不再拉起 Wi‑Fi/NTP；授时改走 BLE，避免常驻射频耗电。」

- **Windows 命令行与字库生成**
  - 「不要把含 `&`、`>`、`<`、`|` 的长 ASCII 塞进 `--symbols`；改用 `lv_font_conv` 的 **`--range 0x20-0x7E`**，与中文 `--symbols` 分离。」

---

## 3. ⚙️ 编译与依赖状态

- **组件**：`main` 已注册上述全部源文件与 UI 资源；NimBLE 通过 **`bt`** 组件链接；`ble_memo.c` 使用 **GNU17**。
- **外设与逻辑**：I2C（SHTC3 + PCF85063）、ADC 电量、`ui_home` / `ui_menu`、BLE memo 队列与 UI 任务并存。
- **`user_config.h`**：`DEV_MODE_NO_WIFI` 默认 **`1`**（关闭开机 Wi‑Fi 与相关 UI 分支）；改为 `0` 时仍会走 `wifi_manager_start`，但与「长按 NTP 对时」已切断。
- **字体流水线**：`python gen_lvgl_fonts.py` 需 **`lv_font_conv` 在 PATH**；本机 **`simhei.ttf`** 路径必须有效；`ui_font_custom.c` / `ui_font_date_large.c` 为生成产物，改脚本后需重新生成再编译。
- **构建环境说明**：当前会话未在本机成功执行 `idf.py build`（环境未暴露 `idf.py`）；本地请以 ESP-IDF 环境验证一次全量编译。

---

## 4. ⚠️ 悬而未决的代码债

- **`main/main.cpp`**：`CONFIG_FOCUSCORE_WIFI_SSID/PASSWORD` 兜底仍为明文；`WifiStatusTimerCb` 内测试下载 URL（`DEV_MODE_NO_WIFI=0` 时）仍为硬编码；开机 `wifi_manager_start` 与 BLE 授时策略的产品级矩阵（何时联网、何时停射频）未文档化。
- **`SyncRtcFromNtp` stub**：保留仅为防误调用；若团队不再需要可考虑删除并清理相关 include/符号，减少误导。
- **BLE**：未强化配对/加密/写入权限；广播与连接参数、功耗策略仍可优化。
- **RTC 可信源**：BLE 授时依赖前端时钟质量；无 NTP 闭环时无「高可信」交叉校验。
- **`common_3500_chars.txt`**：汉字集基于《通用规范汉字表》一级 3500 字来源；若业务要求严格对齐 1988《现代汉语常用字表》，需自行替换字表正文。
- **`lv_font_conv` 兼容性**：若所用版本 **`--range` 语法或语义与 `0x20-0x7E` 不一致**，需对照 `--help` 调整（必要时改为 `32-126` 等）。
- **仓库/Git**：工程未必纳入 Git，变更追溯依赖 IDE/备份；建议恢复版本管理以便评审。

---

以上为截至扫描时的工程快照；若你本地还有未保存或未同步到该路径的修改，以你工作区为准再补一节差异即可。

依据当前仓库中的 `main/` 与 `main/CMakeLists.txt` 扫描结果，整理如下备忘录（一级标题日期按会话惯例记为 **2026年05月05日**；若你以自然日归档，可改为当天日期）。

---

# 2026年05月05日下午

## 1. 📁 核心文件变更

- **`main/CMakeLists.txt`**
  - `SRCS`：`ble_memo.c`、`shtc3.c`、`pcf85063.c`、`battery_monitor.c`、`ui_font_date_large.c`、`ui_home.c`、`ui_menu.c`、`http_downloader.c`、`wifi_manager.c`、`main.cpp`、`ui_font_custom.c`，以及 `../ui_assets/` 下 10 个图标（5 normal + 5 large）。
  - `REQUIRES`：`bt`、`nvs_flash`、`esp_driver_i2c`、`esp_driver_gpio`、`esp_driver_pcnt`、`app_bsp`、`port_bsp`、`user_app`、`ui_bsp`；`PRIV_REQUIRES`：`esp_adc`、`esp_http_client`。
  - `ble_memo.c` 在非 script 模式下单独 **`COMPILE_OPTIONS "-std=gnu17"`**。

- **`main/main.cpp`**
  - 已移除 Wi‑Fi NTP 对时路径；**`TIME:`** 在 **`BleMemoUiTask`** 中解析并写 RTC + `settimeofday`。
  - **长按 GPIO18**：`ble_memo_start_adv()`、状态栏 **`[BLE]`**、**60s** FreeRTOS 单次定时器；超时 **`ble_memo_stop_adv()`** + UI「蓝牙连接超时」；收到 **时间或待办** 后停表、停广播、隐藏 **`[BLE]`**。
  - 待办：通过 **`ble_memo_split_todo_payload`** 最多 **6** 行，**`static_assert(BLE_MEMO_MAX_TODO_LINES == UI_HOME_TODO_LINE_COUNT)`**。
  - Wi‑Fi：仍受 **`DEV_MODE_NO_WIFI`** 与 **`CONFIG_FOCUSCORE_WIFI_*`** / 测试下载宏 **`FOCUSCORE_WIFI_TEST_DOWNLOAD_URL`** 控制。

- **`main/ble_memo.c` / `main/ble_memo.h`**
  - NimBLE GATT 写队列；**`ble_memo_start_adv` / `ble_memo_stop_adv`**（结合 **`ble_hs_synced`、`ble_gap_adv_active`、`ble_gap_conn_active`** 与 **`s_adv_wanted`**，避免无效重复 start/stop）；**开机不同步自动广播**（由 **`OnSync`** 仅打日志）。
  - **`ble_memo_split_todo_payload`**：`| ` 分隔最多 **6** 行。

- **`main/ui_home.c` / `main/ui_home.h`**
  - 首页 400×300：**状态栏**（温度、`[BLE]` 默隐、电量）、时钟/日历、**To Do List**（**6** 行，`[ ]` / `[x]` 删除线）、头像区。
  - 待办容器：**绝对对齐** `TOP_LEFT (0, 140)`，**`lv_obj_set_size(..., todo_w, 160)`**（140+160=300 触底），**无边框**、**背景透明**；内部 Flex **`pad_row=0`**，标题 **Montserrat 20**「To Do List」。
  - 时钟大字：**`LV_ALIGN_TOP_MID`** + 顶部内偏 **12px**，为下方腾空间。
  - **RTC 失败**：时钟/星期/年份占位；**日期**在错误分支临时切 **`ui_font_custom`** 显示 **`  月  日`**，成功更新日历时切回 **`ui_font_date_large`**。
  - 对外：**`ui_home_set_todo_lines`**、**`ui_home_ble_indicator_set_visible`**、**`ui_home_todo_set_first_line_plain`**。

- **`main/pcf85063.c` / `main/pcf85063.h`**
  - **OS / I2C 失败**不静默写假时间；**`pcf85063_sync_system_time`** 不再用编译时间回填 RTC。

- **`main/user_config.h`**
  - **`DEV_MODE_NO_WIFI`** 默认 **`1`**（关开机 Wi‑Fi 等；与长按 BLE 会话可并存，以当前代码为准）。

- **`sdkconfig.defaults`**
  - **`CONFIG_LV_FONT_MONTSERRAT_20/24/48=y`**；**`CONFIG_LV_FONT_CUSTOM_DECLARE`** 已 **注释**（自定义字体在源码中 **`LV_FONT_DECLARE(ui_font_custom)`** 等声明）。

---

## 2. 🤖 高价值 Prompt 记录

- **嵌入式字库缺口**：「`ui_font_date_large` 缺空格/减号且不重生字库时，错误分支临时 **`lv_obj_set_style_text_font(..., &ui_font_custom)`**，成功分支写回 **`&ui_font_date_large`。」**
- **BLE 与 UI 隔离**：「GATT 回调只入队；**`BleMemoUiTask`** 里解析 **`TIME:`**、写 **`pcf85063_set_time`** / **`settimeofday`**，并统一处理待办与「会话结束」。」
- **NimBLE 广播安全调用**：「**`ble_memo_start_adv` / `stop_adv`** 前看 **`ble_hs_synced`、`ble_gap_adv_active`、`ble_gap_conn_active`**；**`s_adv_wanted`** 控制 **DISCONNECT / ADV_COMPLETE** 是否自动重开广播。」
- **布局与可读性**：「待办区用 **绝对坐标 + 固定高度触底** 换垂直空间；时钟用 **TOP_MID + 小 y 偏移** 避免与大号待办区重叠。」

---

## 3. ⚙️ 编译与依赖状态

- **目标**：`sdkconfig.defaults` 中 **`CONFIG_IDF_TARGET="esp32s3"`**，16MB Flash、PSRAM Oct 80M 等（文件头标注为 IDF **5.5.1** 风格 defconfig；实际工具链以本机 **`idf.py --version` / 构建日志** 为准）。
- **LVGL**：Managed **v9**；启用 **Montserrat 20/24/48**；**`ui_font_custom.c` / `ui_font_date_large.c`** 为工程内生成/维护的裁剪字库。
- **蓝牙**：NimBLE 通过 ESP-IDF **`bt`** 组件链接；**`ble_memo.c`** 使用 **GNU17**。
- **外设**：I2C（SHTC3 + PCF85063）、ADC 电量、**RLCD 400×300**、编码器 **PCNT**、GPIO18 按键/长按。
- **建议**：在目标环境中执行一次干净 **`idf.py fullclean && idf.py build`**，确认 **`bt` + `gnu17` + LVGL 字体** 与当前 `sdkconfig` 一致。

---

## 4. ⚠️ 悬而未决的代码债

- **`main/main.cpp`**：`CONFIG_FOCUSCORE_WIFI_SSID/PASSWORD` 仍可有 **menuconfig 外兜底明文**；**`NetworkControlTask` + EventGroup** 长按已不再置位，任务可能 **长期空等**（可删或改作其它用途）。
- **BLE 产品化**：未强调 **配对/加密/写入鉴权**；广播/连接参数与功耗仍可调；**BLE 授时**依赖前端时钟质量。
- **`main/ui_home.c`**：待办 **(0,140)+160** 与左侧时钟卡仍可能 **视觉叠层**（z-order）；若仍重叠需再调 **Y/高度** 或时钟卡高度。
- **字库**：**`ui_font_date_large`** 字符集仍有限；日期占位已靠 **`ui_font_custom`** 缓解，其它控件若混用大字库需同样策略。
- **仓库**：工程未必在 Git 下，变更需靠 IDE/备份；**`sdkconfig.defaults` 与真实 `sdkconfig`** 可能不同步，合并时注意 **LVGL / BT** 选项。

---

📊 **资源与风险（简要）**：启用 **`bt` + NimBLE** 与多任务/队列后，建议定期 **`idf.py size`** 与运行时 **heap** 观察；长按 **60s** 广播窗口与 **UI 定时器** 需与产品「是否允许未同步时重复长按」需求对齐。

---

# 2026年05月07日

## 1. 📁 核心文件变更

- **`main/sd_card.h`（新建）**
  - 声明 `esp_err_t focuscore_sd_init(void)`，C++ guard。

- **`main/sd_card.c`（新建，两次重写）**
  - **v1 — SDSPI 驱动**：`spi_bus_initialize(SPI2_HOST)` + `esp_vfs_fat_sdspi_mount("/sdcard", ...)`；CS=17、CLK=38、MOSI=21、MISO=39；上拉 + 降频 400kHz。因 0x107 超时废弃。
  - **v2 — SDMMC 1-line 驱动**：`SDMMC_HOST_DEFAULT()` + `esp_vfs_fat_sdmmc_mount()`；`slot_config.width=1`、`clk=38`、`cmd=21`、`d0=39`；`SDMMC_SLOT_FLAG_INTERNAL_PULLUP`；挂载成功打印类型+容量。

- **`main/main.cpp`（大量重构）**
  - **引脚冲突修复**：`PIN_ENC_B` 从 `GPIO_NUM_17` 改为 `GPIO_NUM_2`（与 SD 卡 CS 冲突）。
  - **废弃 Wi‑Fi 下载 → 本地 SD 加载**：
    - `g_img_download_ready` → `g_img_data_ready`（全局重命名）。
    - `RuntimeImageRenderFromDownloadedBinLocked` → `RuntimeImageRenderLocked`。
    - 新增 `load_bin_from_sd()`：`fopen`→`fseek/ftell`→`heap_caps_malloc(MALLOC_CAP_SPIRAM)`→`fread`。
    - 从 `app_main` 末尾移除测试加载，改为 File Browser 单击触发。
  - **渲染管道修复**：`RuntimeImageRenderLocked` 开头不再调用 `RuntimeImageReleaseLocked()`（该函数会 `free(g_img_bin_data)` 导致读后即焚）；改为内联释放 LVGL 对象（img_obj/img_dsc）而保留 bin 数据。
  - **图像固定渲染目标**：`lv_image_create(lv_screen_active())`→`lv_image_create(s_digital_garden_screen)`。
  - **4 级屏幕路由重构**：
    - 新增 `s_file_browser_screen`、`s_digital_garden_screen`。
    - Home→Menu→FileBrowser→DigitalGarden 四级路由。
  - **双击状态机**：`KeySingleClickTimerCb`（250ms FreeRTOS 定时器）；KeyBackPressed 检测第二次松开 <250ms→双击，取消待处理单击；超时→`KeyBackSingleClick` 事件。
  - **菜单索引过滤**：仅在 `ui_menu_get_selected_index() == 0`（note 图标）时允许从 Menu 进入 File Browser。
  - **编码器隔离**：Menu 屏→滚动菜单；Digital Garden 屏→滚动图片（步长 60px，边界限幅）。
  - **加载体感优化**：File Browser 单击→立即切到 Digital Garden 屏显示 "Loading..." 标签→再执行 `load_bin_from_sd`。
  - **新增心跳日志**（后注释）：`RENDER_TIMER` 调试日志。
  - **前向声明修复**：`load_bin_from_sd` 声明提前至 `InputEventTask` 之前。
  - **变量作用域修复**：`s_digital_garden_screen` 等 4 个全局变量移至文件顶部渲染函数之前。

- **`main/http_downloader.h` / `main/http_downloader.c`**
  - `g_img_download_ready` → `g_img_data_ready`（同步重命名）。
  - 增加 `volatile` 关键词防止编译器优化跨线程读取。

- **`main/CMakeLists.txt`**
  - SRCS 新增 `sd_card.c`。
  - PRIV_REQUIRES 从 `esp_driver_spi` 改为 `esp_driver_sdmmc`，新增 `fatfs`、`sdmmc`。

- **`main/ui_menu.c`**
  - 确认图标顺序：`[0]note、[1]read、[2]photo、[3]word、[4]music`，note 索引为 0。

---

## 2. 🤖 高价值 Prompt 记录

- **SDMMC vs SDSPI 硬件决策型**
  - 「原生 SDMMC 驱动（1-line 模式），D1/D2 物理悬空。废弃 SPI，改用 `esp_vfs_fat_sdmmc_mount`，`slot_config.width=1`。」

- **渲染管道调试（读后即焚修复）**
  - 「`RuntimeImageReleaseLocked()` 会 `free(g_img_bin_data)` 把刚加载的数据释放了 → `g_img_bin_size = 0` → `pure_data_size = 0 - 12` 无符号下溢 → 渲染静默失败。修法：渲染时只释放 LVGL 对象，不动 bin 数据，数据要移交到新 `img_dsc->data`。」

- **PSRAM 强制分配**
  - 「`heap_caps_malloc(file_size, MALLOC_CAP_SPIRAM)` 强制在 PSRAM 分配，失败时完整回滚（`free` + `fclose`）。」

- **双击状态机（250ms 定时器仲裁）**
  - 「按键松开不立刻动作，启动 250ms 定时器。第二次松开在 250ms 内→双击取消单单击；超时→执行单单击。长按 3000ms 蓝牙独立不受影响。」

- **多级屏幕路由**
  - 「`lv_screen_active()` 判断当前屏：Home→Menu→FileBrowser→DigitalGarden；每种屏定义单/双击的独立行为。」

---

## 3. ⚙️ 编译与依赖状态

- **新增文件与依赖**
  - `main/sd_card.c`、`main/sd_card.h`。
  - PRIV_REQUIRES：`fatfs`、`sdmmc`、`esp_driver_sdmmc`（替代 `esp_driver_spi`）。
  - **编译报错修复**：
    - 缺失 `#endif`：守卫重构时遗漏，导致 `#if !DEV_MODE_NO_WIFI` 未闭合。
    - 变量未声明：`s_digital_garden_screen` 在 `RuntimeImageRenderLocked` 之后声明，移到文件顶部。
    - 函数未声明：`load_bin_from_sd` 在 `InputEventTask` 之后定义，加前向声明。
    - `TAG` 未定义：`load_bin_from_sd` 内缺 `static const char *TAG = "SD_LOAD"`。

- **SD 卡运行态**
  - 挂载成功：`SD_CARD: Mounted at /sdcard | Type: SD | Capacity: 14861 MB`。
  - 文件读取成功：`SD_LOAD: Loaded /sdcard/test.bin (2944012 bytes) into PSRAM`。
  - 渲染闭环：RENDER 日志确认 `lv_image_create` 成功，图片显示在 Digital Garden 屏。

- **按键状态机验证**
  - 单击/双击 250ms 仲裁：心跳日志确认 timer 每秒滴答 2 次，双击后 `g_img_data_ready` 清零。
  - 长按 3000ms BLE 广播：未改动，保持原有逻辑。

---

## 4. ⚠️ 悬而未决的代码债

- **`main/main.cpp`**
  - `CONFIG_FOCUSCORE_WIFI_SSID/PASSWORD` 仍有 menuconfig 外兜底明文。
  - `NetworkControlTask` + `EventGroup` 长按已不再置位，任务长期空等（可删除或改作其它用途）。
  - 编码器 File Browser `TODO`：滚动文件列表未实现。
  - Digital Garden 图片关闭/切换路径不完整：双击释放内存后回 File Browser，但 File Browser 再加载时旧图状态需验证。
  - `s_img_y_offset` 在图片重载/切换后未归零：当新图片高度不同时，滚动位置可能无效。

- **`main/http_downloader.c`**
  - HTTP 下载代码已不被主流程调用（`DEV_MODE_NO_WIFI=1`），但仍保留编译。可考虑按 feature flag 移除或条件编译。

- **文件浏览器 (File Browser)**
  - 当前是占位屏，只有居中 "Note File Browser..." 标签。`load_bin_from_sd("/sdcard/test.bin")` 路径写死，无文件列表选择。

- **SD 卡热插拔与错误恢复**
  - 当前在 `app_main` 启动时调用 `focuscore_sd_init()`，无运行时重试/热插拔检测。若启动时 SD 卡未插入，功能静默跳过。

- **`main/ui_home.c`**
  - 主页待办内容、电量、温度等仍可能有 Mock 数据残留，需确认是否全部接实时数据源。

---

📊 **嵌入式资源审计预估**
本日累计增加 RAM 占用：约 **85-90 KB**（SDMMC 驱动 DMA 描述符 ~2KB、FatFS 内部缓冲区 max_files=5 × 16KB + 元数据 ~5KB；PSRAM 图片加载按文件大小临时分配，运行时清零）
本日累计增加 Flash 占用：约 **30-50 KB**（FatFS 库 + SDMMC 协议栈 + sd_card.c + 路由/双击状态机 + PSRAM 分配路径）
架构师警告：**中低**。`load_bin_from_sd` 成功的 PSRAM 分配在 `RuntimeImageReleaseLocked`（双击回退时）正确释放；图片 `malloc(img_dsc)` 在渲染失败路径上均已 `free`。注意点：双击回到 File Browser 后，`s_img_y_offset` 在下次加载时未自动归零，可能产生跳跃滚动。

# 2026-05-09

## 1. 📁 核心文件变更

- **`main/main.cpp`（大量重构）**
  - **File Browser 目录变更为 `/sdcard/_bin_output/`**：`MAX_FILE_ITEMS 50→30`，路径安全长度 `MAX_PATH_LEN - 8 → -20`，适配 OTA 输出子目录。
  - **File Browser 全屏平铺**：删除"File Browser"标题，容器 400×300 顶格对齐，按钮固定 50px 完美容纳 6 个。
  - **中文文件名支持**：`LV_FONT_DECLARE(ui_font_custom)` + 绑定到每个按钮 Label。
  - **滚动系统三迭代**：
    - v1: `LV_SCROLL_SNAP_START` 吸附 → 老虎机效果（废弃）
    - v2: `lv_obj_scroll_to_view` → WDT 死锁（`lv_obj_get_child` 兄弟链表成环）
    - v3: `update_file_browser_highlight()` 统一封装：`int32_t` 有符号计数 + 钳位 + `lv_obj_scroll_to_view` 安全调用
  - **变量遮蔽 Bug 修复**：残留 `lv_obj_t *` 声明导致 `s_file_list_container` 被局部变量遮蔽，全局始终为 NULL → 高亮/滚动全失效。
  - **视觉修复**：补全 `lv_obj_set_style_bg_opa(child, LV_OPA_COVER, 0)` 确保黑框可见；`lv_obj_get_child(child, 0)` 获取内部 Label 并显式反转文字颜色。
  - **滚动偏移归零**：进入 Digital Garden 前 `s_img_y_offset = 0`，杜绝上张图滚动位置幽灵继承。
  - **隐藏 `.bin` 后缀**：`strrchr(display_name, '.')` 截断扩展名。
  - **续航优化**：时钟刷新周期 `1000ms → 10000ms`，删除冒号闪烁逻辑。
  - **债务清理**：删除 `NetworkControlTask`、`s_network_event_group`、`event_groups.h` 引用。

- **`md_to_bin_pipeline.py`（两次重构）**
  - **第一次——增量支持**：CLI 从单文件改为 `input_dir` + `--output-dir` 全目录扫描；`_sync_cache.json` 按 mtime 增量跳过；`index.json` 统一索引；`MAX_HEIGHT=8000` 物理切片保护 PSRAM；禁止用 sed/bash 改文件（唯一途径为 Edit/Write 内置工具）。
  - **第二次——语义切片重构**：
    - `parse_markdown_sections()` 按 H1→H2→H3 结构树形化解析，H1/H2 标题从渲染内容剥离（UI 已有目录不重复显示）。
    - 文件名改用 `hashlib.md5(content)[:12]` 哈希命名，安全兼容 FATFS。
    - `index.json` 输出树形结构 `{"toc": {"书名": {"章名": [{"title":"节名","file":"hash.bin"}]}}}`。
    - 增加 `resolve_images()` 统一处理 Obsidian `![[wikilink]]` 和标准 `![alt](path)` 两种图片语法。
  - **Bug 修复**：输出目录不存在时 `mkdir(parents=True)` 提前创建。

- **`main/CMakeLists.txt`**
  - SRCS 稳定状态：`sd_card.c`, `ble_memo.c`, `shtc3.c`, `pcf85063.c`, `battery_monitor.c`, `ui_font_date_large.c`, `ui_home.c`, `ui_menu.c`, `http_downloader.c`, `wifi_manager.c`, `main.cpp`, `ui_font_custom.c` + 10 个图标资源。
  - REQUIRES：`bt`, `nvs_flash`, `esp_driver_i2c/gpio/pcnt`, `app_bsp`, `port_bsp`, `user_app`, `ui_bsp`。

---

## 2. 🤖 高价值 Prompt 记录

- **变量遮蔽排查型**
  - 「检查文件级静态指针是否被同名局部变量在 `app_main` 的 if 块内遮蔽了。」
- **LVGL 视觉排查型**
  - 「设置 `bg_color` 必须同时设置 `bg_opa = LV_OPA_COVER`，否则背景透明不可见。」
  - 「按钮内的 Label 不自动继承父按钮的 `text_color`，必须 `lv_obj_get_child(child, 0)` 获取后显式设置。」
- **滚动方案演进型**
  - 「`lv_obj_scroll_to_view` 在 LVGL v9 中遍历兄弟节点链表，若链表成环则无限循环挂死。改用 `lv_obj_scroll_to_y` + 固定按钮高度计算偏移可完全避免链表遍历。」
- **语义切割架构型**
  - 「不要按物理高度切割 Markdown → .bin，那会破坏内容连贯性。按 H2/H3 语义边界切割，每节独立渲染，文件名为内容 Hash。」
- **线程安全与生命周期型**
  - 「`InputEventTask` 是独立 FreeRTOS 任务，调用 LVGL API 前必须锁定 `Lvgl_lock(0)`；`lv_obj_get_child` 的父对象指针必须是容器而非 screen，否则传入空指针或错误对象导致链表遍历崩溃。」

---

## 3. ⚙️ 编译与依赖状态

- **File Browser 组件**
  - 扫描 `/sdcard/_bin_output/` 目录，`.bin` 后缀过滤 + 冒泡排序（≤30 项）。
  - 视觉：黑底白字/白底黑字高亮切换，`bg_opa=COVER` 确保可见，Label 文字颜色显式穿透。
  - 滚动：`update_file_browser_highlight()` 内统一 `lv_obj_scroll_to_view`，外有 `s_file_list_container` 非空保护 + 索引钳位。
- **续航优化状态**
  - 时钟 `10s` 刷新 + `%02d:%02d` 静态格式（无冒号闪烁）。
  - `NetworkControlTask` 已删除，省 6KB 栈 + EventGroup 结构。
- **外部工具链**
  - `md_to_bin_pipeline.py` 依赖 Playwright/Chromium/MathJax/highlight.js。
  - `lv_font_conv`、`pypinyin` 在工程内可用。
- **编译验证**
  - 当前 `main.cpp` 经本会话多次迭代无新增编译错误（已知 `-Wformat-truncation` 被 `#pragma` 压制）。
  - 建议在真实硬件上做一次全量 `idf.py fullclean && idf.py build && idf.py flash monitor` 回归。

---

## 4. ⚠️ 悬而未决的代码债

- **`main/main.cpp`**
  - `s_key_single_click_timer` 双击仲裁 250ms：当文件浏览器列表项 >6 时，单击加载图片 + 跳转 Digital Garden 前无「确认选中」视觉反馈（单击时直接跳转，无二次确认）。
  - `s_img_y_offset` 归零已修复。但 `RuntimeImageReleaseLocked` 调用 `lv_obj_del` 后，`s_digital_garden_screen` 的子对象全清；下次加载新图时若 `RenderCheckTimerCb` 在旧渲染标记清除前触发，可能产生空指针。
  - `CONFIG_FOCUSCORE_WIFI_SSID/PASSWORD` 仍在 `main.cpp` 有 menuconfig 外兜底明文。
  - `DEV_MODE_NO_WIFI` 默认 `1`；设为 `0` 时 Wi-Fi 下载管线未在生产环境验证。

- **`md_to_bin_pipeline.py`**
  - 新语义切片架构尚未在真实多章节 Markdown 文件上端到端测试（特别是包含复杂 LaTeX 公式、多级列表、嵌套代码块的场景）。
  - 节级渲染体积变小后，`section_hash` 碰撞概率极低（MD5 前 12 位 = 48bit），但未做碰撞检测。
  - 增量缓存 `_sync_cache.json` 目前按整体文件 mtime 跳过，若只有个别章节变化会跳过整个文件。可改进为章节级细粒度检测。

- **BLE 产品化**
  - 未强化配对/加密/写入鉴权；广播与连接参数仍可优化。

- **SD 卡热插拔**
  - 开机时 `focuscore_sd_init()` 调用一次；若启动时未插入卡，File Browser 显示无文件，无运行时重试。

---

📊 **嵌入式资源审计预估（本会话累计）**
- 预估净释放 RAM 占用：约 **3-4 KB**（删除 NetworkControlTask 栈 6144B 释放 → 新增 s_file_paths 表 30×128=3840B + 状态变量 ~50B，净释放 ~2.2KB；但 highlight 函数、文件名 buffer 栈占用微增可忽略）
- 预估净增加 Flash 占用：约 **2-4 KB**（文件浏览器全屏布局 + 高亮函数 + 目录路径变更加分支 + `.bin` 截断逻辑）
- 架构师警告：**低**。`update_file_browser_highlight()` 每次迭代会遍历所有子项刷新样式，30 项 × 6 次 API 调用 → 约 180 次 LVGL 操作，在黑白屏上 <5ms。`lv_obj_scroll_to_view` 在钳位保护下不会再引发链表成环死锁。

# 2026-05-10

## 1. 📁 核心文件变更

- **`main/main.cpp`（树形漫游重构）**
  - **cJSON 手写解析器**：`cJSON.c` / `cJSON.h`（334+77 行），支持 Parse/Delete/GetArraySize/GetArrayItem/GetObjectItem/IsObject/IsArray；`cJSON_InitHooks` 将 malloc 重定向到 PSRAM。
  - **四级树状导航栈**：`s_nav_stack[10]`、`s_nav_depth`，基于 `index.json` 的 `toc → filename → H1 → H2 → [切片 Array]` 结构。
  - **`load_index_json()`**：读取 + cJSON_Parse + 详细日志（文件大小、前 50 字节 hex/txt dump、toc 子节点数）。
  - **按钮池**：`s_file_browser_btns[FILE_BTN_MAX=6]` 在 `app_main` 预创建 6 个物理按钮（+内部 Label），`render_file_browser_list()` 只更新文本 + show/hide，零运行时动态创建。
  - **`render_file_browser_list()`**：基于 cJSON 循环链表 `do-while(child != first_child)` 遍历，含 `_safety` 上限（2000）防死循环；Object 分支自动穿透 `__DEFAULT__` 到子节点渲染；Array 分支取 `"title"` 字段。
  - **`__DEFAULT__` 穿透**：单击时 `while(cJSON_IsObject(target) && target->child->next == target->child)` 判断独生子；渲染层 Object 分支中 `child->string == "__DEFAULT__"` 时自动下沉。
  - **单击事件按父级类型分发**：`cJSON_IsArray(current_parent)` → 取 `"file"` 字段加载 `.bin`；else → 目录穿透入栈。
  - **修复误删恢复**：`InputEventTask`、`InputInit`、`load_bin_from_sd`、`update_file_browser_highlight`、`RenderCheckTimerCb`、`WifiStatusTimerCb` 在渲染重写后被意外删除并恢复。
  - **编码器严格钳位**：`if(new_idx >= s_current_list_count) new_idx = s_current_list_count > 0 ? s_current_list_count - 1 : 0`。
- **`main/cJSON.c`（新建）**
  - 手写 cJSON 解析器：Object/Array/Number/String/True/False/NULL 全类型支持，Windows `\r\n` 兼容，UTF-8 字符串支持。
- **`main/cJSON.h`（新建）**
  - cJSON 公共 API 声明。

## 2. 🤖 高价值 Prompt 记录

- **视图缓存 ViewModel 解耦型**
  - 「不要在运行时调用 `cJSON_GetArraySize`/`cJSON_GetArrayItem` 获取 UI 计数，而是在渲染时预先填充 `s_current_nodes[]` 缓存池，UI 交互直接从缓存 O(1) 获取。」
- **按钮池物理对象型**
  - 「物理按钮在 `app_main` 初始化时一次性创建并常驻（`s_file_browser_btns[6]`），渲染函数只更新文本 + show/hide，绝对禁止运行时 `lv_label_create`。」
- **链表安全遍历型**
  - 「cJSON 使用循环链表，遍历时必须记录 `first_child`，每步 `child->next` 后用 `child != first_child` 判终；必须在循环体内挂 `_safety` 硬计数上限防链表损坏死循环。」
- **父级类型优先分发型**
  - 「单击事件不允许根据 `selected_node` 自身类型（叶子也是 Object）判定操作，必须根据 `current_parent`（nav_stack 栈顶）的类型分发：Array→加载文件、Object→目录穿透。」
- **ESP-IDF v6 组件缺失型**
  - 「ESP-IDF v6 不再内置 `cjson` 组件，必须将 `cJSON.c`/`cJSON.h` 源码直接加入 `main/` 目录编译。」

## 3. ⚙️ 编译与依赖状态

- **主组件注册状态**（`main/CMakeLists.txt`）
  - `SRCS`：`cJSON.c`、`sd_card.c`、`ble_memo.c`、`shtc3.c`、`pcf85063.c`、`battery_monitor.c`、`ui_font_date_large.c`、`ui_home.c`、`ui_menu.c`、`http_downloader.c`、`wifi_manager.c`、`main.cpp`、`ui_font_custom.c` + 10 个图标资源。
  - `REQUIRES`：`bt nvs_flash esp_driver_i2c esp_driver_gpio esp_driver_pcnt app_bsp port_bsp user_app ui_bsp`。
  - `PRIV_REQUIRES`：`esp_adc esp_http_client fatfs sdmmc esp_driver_sdmmc`。
  - `ble_memo.c` 单独 `-std=gnu17`。
- **关键变量表**
  | 符号 | 用途 |
  |------|------|
  | `s_nav_stack[10]` / `s_nav_depth` | 四级树状导航栈 |
  | `g_index_json` | 全局 cJSON 根（PSRAM 分配） |
  | `s_current_nodes[30]` / `s_current_list_count` | 视图缓存池 |
  | `s_file_browser_btns[6]` | 物理按钮池 |
  | `s_file_browser_idx` | 编码器选中索引 |
  | `s_file_paths[30][128]` | Array 层文件路径缓存 |
- **运行时日志状态**
  - SD 卡挂载成功（14861 MB）、index.json 加载成功（12459 bytes）、`cJSON_Parse` 成功（root type=64 for Object）。
  - `'toc' found with 0 child(ren)` — 已知 bug：手写 `cJSON_GetArraySize` 对 Object 返回 0（仅检查 `type & cJSON_Array`），但 `toc->child` 非空且 `first->string = "测试"`，不影响渲染（不使用 GetArraySize 而直接遍历 child 链表）。
- **已知缺陷**
  - Array 最后一层渲染显示 `"?"`（未取到 `"title"`），单击报 `"File node not found or valuestring is NULL!"`。怀疑 `index.json` 的 Array 结构中 `"title"`/`"file"` 字段与上位机生成格式不匹配，需检查 `md_to_bin_pipeline.py` 输出的 JSON 实际字段名。

## 4. ⚠️ 悬而未决的代码债

- **Array 层级「?」显示 + 文件加载失败（今日首要债务）**
  - 最后一级点开后显示 `"?"` 而非笔记标题，再单击日志打印 `E FILE_BROWSER: File node not found or valuestring is NULL!`。
  - 根因推测：`md_to_bin_pipeline.py` 生成的 `index.json` 中 Array 项的字段名与 `main.cpp` 中 `cJSON_GetObjectItem(child, "title")` / `cJSON_GetObjectItem(selected_node, "file")` 不匹配。建议上位机输出 `index.json` 样本，在 PC 端验证字段名后再解析。
- **`main.cpp`（通用性）**
  - `CONFIG_FOCUSCORE_WIFI_SSID/PASSWORD` 仍有 menuconfig 外兜底明文。
  - `DEV_MODE_NO_WIFI` 默认 1；Wi-Fi 下载管线未在生产环境验证。
- **BLE 产品化**
  - 未强化配对/加密/写入鉴权；广播与连接参数仍可优化。
- **SD 卡热插拔**
  - 开机时 `focuscore_sd_init()` 调用一次；若启动时未插入卡，File Browser 显示无文件，无运行时重试。
- **cJSON 手写解析器局限**
  - `cJSON_GetArraySize` 对 Object 返回 0（仅支持 Array 类型）；`cJSON_Delete` 处理循环链表正确但未经大规模测试；无序列化能力（Print 系列函数），仅解析 + 只读访问。

---

📊 **嵌入式资源审计预估（本会话累计）**
- 预估增加 RAM 占用：约 **90-110 KB**（cJSON 解析树 ~12KB + PSRAM、按钮池 6× ~256B + LVGL 对象、导航栈 ~160B；SD 卡 & FatFS 驱动 ~85KB 来自之前会话）
- 预估增加 Flash 占用：约 **30-50 KB**（cJSON.c 编译 + 导航栈逻辑 + 按钮池初始化 + 删除旧 populate_file_list 节省 ~2KB）
- 架构师警告：**低**。cJSON 解析的 PSRAM 分配是一次性的，`g_index_json` 常驻。注意最后一级「?」显示 bug 需上位机配合排查，不影响系统稳定性。
