# 2026年05月11日

## 1. 📁 核心文件变更

- **`main/main.cpp`（严重损坏发现）**
  - **问题根因**：Git 仓库中的文件自初始提交起就存在行顺序混乱（非连续行号），导致代码逻辑完全错乱。
  - **症状**：终端报错 `E (83995) FILE_BROWSER: File node not found or valuestring is NULL!`，屏幕显示黑色问号无法点击。
  - **排查过程**：
    1. 定位错误日志到 `InputEventTask` 函数的 `KeyBackSingleClick` 事件处理（第 739 行）
    2. 发现 JSON 解析逻辑缺少防御式检查：仅用 `if(file_node && file_node->valuestring)` 判断，未检查节点类型
    3. 尝试 `git checkout` 恢复时发现所有历史提交（c5ef632/a21fbc3/0e1d56d）中文件均已损坏
    4. 通过 PowerShell 读取文件发现行号非连续（如 730→731→737→735→736→738），代码执行顺序完全错乱
  - **临时解决方案**：生成修复补丁文件 `fix_main_cpp.patch`，需手动应用。

- **CMake 缓存污染（已修复✓）**
  - **问题**：项目从 `09_LVGL_V9_Test` 改名为 `FocusCore` 后，`build/` 目录中的 CMake 缓存仍引用旧路径。
  - **症状**：`ninja: error: rebuilding 'build.ninja': subcommand failed`，大量 `cmake_install.cmake` 路径错误。
  - **修复**：执行 `Remove-Item -Recurse -Force "build"` 清理构建缓存。

---

## 2. 🤖 高价值 Prompt 记录

- **嵌入式防御式编程型**
  - 「对 cJSON API 调用必须三层检查：`cJSON_IsObject(node)` → `cJSON_GetObjectItem() != NULL` → `cJSON_IsString() && valuestring != NULL`，每层失败后立即 `Lvgl_unlock()` + `continue`。」
- **Git 仓库损坏诊断型**
  - 「当 `git checkout` 后文件仍异常时，用 `git show <commit>:path/to/file` 检查历史版本；若所有提交均损坏，说明问题在初始提交时引入。」
- **CMake 缓存清理型**
  - 「项目改名后必须删除 `build/` 目录，CMake 会缓存绝对路径，改名后这些路径全部失效。」

---

## 3. ⚙️ 编译与依赖状态

- **构建环境**：ESP-IDF v6.0，目标 ESP32-S3，16MB Flash + 8MB PSRAM。
- **CMake 缓存**：已清理，等待在 ESP-IDF 终端中重新构建。
- **Git 状态**：`HEAD` 在 `0e1d56d`，工作区干净但 `main/main.cpp` 文件本身已损坏。
- **待修复文件**：`main/main.cpp` 第 730-740 行附近的 JSON 解析逻辑。

---

## 4. ⚠️ 悬而未决的代码债

- **`main/main.cpp` 文件损坏（高优先级）**
  - Git 仓库中所有提交的 `main.cpp` 行顺序均混乱，无法通过 `git checkout` 恢复。
  - 已生成修复补丁 `fix_main_cpp.patch`，需手动编辑文件应用。
  - 修复后必须提交新版本覆盖损坏的历史。

- **JSON 结构契约验证（中优先级）**
  - 修复后需在真实 `index.json` 上验证 Array 元素是否包含 `"title"` 和 `"file"` 字段。
  - 建议在 PC 端用 `jq` 或 Python 脚本验证 `md_to_bin_pipeline.py` 输出的 JSON 结构。

- **其他已知债务**（继承自之前会话）
  - `CONFIG_FOCUSCORE_WIFI_SSID/PASSWORD` 仍有 menuconfig 外兜底明文。
  - BLE 未强化配对/加密/写入鉴权。
  - SD 卡热插拔无运行时重试。

---

📊 **嵌入式资源审计预估（本次修复）**
- 预估增加 RAM 占用：**0 KB**（仅逻辑重构，无新增全局变量）
- 预估增加 Flash 占用：**+0.5 KB**（新增 3 个错误分支 + 类型检查逻辑）
- 架构师警告：**无**。修复后的代码符合防御式编程原则，所有 cJSON API 调用前均有类型/NULL 检查。

