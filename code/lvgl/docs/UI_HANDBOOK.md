# LVGL v9 UI 组件库 — 技术交接文档

> 项目：高昌举升机 HMI 移植（方案二 Dark Industrial）  
> 日期：2026-04-27  
> 目标平台：ESP32-S3 (PSRAM 8MB+)，当前验证环境：PC 仿真器 (lv_port_pc_vscode)  
> LVGL 版本：v9.3.0-dev  
> 屏幕分辨率：1024×600  

---

## 一、项目概述

将 HTML/CSS 原型（方案二：左侧导航栏 + Dark Industrial 绿色主题）完整移植为 LVGL v9 C 语言组件库。所有中文文本替换为英文，使用 LVGL 原生 Grid/Flex 布局、原生键盘、全局样式对象。

---

## 二、目录结构与文件说明

```
main/UI/
├── ui.h                    # 全局入口头文件
│                           #   - 页面枚举 ui_page_id_t (BOOT/DASH/CTRL/SET/ABOUT)
│                           #   - UI Manager 结构体 (nav/content_area/keyboard/header)
│                           #   - 页面切换 API: ui_switch_page()
│                           #   - 键盘 API: ui_keyboard_show_num/show_text/hide()
│                           #   - 40+ 英文文本宏 (UI_TEXT_xxx)
│
├── ui.c                    # 核心实现
│                           #   - ui_init(): 初始化主题→创建屏幕布局→创建导航栏→创建Header→创建键盘→显示Boot页
│                           #   - create_sidebar(): 左侧200px导航栏，4个可选中按钮
│                           #   - create_header(): 顶部60px状态栏（品牌/型号/状态/时钟）
│                           #   - create_keyboard(): 全局隐藏 lv_keyboard
│                           #   - ui_switch_page(): 销毁旧页面→创建新页面→更新导航按钮状态
│
├── pages/
│   ├── ui_page_boot.c/.h   # 全屏登录页（居中卡片：Logo+标题+用户名/密码输入+登录按钮）
│   ├── ui_page_dash.c/.h   # Dashboard：4列2行 Grid 布局
│   │                       #   Card A(跨2列): 立柱高度 (Pillar1/Pillar2 大数值)
│   │                       #   Card B(1列):   高度差值 (带颜色状态)
│   │                       #   Card C(跨2行): 报警+状态列表
│   │                       #   Card D/E/F:    手动控制/参数/快捷面板 (小按钮)
│   │
│   ├── ui_page_ctrl.c/.h   # 控制面板：6个 220×140 App Card 按钮居中排列
│   ├── ui_page_set.c/.h    # 设置页：7行设置项（语言/亮度/报警阈值/版本/序列号/MAC/管理员）
│   └── ui_page_about.c/.h  # 关于页：标题+产品简介+2×3特性网格+版本号
│
├── components/
│   ├── ui_comp_btn.c/.h    # ui_comp_app_btn_create()    → 控制页大卡片按钮
│   │                       # ui_comp_ctrl_btn_create()   → Dashboard 小按钮
│   │                       # ui_comp_setting_row_create()→ 设置行（图标+标签+值控件）
│   │
│   ├── ui_comp_msgbox.c/.h # ui_comp_msgbox_show() → 确认/取消弹窗（遮罩层+居中面板）
│   └── ui_comp_header.c/.h # ui_header_set_clock/set_system_status/set_link_status()
│
├── fonts/
│   └── ui_font.h           # 13个字体宏定义
│                           #   UI_FONT_PRIMARY     → &lv_font_montserrat_14
│                           #   UI_FONT_DISPLAY_L   → &lv_font_montserrat_42
│                           #   UI_FONT_CARD_VALUE  → &lv_font_montserrat_42
│                           #   等（换CJK字体只改此文件）
│
└── utils/
    ├── ui_theme.c/.h        # 调色板 + 9个全局 lv_style_t
    │                        #   颜色: UI_COLOR_BG(0x151515) UI_COLOR_ACCENT(0xB1D873) 等12色
    │                        #   样式: style_card / style_btn_primary / style_nav_btn 等
    │                        #   布局常量: UI_NAV_WIDTH(200) UI_HEADER_HEIGHT(60) 等
    │
    └── ui_events.c/.h       # 事件回调实现 + 回调注册表
                             #   ui_event_nav_clicked → 页面切换
                             #   ui_event_ta_focused  → 弹出键盘
                             #   ui_event_kb_ready    → 隐藏键盘
                             #   ui_register_callback() → 注册硬件层回调
```