---

# 2026年05月11日（cJSON 循环链表 Bug 修复）

## 1. 📁 核心文件变更

- **`main/cJSON.c` / `main/cJSON.h`（重写修复）**
  - **根本问题**：手写 cJSON 的 `cJSON_GetObjectItem` 循环条件错误 `while (child != object->child)` 导致第一次迭代后立即退出，永远找不到任何键。
  - **修复**：改为 `cJSON *first = child; do { ... } while (child != first);` 确保至少遍历一次并正确遍历整个循环链表。
  - **影响范围**：所有 JSON 解析（index.json、待办、授时）全部失效，表现为"键存在但 API 返回 NULL"。

- **`main/main.cpp`（多态 JSON 解析）**
  - **Array 元素多态支持**：
    - Branch A: `cJSON_IsString(selected_node)` → 直接取 `valuestring` 作为文件名
    - Branch B: `cJSON_IsObject(selected_node)` → 手动遍历查找 `"title"` 键（绕过坏的 `cJSON_GetObjectItem`）
    - Branch C: Object 单子节点 → 取子节点的 `valuestring`
  - **Array 本身作为目录**：`cJSON_IsArray(selected_node)` 时压入导航栈，渲染其子项。
  - **渲染层手动遍历**：`render_file_browser_list` 中对 Array 元素手动 `do-while` 遍历查找 `"title"`，不依赖 `cJSON_GetObjectItem`。

- **`main/CMakeLists.txt`**
  - 保持 `cJSON.c` 在 SRCS 中（ESP-IDF v6.0 无内置 cJSON 组件）。

---

## 2. 🤖 高价值 Prompt 记录

- **3次修复原则触发**：「同一问题尝试3次表面修复仍失败 → 立即停止打补丁，质疑底层依赖。」
- **矛盾信号识别**：「调试日志显示 `Key[0]: 'title' (type=16)` 存在，但 `GetObjectItem("title")` 返回 NULL → 100% 是中间函数 bug。」
- **循环链表陷阱**：「`do { ... } while (child != list->child)` 第一次迭代后立即退出；正确写法：先保存 `first = child`，条件改为 `child != first`。」
- **用户关键提示**：「我建议你重新搞一个正经可以用的 cJSON」→ 迫使重新审视底层库，发现循环 bug。

---

## 3. ⚙️ 编译与依赖状态

- **cJSON 状态**：手写版本已修复，`cJSON_GetObjectItem` 循环条件正确，`cJSON_IsString` 宏已添加。
- **JSON 结构**：`index.json` 为树形结构 `{"toc": {"书名": {"章名": [{"title":"节名","file":"hash.bin"}]}}}`。
- **多态解析**：支持 Array 元素为 String（直接文件名）、Object（含 `"title"/"file"` 字段）、Object 单子节点（子节点值为文件名）三种格式。
- **编译验证**：已通过编译，等待烧录测试。

---

## 4. ⚠️ 悬而未决的代码债

- **cJSON 手写库局限**：
  - 仅支持解析 + 只读访问，无序列化能力（Print 系列函数）。
  - `cJSON_GetArraySize` 对 Object 返回 0（仅支持 Array 类型）。
  - 未经大规模测试，复杂 JSON 可能有边界 case。
- **JSON 结构契约**：上位机 `md_to_bin_pipeline.py` 输出的 `index.json` 必须与解析逻辑匹配，字段名变更需同步修改。
- **调试经验沉淀**：已写入 `.cursorrules` 第4章「调试与问题定位铁律」，包含循环链表陷阱、3次修复原则、矛盾日志警报等。

---

📊 **嵌入式资源审计预估**
- 预估增加 RAM 占用：**0 KB**（仅修复现有代码逻辑，无新增全局变量）
- 预估增加 Flash 占用：**+0.5 KB**（多态分支 + 手动遍历逻辑）
- 架构师警告：**无**。修复后的 cJSON 循环逻辑正确，所有 API 调用前均有类型/NULL 检查。