---

## 三、架构设计决策

### 3.1 屏幕布局

```
┌──────────┬──────────────────────────────────────────────┐
│          │  Header (60px, 品牌/型号/状态/时钟)           │
│          ├──────────────────────────────────────────────┤
│  Sidebar │                                              │
│  (200px) │  Content Area (按需加载页面)                   │
│          │                                              │
│  4个     ├──────────────────────────────────────────────┤
│  导航按钮│  Keyboard (默认隐藏, 聚焦textarea时弹出)       │
└──────────┴──────────────────────────────────────────────┘
```

- 屏幕使用 `LV_LAYOUT_FLEX` + `LV_FLEX_FLOW_ROW` 水平分割
- 右侧使用 `LV_FLEX_FLOW_COLUMN` 垂直分割 Header / Content / Keyboard
- 导航按钮使用 `LV_OBJ_FLAG_CHECKABLE`，active 态通过 `LV_STATE_CHECKED` 切换样式

### 3.2 页面管理

- 页面**按需创建/销毁**（不预加载），节省 ESP32-S3 内存
- `ui_switch_page()` 流程：隐藏键盘 → `lv_obj_delete` 旧内容 → 调用页面 create 函数 → `lv_obj_set_parent` 到 content_area → 更新导航按钮
- Boot 页面是首个显示的页面，登录成功后调用 `ui_switch_page(UI_PAGE_DASH)`

### 3.3 样式系统

- 9 个全局 `lv_style_t` 对象在 `ui_theme_init()` 中初始化
- 控件通过 `lv_obj_add_style()` 挂载样式，**避免**逐属性 `lv_obj_set_style_xxx()`
- 导航按钮有两种样式：`style_nav_btn`（默认）和 `style_nav_btn_active`（选中态），通过 `LV_STATE_CHECKED` 自动切换

### 3.4 键盘系统

- 1 个全局 `lv_keyboard` 对象，创建时 `LV_OBJ_FLAG_HIDDEN`
- `ui_keyboard_show_num(ta)` → `LV_KEYBOARD_MODE_NUMBER` + 关联 textarea + 取消隐藏
- `ui_keyboard_show_text(ta)` → `LV_KEYBOARD_MODE_TEXT_LOWER` + 关联 textarea + 取消隐藏
- `ui_keyboard_hide()` → 解除关联 + 隐藏
- 键盘的 `LV_EVENT_READY` 和 `LV_EVENT_CANCEL` 事件都调用 `ui_keyboard_hide()`

---

## 四、构建系统集成

### 4.1 CMakeLists.txt 修改

```cmake
# 新增头文件路径
include_directories(${PROJECT_SOURCE_DIR}/main/UI)
include_directories(${PROJECT_SOURCE_DIR}/main/UI/pages)
include_directories(${PROJECT_SOURCE_DIR}/main/UI/components)
include_directories(${PROJECT_SOURCE_DIR}/main/UI/fonts)
include_directories(${PROJECT_SOURCE_DIR}/main/UI/utils)

# 自动收集所有 UI 源文件
file(GLOB_RECURSE UI_SOURCES "${PROJECT_SOURCE_DIR}/main/UI/*.c")
add_executable(main ... ${UI_SOURCES})
```

### 4.2 SDL_main.c 修改

| 项目 | 旧值 | 新值 |
|------|------|------|
| 分辨率 | `hal_init(320, 480)` | `hal_init(1024, 600)` |
| UI入口 | `create_hover_card()` | `ui_init()` |
| MSVC兼容 | `int SDL_main(...)` | 条件编译 `#if defined(_MSC_VER) int main(...) #else int SDL_main(...) #endif` |

---

## 五、已知问题与待完善项

### 5.1 当前已知问题

| # | 问题 | 严重性 | 位置 | 说明 |
|---|------|--------|------|------|
| 1 | Boot 页登录未实现真实验证 | 中 | `ui_page_boot.c` | `boot_login_event()` 直接调用 `ui_switch_page(UI_PAGE_DASH)`，未校验用户名密码 |
| 2 | Header 时钟未动态更新 | 低 | `ui_comp_header.c` | 需在主循环或定时器中调用 `ui_header_set_clock()` |
| 3 | 控制页按钮无事件处理 | 中 | `ui_page_ctrl.c` | App Card 按钮点击未绑定回调 |
| 4 | Dashboard 控制按钮无事件 | 中 | `ui_page_dash.c` | Card D/E/F 的 ctrl_btn 未绑定回调 |
| 5 | 设置页亮度/报警阈值点击键盘未完成闭环 | 低 | `ui_page_set.c` | 点击标签弹出键盘后，确认输入未回写显示 |
| 6 | 管理员管理按钮无功能 | 低 | `ui_page_set.c` | 仅创建了一个 Add 按钮，未实现管理员表格 |
| 7 | C4819 编码警告 | 低 | 多个文件 | MSVC 报"文件包含不能在当前代码页(936)中表示的字符"，需保存为 UTF-8 BOM |

### 5.2 待完善功能

- [ ] Boot 页真实登录逻辑（对接 ESP32 NVS 存储的账号密码）
- [ ] Dashboard 数据动态刷新（立柱高度、报警状态需通过 `ui_invoke_callback` 从硬件层获取）
- [ ] 控制页 App Card 按钮绑定动作（打开弹窗/切换开关等）
- [ ] 管理员管理弹窗（管理员列表表格 + 添加/删除功能）
- [ ] ON/OFF 开关切换弹窗（原型的 toggle overlay）
- [ ] 控制值输入弹窗（原型的 ctrl_overlay + 内嵌数字键盘）
- [ ] 头文件编码统一为 UTF-8 BOM（解决 MSVC C4819 警告）
- [ ] 页面切换动画（原 HTML 有 translateX 动画，LVGL 可用 `lv_screen_load_anim`）
- [ ] SVG Logo 图标（当前用 LV_SYMBOL 占位）
- [ ] 实时时钟定时器（`lv_timer_create` 每秒更新时钟）

---

## 六、LVGL v9 踩坑记录

### 6.1 OPA 透明度宏

**问题**：代码中使用了 `LV_OPA_2`、`LV_OPA_4`、`LV_OPA_6` 等宏，但 LVGL v9 只定义了步长 10 的宏。

**LVGL v9 定义的 OPA 宏**：
```c
LV_OPA_TRANSP = 0,  LV_OPA_0 = 0,
LV_OPA_10 = 25,     LV_OPA_20 = 51,
LV_OPA_30 = 76,     LV_OPA_40 = 102,
LV_OPA_50 = 127,    LV_OPA_60 = 153,
LV_OPA_70 = 178,    LV_OPA_80 = 204,
LV_OPA_90 = 229,    LV_OPA_100 = 255,
LV_OPA_COVER = 255
```

**修复方案**：直接使用数值，如 `lv_style_set_bg_opa(&style, 10)` 替代 `LV_OPA_4`（10/255 ≈ 4%）。

**涉及文件**：`ui_theme.c`、`ui.c`、`ui_comp_btn.c`

### 6.2 LV_SYMBOL 符号宏

**问题**：使用了 LVGL v9 不存在的 symbol 宏。

| 不存在的宏 | 替换为 | 原因 |
|-----------|--------|------|
| `LV_SYMBOL_MINIMIZE` | `LV_SYMBOL_DOWN` | v9 无 MINIMIZE |
| `LV_SYMBOL_MAXIMIZE` | `LV_SYMBOL_UP` | v9 无 MAXIMIZE |
| `LV_SYMBOL_QUESTION` | `LV_SYMBOL_WARNING` | v9 无 QUESTION |
| `LV_SYMBOL_DOT` | `LV_SYMBOL_BULLET` | v9 无 DOT |
| `LV_SYMBOL_USER` | `LV_SYMBOL_CHARGE` | v9 无 USER |

### 6.3 LV_FLEX_ALIGN_STRETCH 不存在

**问题**：代码中使用了 `LV_FLEX_ALIGN_STRETCH`，LVGL v9 的 `lv_flex_align_t` 枚举不包含此值。

**可用值**：
```c
LV_FLEX_ALIGN_START
LV_FLEX_ALIGN_END
LV_FLEX_ALIGN_CENTER
LV_FLEX_ALIGN_SPACE_EVENLY
LV_FLEX_ALIGN_SPACE_AROUND
LV_FLEX_ALIGN_SPACE_BETWEEN
```

**修复**：全部替换为 `LV_FLEX_ALIGN_START` 或 `LV_FLEX_ALIGN_CENTER`。

### 6.4 头文件循环包含

**问题**：`ui_events.h` 和 `ui_events.c` 内容写反（.h 文件包含了 .c 的内容，.c 包含了 .h 的内容），导致 MSVC 报 "包含文件太多: 深度=1024" 错误。

**修复**：重写两个文件，确保 `.h` 只有声明和宏，`.c` 才有实现。

### 6.5 MSVC 下 SDL_main 入口点

**问题**：MinGW/GCC 下 SDL2 重定义 `main` 为 `SDL_main`，但 MSVC 不会自动做此重定义，导致链接时 `LNK2019: 无法解析的外部符号 main`。

**修复**：条件编译
```c
#if defined(_MSC_VER)
int main(int argc, char **argv)
#else
int SDL_main(int argc, char **argv)
#endif
```

---

## 七、调试指南

### 7.1 PC 仿真器运行

```bash
cd lv_port_pc_vscode/lv_port_pc_vscode/build
cmake --build . --config Release
./bin/Release/main.exe
```

### 7.2 常见调试场景

**场景1：页面切换崩溃**
- 检查 `ui_switch_page()` 中 `s_page_creators[id]` 是否为 NULL
- 检查页面 create 函数返回的 `lv_obj_t *` 是否为 NULL
- 确认旧页面 `lv_obj_delete` 时机正确（不能删除当前活动的 screen）

**场景2：键盘不弹出**
- 检查 `ui_event_ta_focused` 是否正确绑定到 `LV_EVENT_FOCUSED`
- 检查 `mgr->keyboard` 是否为 NULL
- 检查键盘是否被其他控件遮挡（`lv_obj_move_foreground`）

**场景3：导航按钮不切换高亮**
- 确认按钮设置了 `LV_OBJ_FLAG_CHECKABLE`
- 确认 `style_nav_btn_active` 绑定在 `LV_STATE_CHECKED` 上
- 检查 `ui_switch_page()` 中是否正确调用了 `lv_obj_add_state/clear_state`

**场景4：样式不生效**
- 确认 `ui_theme_init()` 在 `ui_init()` 最先调用
- 确认 `lv_obj_add_style()` 的 selector 参数正确（0 = 默认, `LV_STATE_CHECKED` = 选中态）
- 全局 style 对象必须是 `static` 或全局变量（不能是局部变量）

### 7.3 LVGL v9 API 与 v8 差异速查

| v8 API | v9 API | 说明 |
|--------|--------|------|
| `lv_scr_act()` | `lv_screen_active()` | 获取活动屏幕 |
| `lv_obj_set_style_transform_zoom()` | `lv_obj_set_style_transform_scale_x/y()` | 缩放（256=100%） |
| `lv_chart_set_next_value()` | 不变 | 图表 |
| `lv_style_set_bg_opa(style, LV_OPA_50)` | 不变 | 透明度 |
| `lv_obj_del()` | `lv_obj_delete()` | 删除对象 |

---

## 八、移植到 ESP32-S3 的步骤

1. 将 `main/UI/` 目录复制到 ESP-IDF 项目的 `components/ui/`
2. 修改 `CMakeLists.txt`：用 `idf_component_register(SRCS ... INCLUDE_DIRS ...)` 替代 `file(GLOB_RECURSE)`
3. 在 `ui_font.h` 中替换字体指针为 CJK 字体（使用 LVGL Font Converter 生成）
4. 将 `ui_events.c` 中的 `ui_register_callback` 对接到实际的硬件控制逻辑
5. 将 `SDL_main.c` 中的 `ui_init()` 调用移到 ESP-IDF 的 `app_main()` 中
6. 修改 `lv_conf.h` 中的 `LV_MEM_SIZE` 为 ESP32-S3 PSRAM 适配值
7. 添加 `lv_timer_create()` 实现时钟更新和数据刷新
