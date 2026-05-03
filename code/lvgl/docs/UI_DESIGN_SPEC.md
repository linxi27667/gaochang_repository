# LVGL v9 UI 组件库 — 设计规格说明书

> 项目：高昌举升机 HMI 移植（方案二 Dark Industrial）
> 目标平台：ESP32-S3 (PSRAM 8MB+)，当前验证环境：PC 仿真器 (lv_port_pc_vscode)
> LVGL 版本：v9.3.0-dev
> 屏幕分辨率：1024×600

---

## 一、项目概述

将 HTML/CSS 原型（方案二：左侧导航栏 + Dark Industrial 绿色主题）完整移植为 LVGL v9 C 语言组件库。所有中文文本替换为英文，使用 LVGL 原生 Grid/Flex 布局、原生键盘、全局样式对象。

---

## 二、目录结构

```
main/UI/
├── ui.h                    # 全局入口头文件
├── ui.c                    # 核心实现（初始化 + 页面管理 + 导航栏 + Header + 键盘）
├── pages/
│   ├── ui_page_boot.c/.h   # 登录页
│   ├── ui_page_dash.c/.h   # 仪表盘页（Grid 布局）
│   ├── ui_page_ctrl.c/.h   # 控制面板页
│   ├── ui_page_set.c/.h    # 设置页
│   └── ui_page_about.c/.h  # 关于页
├── components/
│   ├── ui_comp_btn.c/.h    # 按钮组件（App卡片按钮、控制按钮、设置行）
│   ├── ui_comp_msgbox.c/.h # 弹窗组件（确认/取消对话框）
│   └── ui_comp_header.c/.h # 顶部状态栏组件（时钟/状态更新）
├── fonts/
│   └── ui_font.h           # 字体宏定义（16个宏，统一管理所有字体）
├── images/                 # 预留图片目录（当前为空）
└── utils/
    ├── ui_theme.c/.h       # 调色板 + 9个全局样式对象
    └── ui_events.c/.h      # 事件回调 + 回调注册表
```

---

## 三、调色板（Dark Industrial 主题色）

| 宏名 | 十六进制 | 用途 | 对应 CSS 变量 |
|------|----------|------|--------------|
| `UI_COLOR_BG` | `#151515` | 全局背景色 | `--COLOR_BG_GLOBAL` |
| `UI_COLOR_CARD` | `#242529` | 卡片/面板背景色 | `--COLOR_BG_CARD` |
| `UI_COLOR_NAV` | `#242529` | 侧边栏背景色 | 侧边栏专用 |
| `UI_COLOR_ACCENT` | `#B1D873` | 主强调色（绿色）| `--COLOR_ACCENT` |
| `UI_COLOR_TEXT` | `#EAEAEA` | 主文本颜色 | `--COLOR_TEXT_PRI` |
| `UI_COLOR_TEXT_DIM` | `#8A8D93` | 次要文本颜色 | `--COLOR_TEXT_SEC` |
| `UI_COLOR_BORDER` | `#3A4556` | 边框/分割线颜色 | `--COLOR_BORDER` |
| `UI_COLOR_WARNING` | `#FFB020` | 警告色 | `--COLOR_WARNING` |
| `UI_COLOR_SUCCESS` | `#00C853` | 成功/在线色 | `--COLOR_SUCCESS` |
| `UI_COLOR_ERROR` | `#FF5252` | 错误/危险色 | `--COLOR_ERROR` |
| `UI_COLOR_INPUT_BG` | `#151515` | 输入框背景色 | 输入控件专用 |
| `UI_COLOR_BTN_SEC` | `#222222` | 次要按钮背景色 | 次要按钮专用 |

辅助色：

| 宏名 | 十六进制 | 用途 |
|------|----------|------|
| `UI_COLOR_OVERLAY` | `#000000` | 弹窗遮罩层背景 |
| `UI_COLOR_ACCENT_10` | `#1AB1D8` | 强调色悬停近似色 |

---

## 四、布局常量

| 宏名 | 值 | 说明 |
|------|----|------|
| `UI_NAV_WIDTH` | 200 | 侧边栏宽度（像素）|
| `UI_HEADER_HEIGHT` | 60 | 顶部状态栏高度（像素）|
| `UI_RADIUS_S` | 8 | 小圆角半径 |
| `UI_RADIUS_M` | 16 | 中圆角半径 |
| `UI_GAP` | 16 | 组件间距 |
| `UI_PAD` | 12 | 组件内边距 |

---

## 五、全局样式对象

9 个全局 `lv_style_t` 对象，在 `ui_theme_init()` 中初始化，控件通过 `lv_obj_add_style()` 挂载。

### 5.1 样式对象一览

| 样式名 | 定义位置 | 作用 | 关键属性 |
|--------|---------|------|---------|
| `style_card` | `ui_theme.c` 第53行 | 卡片/面板通用样式 | 圆角16、无描边、内边距14、间距8 |
| `style_btn_primary` | `ui_theme.c` 第62行 | 主操作按钮 | 绿色背景`#B1D873`、深色文字、圆角16、阴影、缩放过渡动画 |
| `style_btn_secondary` | `ui_theme.c` 第79行 | 次要按钮 | 深灰背景`#222222`、暗淡文字、圆角16、过渡动画 |
| `style_text_primary` | `ui_theme.c` 第91行 | 主要文本 | 白色文字`#EAEAEA`、14号字体 |
| `style_text_dim` | `ui_theme.c` 第97行 | 次要文本 | 暗淡文字`#8A8D93`、12号字体 |
| `style_input` | `ui_theme.c` 第103行 | 输入框 | 黑色背景、1px描边、圆角8、14号字体 |
| `style_nav_btn` | `ui_theme.c` 第115行 | 导航按钮默认态 | 4%白色半透明背景、暗淡文字、圆角16、高度52px |
| `style_nav_btn_active` | `ui_theme.c` 第128行 | 导航按钮选中态 | 绿色背景`#B1D873`、深色文字、圆角16、高度52px |
| `style_status_item` | `ui_theme.c` 第141行 | 状态列表行 | 2%白色半透明背景、圆角8、无描边 |

### 5.2 过渡动画描述符

| 过渡名 | 作用 | 动画时长 | 属性 |
|--------|------|---------|------|
| `trans_btn` | 按钮状态过渡 | 200ms ease-out | 背景色、X/Y缩放、阴影宽度 |
| `trans_nav` | 导航按钮状态过渡 | 200ms ease-out | 背景色、文字颜色 |

---

## 六、字体系统

所有字体通过 `ui_font.h` 中的宏定义管理，UI 代码**禁止**直接使用硬编码字体指针。更换 CJK 字体时只需修改此文件。

| 宏名 | 当前映射 | 用途 |
|------|---------|------|
| `UI_FONT_PRIMARY` | `&lv_font_montserrat_14` | 通用正文文本 |
| `UI_FONT_PRIMARY_M` | `&lv_font_montserrat_16` | 中号正文文本 |
| `UI_FONT_DISPLAY_L` | `&lv_font_montserrat_42` | 超大显示数值（如立柱高度）|
| `UI_FONT_DISPLAY_M` | `&lv_font_montserrat_24` | 中号标题/关于页主标题 |
| `UI_FONT_DISPLAY_S` | `&lv_font_montserrat_20` | 小号标题/Header品牌 |
| `UI_FONT_NAV` | `&lv_font_montserrat_16` | 导航按钮文字 |
| `UI_FONT_HEADER` | `&lv_font_montserrat_14` | Header型号标签 |
| `UI_FONT_CARD_TITLE` | `&lv_font_montserrat_14` | 卡片标题 |
| `UI_FONT_CARD_VALUE` | `&lv_font_montserrat_42` | 卡片大数值（Dashboard）|
| `UI_FONT_CARD_UNIT` | `&lv_font_montserrat_14` | 卡片单位文字 |
| `UI_FONT_BTN` | `&lv_font_montserrat_16` | 按钮文字 |
| `UI_FONT_INPUT` | `&lv_font_montserrat_14` | 输入框文字 |
| `UI_FONT_SETTING` | `&lv_font_montserrat_14` | 设置项文字 |
| `UI_FONT_SMALL` | `&lv_font_montserrat_12` | 小号文字（状态值、说明）|
| `UI_FONT_TINY` | `&lv_font_montserrat_10` | 极小文字（预留）|
| `UI_FONT_KEYBOARD` | `&lv_font_montserrat_14` | 键盘文字 |

---

## 七、页面管理器

### 7.1 页面枚举

```c
typedef enum {
    UI_PAGE_BOOT = 0,   // 登录页
    UI_PAGE_DASH,       // 仪表盘
    UI_PAGE_CTRL,       // 控制面板
    UI_PAGE_SET,        // 设置
    UI_PAGE_ABOUT,      // 关于
    UI_PAGE_COUNT       // 总数（不可用作页面ID）
} ui_page_id_t;
```

### 7.2 UI 管理器结构体

```c
typedef struct {
    ui_page_id_t    current_page;       // 当前活跃页面ID
    lv_obj_t *      nav;                // 左侧导航栏容器
    lv_obj_t *      content_area;       // 右侧内容区容器
    lv_obj_t *      keyboard;           // 全局隐藏键盘
    lv_obj_t *      header;             // 顶部状态栏
    lv_obj_t *      nav_btns[4];        // 4个导航按钮（DASH/CTRL/SET/ABOUT）
} ui_manager_t;
```

- 全局唯一实例 `s_mgr`，定义在 `ui.c` 第13行
- 通过 `ui_get_manager()` 获取只读指针

### 7.3 页面创建函数指针数组

```c
static const page_create_fn s_page_creators[UI_PAGE_COUNT] = {
    [UI_PAGE_BOOT]  = ui_page_boot_create,
    [UI_PAGE_DASH]  = ui_page_dash_create,
    [UI_PAGE_CTRL]  = ui_page_ctrl_create,
    [UI_PAGE_SET]   = ui_page_set_create,
    [UI_PAGE_ABOUT] = ui_page_about_create,
};
```

定义在 `ui.c` 第26行。页面按需创建/销毁，节省 ESP32-S3 内存。

### 7.4 页面切换流程 `ui_switch_page()`

定义在 `ui.c` 第214行，流程：

1. 检查目标页面是否与当前页面相同 → 相同则直接返回
2. 检查页面ID是否合法（0 ~ UI_PAGE_COUNT-1）
3. 隐藏键盘（`ui_keyboard_hide`）
4. 销毁旧页面内容（`lv_obj_delete(s_current_content)`）
5. 调用页面创建函数（`s_page_creators[id]()`）
6. 设置父对象为 content_area（`lv_obj_set_parent`）
7. 更新导航按钮选中状态（遍历4个按钮，设置/清除 `LV_STATE_CHECKED`）
8. 记录当前页面ID
9. 触发 `UI_CB_NAV_CHANGED` 回调

---

## 八、屏幕布局架构

### 8.1 整体布局图

```
┌──────────┬──────────────────────────────────────────────┐
│          │  Header (60px, 品牌/型号/状态/时钟)           │
│          ├──────────────────────────────────────────────┤
│  Sidebar │                                              │
│  (200px) │  Content Area (按需加载页面内容)               │
│          │                                              │
│  4个     ├──────────────────────────────────────────────┤
│  导航按钮│  Keyboard (默认隐藏, 聚焦textarea时弹出)       │
└──────────┴──────────────────────────────────────────────┘
```

### 8.2 布局实现细节

屏幕（Screen）使用 `LV_LAYOUT_FLEX` + `LV_FLEX_FLOW_ROW` 水平分割：
- **左侧**：`create_sidebar()` 创建的导航栏，固定 200px 宽
- **右侧**：`lv_obj_create(scr)` 创建的右面板，`lv_flex_grow=1` 自适应宽度

右面板使用 `LV_FLEX_FLOW_COLUMN` 垂直分割为三部分：
1. **Header**（60px 固定高度）— `create_header()` 创建
2. **Content Area**（`flex_grow=1` 自适应高度）— 页面内容加载区
3. **Keyboard**（默认隐藏，`LV_SIZE_CONTENT` 自适应高度）— `create_keyboard()` 创建

---

## 九、系统启动流程 `ui_init()`

定义在 `ui.c` 第283行，启动顺序：

1. `ui_theme_init()` — 初始化9个全局样式对象
2. `memset(&s_mgr, 0, ...)` — 清零管理器
3. 设置屏幕背景色(`#151515`) — Flex Row 布局
4. `create_sidebar(scr)` — 创建左侧导航栏
5. 创建右侧面板 — Flex Column 布局
6. `create_header(right)` — 创建顶部状态栏
7. 创建 `content_area` — 页面内容容器
8. `create_keyboard(right)` — 创建全局隐藏键盘
9. `ui_switch_page(UI_PAGE_BOOT)` — 显示登录页

---

## 十、侧边栏导航详解

**创建函数**：`create_sidebar()` — `ui.c` 第42行
**父对象**：Screen
**布局**：`LV_FLEX_FLOW_COLUMN`，垂直排列
**背景色**：`UI_COLOR_NAV (#242529)`
**右侧边框**：1px `UI_COLOR_BORDER (#3A4556)`

### 10.1 四个导航按钮

| 按钮序号 | 图标 | 文字 | 目标页面 | 事件回调 |
|---------|------|------|---------|---------|
| 0 | `LV_SYMBOL_HOME` | "Dashboard" | `UI_PAGE_DASH` | `ui_event_nav_clicked` |
| 1 | `LV_SYMBOL_EDIT` | "Control" | `UI_PAGE_CTRL` | `ui_event_nav_clicked` |
| 2 | `LV_SYMBOL_SETTINGS` | "Settings" | `UI_PAGE_SET` | `ui_event_nav_clicked` |
| 3 | `LV_SYMBOL_WARNING` | "About" | `UI_PAGE_ABOUT` | `ui_event_nav_clicked` |

### 10.2 每个按钮的创建逻辑（ui.c 第74-98行循环）

1. `lv_btn_create(nav)` — 创建按钮
2. `lv_obj_add_style(&style_nav_btn)` — 挂载默认样式
3. `lv_obj_add_style(&style_nav_btn_active, LV_STATE_CHECKED)` — 挂载选中态样式
4. `LV_OBJ_FLAG_CHECKABLE` — 设置为可选中（点击切换 CHECKED 状态）
5. 内部 Flex Row 布局：图标(label) + 文字(label) 水平排列
6. `lv_obj_add_event_cb(ui_event_nav_clicked)` — 注册点击事件
7. 事件 `user_data` 存储目标页面ID — 点击时调用 `ui_switch_page()`
8. 按钮指针保存到 `s_mgr.nav_btns[i]`

**选中状态切换**：通过 `LV_OBJ_FLAG_CHECKABLE` 实现，`ui_switch_page()` 中手动调用 `lv_obj_add_state/clear_state` 控制高亮。

---

## 十一、顶部状态栏（Header）详解

**创建函数**：`create_header()` — `ui.c` 第104行
**父对象**：右侧面板（right）
**尺寸**：100%宽 × 60px高
**布局**：`LV_FLEX_FLOW_ROW`，`SPACE_BETWEEN` 两端对齐

### 11.1 左侧品牌区

| 子控件 | 类型 | 内容 | 样式 |
|--------|------|------|------|
| brand | label | "GAOCHANG" | `UI_FONT_DISPLAY_S`（20号），白色 |
| model | label | "OMCN-4.0T" | `UI_FONT_HEADER`（14号），绿色文字 + 10%绿色背景 + 圆角8 |

### 11.2 右侧状态区

| 子控件 | 类型 | 内容 | 样式 |
|--------|------|------|------|
| sys_tag | label | `LV_SYMBOL_BULLET + " System OK"` | `UI_FONT_SMALL`（12号），绿色文字，4%白色背景 |
| link_tag | label | `LV_SYMBOL_WIFI + " Connected"` | `UI_FONT_SMALL`（12号），黄色文字，4%白色背景 |
| clock | label | "--:--:--" | `UI_FONT_PRIMARY_M`（16号），白色文字，6%白色背景 |

**时钟更新**：需在外部定时调用 `ui_header_set_clock("HH:MM:SS")`，定义在 `ui_comp_header.c`。通过遍历 Header 子控件找到右侧区域的最后一个子控件（时钟 label）进行文本更新。

---

## 十二、键盘系统详解

**创建函数**：`create_keyboard()` — `ui.c` 第194行
**父对象**：右侧面板（right）
**初始状态**：`LV_OBJ_FLAG_HIDDEN`（隐藏）
**布局**：`lv_pct(100)` 宽 × `LV_SIZE_CONTENT` 高，底部对齐

### 12.1 键盘 API

| 函数 | 定义位置 | 功能 |
|------|---------|------|
| `ui_keyboard_show_num(ta)` | `ui.c` 第255行 | 切换为数字模式 + 绑定textarea + 取消隐藏 + 提到前台 |
| `ui_keyboard_show_text(ta)` | `ui.c` 第264行 | 切换为文本模式 + 绑定textarea + 取消隐藏 + 提到前台 |
| `ui_keyboard_hide()` | `ui.c` 第273行 | 解除textarea绑定 + 隐藏 |

### 12.2 键盘事件绑定

| 事件 | 回调 | 行为 |
|------|------|------|
| `LV_EVENT_READY`（确认键）| `ui_event_kb_ready` | 调用 `ui_keyboard_hide()` |
| `LV_EVENT_CANCEL`（取消键）| `ui_event_kb_ready` | 调用 `ui_keyboard_hide()` |

### 12.3 键盘弹出触发

任何 `lv_textarea` 控件绑定 `ui_event_ta_focused` 回调后，获得焦点时自动弹出键盘。回调逻辑（`ui_events.c` 第52行）：

1. 检查键盘是否存在
2. 检查textarea的`accepted_chars`是否包含`'0'`（判断是否数字输入）
3. 数字输入 → `LV_KEYBOARD_MODE_NUMBER`
4. 文本输入 → `LV_KEYBOARD_MODE_TEXT_LOWER`
5. 绑定textarea到键盘
6. 取消隐藏 + 移到前台

---

## 十三、事件系统详解

### 13.1 事件回调函数

| 回调名 | 定义位置 | 触发场景 | 行为 |
|--------|---------|---------|------|
| `ui_event_nav_clicked` | `ui_events.c` 第35行 | 导航按钮点击 | 从 `user_data` 取出页面ID，调用 `ui_switch_page()` |
| `ui_event_ctrl_toggle` | `ui_events.c` 第44行 | 控制按钮点击 | 触发 `UI_CB_CTRL_ACTION` 回调 |
| `ui_event_ta_focused` | `ui_events.c` 第52行 | 输入框获得焦点 | 弹出键盘（见12.3节）|
| `ui_event_kb_ready` | `ui_events.c` 第74行 | 键盘确认/取消 | 隐藏键盘 |
| `ui_event_msgbox_confirm` | `ui_events.c` 第83行 | 弹窗确认按钮 | 删除遮罩层 |
| `ui_event_msgbox_cancel` | `ui_events.c` 第89行 | 弹窗取消按钮 | 删除遮罩层 |

### 13.2 回调注册表

硬件/业务层可通过 `ui_register_callback()` 注册回调，UI 层通过 `ui_invoke_callback()` 触发。

| 回调ID | 枚举值 | 触发时机 | 用途 |
|--------|--------|---------|------|
| `UI_CB_LOGIN` | 0 | 登录按钮点击 | 对接NVS账号密码验证 |
| `UI_CB_NAV_CHANGED` | 1 | 页面切换完成 | 通知硬件层当前页面 |
| `UI_CB_CTRL_ACTION` | 2 | 控制按钮点击 | 执行举升/下降等动作 |
| `UI_CB_SETTING_CHANGED` | 3 | 设置项修改 | 保存参数到NVS |

---

## 十四、登录页（Boot）详解

**文件**：`ui_page_boot.c` / `ui_page_boot.h`
**创建函数**：`ui_page_boot_create()` — 第43行
**布局**：全屏 Flex 居中，暗色背景 `UI_COLOR_BG`

### 14.1 页面结构图

```
┌─────────────────────────────────────────────┐
│                                             │
│          ┌──── 登录卡片 (380px宽) ────┐     │
│          │                             │     │
│          │  ⚡ Logo图标 (42号字体, 绿色) │     │
│          │                             │     │
│          │  "Welcome"  标题 (20号字体)   │     │
│          │                             │     │
│          │  "Please login to continue"  │     │
│          │   副标题 (14号字体, 暗淡色)    │     │
│          │                             │     │
│          │  ┌─────────────────────┐    │     │
│          │  │ Username 输入框      │    │     │
│          │  └─────────────────────┘    │     │
│          │                             │     │
│          │  ┌─────────────────────┐    │     │
│          │  │ •••••• 密码输入框     │    │     │
│          │  └─────────────────────┘    │     │
│          │                             │     │
│          │  [错误提示] (默认隐藏, 红色)   │     │
│          │                             │     │
│          │  ┌─────────────────────┐    │     │
│          │  │      Login 按钮      │    │     │
│          │  └─────────────────────┘    │     │
│          │                             │     │
│          └─────────────────────────────┘     │
│                                             │
└─────────────────────────────────────────────┘
```

### 14.2 控件清单

| 序号 | 控件 | 类型 | 创建位置 | 关键属性 |
|------|------|------|---------|---------|
| 1 | page | `lv_obj_create(NULL)` | 第46行 | 无父对象（稍后由 `ui_switch_page` 设置），全屏，居中Flex |
| 2 | card | `lv_obj_create(page)` | 第55行 | `style_card`，380px宽，36px内边距，垂直Flex |
| 3 | logo | `lv_label_create(card)` | 第68行 | `LV_SYMBOL_CHARGE`，42号字体，绿色 |
| 4 | title | `lv_label_create(card)` | 第74行 | "Welcome"，20号字体，白色 |
| 5 | subtitle | `lv_label_create(card)` | 第80行 | "Please login to continue"，14号字体，暗淡色 |
| 6 | user_ta | `lv_textarea_create(card)` | 第86行 | `style_input`，单行，placeholder="Username"，绑定 `ui_event_ta_focused` |
| 7 | pass_ta | `lv_textarea_create(card)` | 第95行 | `style_input`，单行，密码模式，placeholder="Password"，绑定 `ui_event_ta_focused` |
| 8 | s_error_label | `lv_label_create(card)` | 第105行 | 默认隐藏，红色文字，12号字体 |
| 9 | login_btn | `lv_btn_create(card)` | 第112行 | `style_btn_primary`，100%宽×40px，绑定 `boot_login_event` |

### 14.3 登录事件

**回调**：`boot_login_event()` — 第20行
**当前行为**：直接调用 `ui_switch_page(UI_PAGE_DASH)` 跳转到仪表盘，**未实现真实验证**。
**TODO**：对接 `ui_invoke_callback(UI_CB_LOGIN, ...)` 与 ESP32 NVS 存储验证。

---

## 十五、仪表盘页（Dashboard）详解

**文件**：`ui_page_dash.c` / `ui_page_dash.h`
**创建函数**：`ui_page_dash_create()` — 第101行
**布局**：4列×2行 Grid 布局

### 15.1 Grid 定义

```c
col_dsc[] = {FR(1), FR(1), FR(1), FR(1), TEMPLATE_LAST}  // 4列等宽
row_dsc[] = {FR(1), FR(1), TEMPLATE_LAST}                 // 2行等高
```

### 15.2 页面结构图

```
┌────────────────┬──────────┬──────────┬──────────────────┐
│                │          │          │                  │
│   Card A       │          │ Card B   │   Card C         │
│   立柱高度      │          │ 高度差    │   报警+状态       │
│   (跨2列,1行)  │          │ (1列,1行) │   (1列,跨2行)    │
│                │          │          │                  │
├────────┬───────┼──────────┤          │                  │
│ Card D │ Card E│  Card F  │          │                  │
│ 手动控制│ 参数  │ 快捷面板  │          │                  │
│(1列1行)│(1列1行)│ (1列1行) │          │                  │
└────────┴───────┴──────────┴──────────┘
```

### 15.3 Grid 坐标映射

| 卡片 | 列起始 | 列跨度 | 行起始 | 行跨度 | 内容 |
|------|--------|--------|--------|--------|------|
| Card A | 0 | 2 | 0 | 1 | 立柱高度 |
| Card B | 2 | 1 | 0 | 1 | 高度差 |
| Card C | 3 | 1 | 0 | 2 | 报警+状态（跨2行）|
| Card D | 0 | 1 | 1 | 1 | 手动控制 |
| Card E | 1 | 1 | 1 | 1 | 参数 |
| Card F | 2 | 1 | 1 | 1 | 快捷面板 |

### 15.4 Card A — 立柱高度

**创建位置**：`ui_page_dash.c` 第122行
**Grid 位置**：col=0, span=2, row=0, span=1

```
┌─────────────────────────────────────────────┐
│  "Pillar Height"  (暗淡色, 14号, 卡片标题)    │
│                                              │
│    ┌──────────────┐   ┌──────────────┐       │
│    │  "Pillar 1"  │   │  "Pillar 2"  │       │
│    │   "1856"     │   │   "1842"     │       │
│    │    "mm"      │   │    "mm"      │       │
│    └──────────────┘   └──────────────┘       │
└─────────────────────────────────────────────┘
```

**内部结构**：
- `create_card()` 创建卡片外壳 + 标题（第44-66行辅助函数）
- 内部 `heights` 容器：Flex Row 居中，内含两个 `create_height_value()` 块
- 每个 `create_height_value()` 辅助函数（第69-99行）创建：标签(14号暗淡) + 数值(42号白色) + 单位(14号暗淡)

**`create_height_value()` 调用参数**：

| 调用 | label_text | value | unit |
|------|-----------|-------|------|
| 第1次 | "Pillar 1" | "1856" | "mm" |
| 第2次 | "Pillar 2" | "1842" | "mm" |

### 15.5 Card B — 高度差

**创建位置**：`ui_page_dash.c` 第138行
**Grid 位置**：col=2, span=1, row=0, span=1

```
┌──────────────────┐
│  "Height Diff"   │
│                  │
│     "14"         │  ← 42号字体, 绿色(UI_COLOR_SUCCESS)
│     "mm"         │  ← 暗淡色
└──────────────────┘
```

Flex 居中排列，差值标签（42号绿色）+ 单位标签。

### 15.6 Card C — 报警+状态

**创建位置**：`ui_page_dash.c` 第151行
**Grid 位置**：col=3, span=1, row=0, span=2（跨2行）

```
┌──────────────────────┐
│  "Alarm · Status"    │  ← 卡片标题
│                      │
│  ✓ "No Alarm"        │  ← 报警图标+文字, 绿色
│                      │
│  ┌─────────────────┐ │
│  │ System Status  Normal │ │  ← style_status_item
│  ├─────────────────┤ │
│  │ Connection    Linked  │ │
│  ├─────────────────┤ │
│  │ Run Mode     Standby  │ │
│  ├─────────────────┤ │
│  │ Today Jobs  128 times │ │
│  └─────────────────┘ │
└──────────────────────┘
```

**内部控件**：

| 序号 | 控件 | 类型 | 内容 | 样式 |
|------|------|------|------|------|
| 1 | alarm_icon | label | `LV_SYMBOL_OK` | 20号字体，绿色 |
| 2 | alarm_text | label | "No Alarm" | 14号字体，绿色 |
| 3-6 | srow × 4 | `lv_obj_create` | 状态列表行 | `style_status_item`，Flex Row 两端对齐 |

状态列表数据：

| 标签 | 值 |
|------|-----|
| "System Status" | "Normal" |
| "Connection" | "Linked" |
| "Run Mode" | "Standby" |
| "Today Jobs" | "128 times" |

### 15.7 Card D — 手动控制

**创建位置**：`ui_page_dash.c` 第189行
**Grid 位置**：col=0, span=1, row=1, span=1

```
┌──────────────────┐
│  "Manual Ctrl"   │  ← 卡片标题
│                  │
│  ┌────────────┐  │
│  │  ▼ Min Clear│  │  ← ui_comp_ctrl_btn_create()
│  ├────────────┤  │
│  │  ▲ Max Height│ │  ← ui_comp_ctrl_btn_create()
│  └────────────┘  │
└──────────────────┘
```

- 内部 `d_btns` 容器：Flex Column，包含2个 `ui_comp_ctrl_btn_create()` 小按钮
- **按钮1**：图标=`LV_SYMBOL_DOWN`，文字="Min Clear"
- **按钮2**：图标=`LV_SYMBOL_UP`，文字="Max Height"

### 15.8 Card E — 参数

**创建位置**：`ui_page_dash.c` 第204行
**Grid 位置**：col=1, span=1, row=1, span=1

- **按钮1**：图标=`LV_SYMBOL_IMAGE`，文字="ID Setup"
- **按钮2**：图标=`LV_SYMBOL_SHUFFLE`，文字="Start Height"

### 15.9 Card F — 快捷面板

**创建位置**：`ui_page_dash.c` 第219行
**Grid 位置**：col=2, span=1, row=1, span=1

- **按钮1**：图标=`LV_SYMBOL_WIFI`，文字="Link Setup"
- **按钮2**：图标=`LV_SYMBOL_LIST`，文字="Auto Level"

---

## 十六、控制面板页（Control）详解

**文件**：`ui_page_ctrl.c` / `ui_page_ctrl.h`
**创建函数**：`ui_page_ctrl_create()` — 第17行
**布局**：Flex Row Wrap 居中排列

### 16.1 页面结构图

```
┌──────────────────────────────────────────────────┐
│                                                  │
│   ┌──────────┐  ┌──────────┐  ┌──────────┐      │
│   │  ▼       │  │  ▲       │  │  🖷      │      │
│   │ Min Clear│  │Max Height│  │ ID Setup │      │
│   └──────────┘  └──────────┘  └──────────┘      │
│                                                  │
│   ┌──────────┐  ┌──────────┐  ┌──────────┐      │
│   │  🔀      │  │  📶      │  │  📋      │      │
│   │Start Ht  │  │Link Setup│  │Auto Level│      │
│   └──────────┘  └──────────┘  └──────────┘      │
│                                                  │
└──────────────────────────────────────────────────┘
```

### 16.2 控件清单

6个 App Card 按钮，通过 `ui_comp_app_btn_create()` 创建，每个 220×140 像素。

| 序号 | 图标 | 文字 | user_data |
|------|------|------|-----------|
| 0 | `LV_SYMBOL_DOWN` | "Min Clear" | 0 |
| 1 | `LV_SYMBOL_UP` | "Max Height" | 1 |
| 2 | `LV_SYMBOL_IMAGE` | "ID Setup" | 2 |
| 3 | `LV_SYMBOL_SHUFFLE` | "Start Height" | 3 |
| 4 | `LV_SYMBOL_WIFI` | "Link Setup" | 4 |
| 5 | `LV_SYMBOL_LIST` | "Auto Level" | 5 |

- `user_data` 存储动作ID，用于后续事件回调区分
- **当前未绑定点击事件回调**

---

## 十七、设置页（Settings）详解

**文件**：`ui_page_set.c` / `ui_page_set.h`
**创建函数**：`ui_page_set_create()` — 第26行
**布局**：Flex Column，可滚动

### 17.1 页面结构图

```
┌──────────────────────────────────────────────────┐
│  ┌────────────────────────────────────────────┐  │
│  │  ▼  Language          [English ▼]  下拉框  │  │
│  ├────────────────────────────────────────────┤  │
│  │  🖷 Brightness           80%      可点击   │  │
│  ├────────────────────────────────────────────┤  │
│  │  ⚠ Alarm Threshold       20       可点击   │  │
│  ├────────────────────────────────────────────┤  │
│  │  ▶ Version            V1.2.0    只读       │  │
│  ├────────────────────────────────────────────┤  │
│  │  🖷 Serial No.    GC-2024-00128  只读       │  │
│  ├────────────────────────────────────────────┤  │
│  │  📶 MAC Address  A1:B2:C3:D4:E5:F6 只读    │  │
│  ├────────────────────────────────────────────┤  │
│  │  ⚡ Admin Management   [Add] 按钮          │  │
│  └────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────┘
```

### 17.2 设置行数据

| 序号 | 图标 | 标签 | 默认值 | 值控件类型 | 可点击 | 点击行为 |
|------|------|------|--------|-----------|--------|---------|
| 0 | `LV_SYMBOL_DOWN` | "Language" | "English" | `lv_dropdown` | 否 | 下拉选择 |
| 1 | `LV_SYMBOL_IMAGE` | "Brightness" | "80%" | label(样式化) | 是 | 弹出数字键盘 |
| 2 | `LV_SYMBOL_WARNING` | "Alarm Threshold" | "20" | label(样式化) | 是 | 弹出数字键盘 |
| 3 | `LV_SYMBOL_NEXT` | "Version" | "V1.2.0" | label | 否 | 只读 |
| 4 | `LV_SYMBOL_IMAGE` | "Serial No." | "GC-2024-00128" | label | 否 | 只读 |
| 5 | `LV_SYMBOL_WIFI` | "MAC Address" | "A1:B2:C3:D4:E5:F6" | label | 否 | 只读 |
| 6 | `LV_SYMBOL_CHARGE` | "Admin Management" | — | `lv_btn` | — | 添加管理员按钮 |

### 17.3 可点击值控件详解

**序号1/2（亮度/报警阈值）**的可点击值控件创建逻辑（第88-101行）：

1. `lv_label_create(NULL)` — 无父对象创建
2. 设置绿色文字(`UI_COLOR_ACCENT`)
3. 设置黑色背景 + 1px描边 + 圆角8
4. 设置内边距 8px垂直/14px水平
5. `LV_OBJ_FLAG_CLICKABLE` — 可点击
6. 绑定 `setting_input_click_cb` — 点击时弹出数字键盘

**`setting_input_click_cb`**（第20行）：调用 `ui_keyboard_show_num(ta)` 弹出数字键盘。

> ⚠️ **已知问题**：键盘确认输入后，值未回写到显示标签上，需完善闭环。

### 17.4 语言下拉框详解

**序号0（Language）**的值控件创建逻辑（第68-77行）：

1. `lv_dropdown_create(NULL)` — 无父对象创建
2. 设置选项："English\nChinese"
3. 设置黑色背景、白色文字、1px描边、圆角8
4. 宽度110px
5. 通过 `ui_comp_setting_row_create()` 的 `lv_obj_set_parent` 移入设置行右侧

### 17.5 管理员按钮详解

**序号6（Admin Management）**的值控件创建逻辑（第79-86行）：

1. `lv_btn_create(NULL)` — 无父对象创建
2. `style_btn_primary` 样式
3. 内部 label "Add"，12号字体
4. 无事件绑定（TODO：打开管理员管理弹窗）

---

## 十八、关于页（About）详解

**文件**：`ui_page_about.c` / `ui_page_about.h`
**创建函数**：`ui_page_about_create()` — 第28行
**布局**：Flex Column，可滚动

### 18.1 页面结构图

```
┌──────────────────────────────────────────────────┐
│           "Screw Lift System"                     │  ← 标题区
│           "Model: OMCN-4.0T"                      │
│                                                   │
│  ┌────────────────────────────────────────────┐  │
│  │  "Product Overview"                        │  │  ← 描述卡片
│  │  High-precision screw drive with           │  │
│  │  dual-pillar synchronous lifting...        │  │
│  └────────────────────────────────────────────┘  │
│                                                   │
│  ┌────────────────────────────────────────────┐  │
│  │  "Key Features"                            │  │  ← 特性卡片
│  │  ┌──────────────┐  ┌──────────────┐       │  │
│  │  │ ▲ Dual-Pillar │  │ 👁 Real-Time │       │  │
│  │  │   Sync Lift   │  │   Height Mon │       │  │
│  │  ├──────────────┤  ├──────────────┤       │  │
│  │  │ 📋 Auto-Level │  │ ⚡ Overload  │       │  │
│  │  │   Control     │  │   Safety     │       │  │
│  │  ├──────────────┤  ├──────────────┤       │  │
│  │  │ 📶 Linked     │  │ ⚙ Flexible  │       │  │
│  │  │   Co-op Op    │  │   Param Conf │       │  │
│  │  └──────────────┘  └──────────────┘       │  │
│  └────────────────────────────────────────────┘  │
│                                                   │
│  "Firmware V1.2.0 | Serial GC-2024-00128"        │  ← 版本页脚
└──────────────────────────────────────────────────┘
```

### 18.2 页面控件清单

| 序号 | 控件 | 类型 | 创建位置 | 内容 | 样式 |
|------|------|------|---------|------|------|
| 1 | page | `lv_obj_create(NULL)` | 第30行 | 全屏容器 | 透明背景 |
| 2 | scroll | `lv_obj_create(page)` | 第37行 | 可滚动容器 | Flex Column |
| 3 | title_area | `lv_obj_create(scroll)` | 第48行 | 标题区 | Flex Column 居中 |
| 4 | main_title | `lv_label_create(title_area)` | 第60行 | "Screw Lift System" | 24号字体，绿色 |
| 5 | model_lbl | `lv_label_create(title_area)` | 第65行 | "Model: OMCN-4.0T" | 14号字体，暗淡色 |
| 6 | desc_card | `lv_obj_create(scroll)` | 第71行 | 描述卡片 | `style_card` |
| 7 | desc_title | `lv_label_create(desc_card)` | 第80行 | "Product Overview" | `style_text_dim` |
| 8 | desc_text | `lv_label_create(desc_card)` | 第84行 | 产品描述文本 | 14号字体，白色 |
| 9 | feat_card | `lv_obj_create(scroll)` | 第91行 | 特性卡片 | `style_card` |
| 10 | feat_title | `lv_label_create(feat_card)` | 第101行 | "Key Features" | `style_text_dim` |
| 11 | feat_grid | `lv_obj_create(feat_card)` | 第106行 | 2列3行Grid | Grid 布局 |
| 12-17 | fitem × 6 | `lv_obj_create(feat_grid)` | 第119行 | 特性项 | `style_status_item` |
| 18 | version_lbl | `lv_label_create(scroll)` | 第152行 | 版本号 | 12号字体，暗淡色 |

### 18.3 特性网格数据

2列×3行 Grid 布局，6个特性项：

| 序号 | 图标 | 文字 | Grid 位置 |
|------|------|------|-----------|
| 0 | `LV_SYMBOL_UP` | "Dual-Pillar Sync Lift" | col=0, row=0 |
| 1 | `LV_SYMBOL_EYE_OPEN` | "Real-Time Height Monitor" | col=1, row=0 |
| 2 | `LV_SYMBOL_LIST` | "Auto-Leveling Control" | col=0, row=1 |
| 3 | `LV_SYMBOL_CHARGE` | "Overload Safety" | col=1, row=1 |
| 4 | `LV_SYMBOL_WIFI` | "Linked Co-op Operation" | col=0, row=2 |
| 5 | `LV_SYMBOL_SETTINGS` | "Flexible Param Config" | col=1, row=2 |

### 18.4 特性项内部结构

每个特性项由 `style_status_item` 包裹，内含：
- **图标背景**（32×32 圆角方块）：10% 绿色背景，内居中放置图标 label（绿色）
- **文字标签**：14号字体，白色

---

## 十九、组件库详解

### 19.1 App Card 按钮 `ui_comp_app_btn_create()`

**文件**：`ui_comp_btn.c` 第12行
**用途**：控制面板页的大卡片按钮
**尺寸**：220×140 像素

```
┌──────────────────┐
│                  │
│    ┌──────┐      │  ← 图标背景 (64×64, 20%绿色, 圆角16)
│    │  图标 │      │  ← 图标 (24号字体, 绿色)
│    └──────┘      │
│                  │
│   "Button Text"  │  ← 文字 (16号字体, 暗淡色)
│                  │
└──────────────────┘
```

**样式**：`style_card` + `UI_COLOR_CARD` 背景 + 20px阴影(20%透明度)
**布局**：Flex Column 居中，行间距12px

**创建逻辑**（`ui_comp_btn.c` 第12-52行）：

1. `lv_btn_create(parent)` — 创建按钮
2. 设置 220×140 尺寸
3. 挂载 `style_card` + `UI_COLOR_CARD` 背景 + 阴影
4. Flex Column 居中布局，行间距12px
5. 创建图标背景 `lv_obj_create(btn)` — 64×64，20%绿色，圆角16，Flex居中
6. 创建图标 `lv_label_create(icon_bg)` — 24号字体，绿色
7. 创建文字 `lv_label_create(btn)` — 16号字体，暗淡色

### 19.2 控制按钮 `ui_comp_ctrl_btn_create()`

**文件**：`ui_comp_btn.c` 第57行
**用途**：仪表盘 Card D/E/F 中的小按钮
**尺寸**：100%宽 × 自适应高

```
┌────────────────┐
│     ▲          │  ← 图标 (16号字体, 绿色)
│  Max Height    │  ← 文字 (12号字体, 暗淡色)
└────────────────┘
```

**样式**：8%白色半透明背景 + 圆角8 + 无描边
**布局**：Flex Column 居中，行间距6px，`flex_grow=1`

**创建逻辑**（第57-85行）：

1. `lv_btn_create(parent)` — 创建按钮
2. 设置 100%宽 × `LV_SIZE_CONTENT` 高
3. 8%白色背景 + 圆角8 + 无描边
4. Flex Column 居中，行间距6px，`flex_grow=1`
5. 创建图标 `lv_label_create(btn)` — 16号字体，绿色
6. 创建文字 `lv_label_create(btn)` — 12号字体，暗淡色

### 19.3 设置行 `ui_comp_setting_row_create()`

**文件**：`ui_comp_btn.c` 第90行
**用途**：设置页的每行设置项
**尺寸**：100%宽 × 自适应高

```
┌────────────────────────────────────────────┐
│  ┌────┐                                   │
│  │ ⚙ │  Setting Label        Value Widget  │
│  └────┘                                   │
└────────────────────────────────────────────┘
```

**创建逻辑**（第90-142行）：

1. `lv_obj_create(parent)` — 创建行容器
2. 挂载 `style_card`，无阴影
3. Flex Row，`SPACE_BETWEEN` 两端对齐
4. 创建左侧容器 `left`：Flex Row
   - 图标背景 `icon_bg`：36×36，10%绿色，圆角8，Flex居中
   - 图标 `icon`：14号字体，绿色
   - 文字标签 `lbl`：14号字体，白色
5. 将调用方传入的 `value` 控件通过 `lv_obj_set_parent(value, row)` 移入右侧

> **重要设计**：`value` 控件由调用方创建（因为类型不同：dropdown/label/btn），通过参数传入，`ui_comp_setting_row_create` 只负责将其放到行右侧。

### 19.4 弹窗组件 `ui_comp_msgbox_show()`

**文件**：`ui_comp_msgbox.c` 第9行
**用途**：确认/取消对话框

```
┌────────────────────────────────────────────┐
│          (70%黑色遮罩层, 居中)               │
│     ┌──────────────────────────────┐       │
│     │  ⚠ Title                     │       │
│     │                              │       │
│     │  Message body text...        │       │
│     │                              │       │
│     │            [Cancel] [Confirm]│       │
│     └──────────────────────────────┘       │
└────────────────────────────────────────────┘
```

**创建逻辑**（第9-101行）：

1. 创建遮罩层 `overlay`：100%×100%，70%黑色透明，Flex居中，移到前台
2. 创建对话框 `dialog`：380px宽，`style_card`，垂直Flex
3. 创建标题行：图标(`LV_SYMBOL_WARNING`，20号，黄色) + 标题文字(16号，白色)
4. 创建消息体：14号字体，暗淡色
5. 创建按钮行（Flex Row 右对齐）：
   - 取消按钮：`style_btn_secondary`，文字="Cancel"
   - 确认按钮：`style_btn_primary`，文字="Confirm"
6. 如传入回调则绑定，否则默认回调为 `ui_comp_msgbox_close`（删除遮罩层）

**关闭**：`ui_comp_msgbox_close(overlay)` — `lv_obj_delete(overlay)` 删除整个遮罩层

### 19.5 Header 更新组件 `ui_comp_header`

**文件**：`ui_comp_header.c`
**用途**：更新顶部状态栏内容

| 函数 | 功能 | 实现状态 |
|------|------|---------|
| `ui_header_set_clock(hhmmss)` | 更新时钟文字 | ✅ 已实现（遍历Header子控件定位到时钟label）|
| `ui_header_set_system_status(ok)` | 更新系统状态 | ❌ TODO |
| `ui_header_set_link_status(linked)` | 更新连接状态 | ❌ TODO |

---

## 二十、文本字符串宏定义

所有文本定义在 `ui.h` 第113-141行，统一使用英文（因无 CJK 字体）。

### 20.1 导航文本

| 宏名 | 值 |
|------|-----|
| `UI_TEXT_NAV_DASH` | "Dashboard" |
| `UI_TEXT_NAV_CTRL` | "Control" |
| `UI_TEXT_NAV_SET` | "Settings" |
| `UI_TEXT_NAV_ABOUT` | "About" |

### 20.2 Header 文本

| 宏名 | 值 |
|------|-----|
| `UI_TEXT_BRAND` | "GAOCHANG" |
| `UI_TEXT_MODEL` | "OMCN-4.0T" |
| `UI_TEXT_STATUS_OK` | "System OK" |
| `UI_TEXT_STATUS_ERR` | "Error" |
| `UI_TEXT_LINKED` | "Connected" |
| `UI_TEXT_UNLINKED` | "Disconnected" |

### 20.3 登录页文本

| 宏名 | 值 |
|------|-----|
| `UI_TEXT_BOOT_TITLE` | "Welcome" |
| `UI_TEXT_BOOT_SUBTITLE` | "Please login to continue" |
| `UI_TEXT_USERNAME` | "Username" |
| `UI_TEXT_PASSWORD` | "Password" |
| `UI_TEXT_LOGIN` | "Login" |
| `UI_TEXT_LOGIN_ERR` | "Invalid username or password" |

### 20.4 通用文本

| 宏名 | 值 |
|------|-----|
| `UI_TEXT_CANCEL` | "Cancel" |
| `UI_TEXT_CONFIRM` | "Confirm" |
| `UI_TEXT_APPLY` | "Apply" |
| `UI_TEXT_CLOSE` | "Close" |

---

## 二十一、构建系统集成

### 21.1 CMakeLists.txt 修改

```cmake
# 新增5个头文件搜索路径
include_directories(${PROJECT_SOURCE_DIR}/main/UI)
include_directories(${PROJECT_SOURCE_DIR}/main/UI/pages)
include_directories(${PROJECT_SOURCE_DIR}/main/UI/components)
include_directories(${PROJECT_SOURCE_DIR}/main/UI/fonts)
include_directories(${PROJECT_SOURCE_DIR}/main/UI/utils)

# 自动收集所有 UI 源文件（无需手动列出每个 .c 文件）
file(GLOB_RECURSE UI_SOURCES "${PROJECT_SOURCE_DIR}/main/UI/*.c")
```

### 21.2 SDL_main.c 修改

| 项目 | 旧值 | 新值 |
|------|------|------|
| 分辨率 | `hal_init(320, 480)` | `hal_init(1024, 600)` |
| UI入口 | `create_hover_card()` | `ui_init()` |
| MSVC兼容 | `int SDL_main(...)` | 条件编译 |

MSVC 条件编译：
```c
#if defined(_MSC_VER)
int main(int argc, char **argv)
#else
int SDL_main(int argc, char **argv)
#endif
```

---

## 二十二、项目逻辑思路总览

### 22.1 对象创建层次树

```
lv_screen_active()                          ← ui_init() 第293行
├── nav (侧边栏)                            ← create_sidebar() 第44行
│   ├── nav_btn[0] (Dashboard)              ← 循环 第74行
│   │   ├── icon (LV_SYMBOL_HOME)
│   │   └── label ("Dashboard")
│   ├── nav_btn[1] (Control)
│   ├── nav_btn[2] (Settings)
│   └── nav_btn[3] (About)
│
└── right (右侧面板)                        ← ui_init() 第305行
    ├── header (状态栏)                     ← create_header() 第106行
    │   ├── brand_area (左侧品牌区)
    │   │   ├── brand ("GAOCHANG")
    │   │   └── model ("OMCN-4.0T")
    │   └── status_area (右侧状态区)
    │       ├── sys_tag ("System OK")
    │       ├── link_tag ("Connected")
    │       └── clock ("--:--:--")
    │
    ├── content_area (页面加载区)            ← ui_init() 第321行
    │   └── [当前页面内容]                   ← ui_switch_page() 动态替换
    │       ├── Boot页 → 登录卡片
    │       ├── Dash页 → Grid 6卡片
    │       ├── Ctrl页 → 6个App按钮
    │       ├── Set页  → 7行设置项
    │       └── About页 → 标题+描述+特性+版本
    │
    └── keyboard (全局键盘)                 ← create_keyboard() 第196行
        └── [默认隐藏, 聚焦时弹出]
```

### 22.2 事件流转图

```
用户点击导航按钮
    │
    ▼
ui_event_nav_clicked()                      ← ui_events.c 第35行
    │  从 user_data 取出页面ID
    ▼
ui_switch_page(target_id)                   ← ui.c 第214行
    │  1. 隐藏键盘
    │  2. 销毁旧页面
    │  3. 调用 s_page_creators[id]() 创建新页面
    │  4. 设置父对象到 content_area
    │  5. 更新导航按钮选中状态
    │  6. 触发 UI_CB_NAV_CHANGED 回调
    ▼
新页面显示

---

用户点击输入框
    │
    ▼
ui_event_ta_focused()                       ← ui_events.c 第52行
    │  判断数字/文本模式
    ▼
ui_keyboard_show_num/show_text(ta)          ← ui.c 第255/264行
    │  设置模式 + 绑定textarea + 取消隐藏
    ▼
键盘弹出

---

用户点击键盘确认/取消
    │
    ▼
ui_event_kb_ready()                         ← ui_events.c 第74行
    │
    ▼
ui_keyboard_hide()                          ← ui.c 第273行
    │  解除绑定 + 隐藏
    ▼
键盘收起

---

用户点击弹窗确认/取消
    │
    ▼
ui_event_msgbox_confirm/cancel()            ← ui_events.c 第83/89行
    │
    ▼
ui_comp_msgbox_close(overlay)               ← ui_comp_msgbox.c 第103行
    │  lv_obj_delete(overlay)
    ▼
弹窗关闭
```

### 22.3 样式挂载关系图

```
style_card ───────────→ 所有卡片容器、登录卡片、设置行、弹窗对话框
style_btn_primary ────→ 登录按钮、弹窗确认按钮、管理员Add按钮
style_btn_secondary ──→ 弹窗取消按钮
style_text_primary ───→ (预留，部分控件直接用 set_style)
style_text_dim ───────→ 所有卡片标题标签、弹窗消息体、关于页描述标题/特性标题
style_input ──────────→ 登录页用户名/密码输入框
style_nav_btn ────────→ 导航按钮默认态
style_nav_btn_active ─→ 导航按钮选中态 (LV_STATE_CHECKED)
style_status_item ────→ Dashboard Card C 状态列表行、About页特性项
```

### 22.4 页面与组件调用关系

```
ui_init()
  ├── ui_theme_init()          → 初始化9个全局样式
  ├── create_sidebar()         → 4个 nav_btn (内含 icon + label)
  ├── create_header()          → brand_area + status_area
  ├── create_keyboard()        → lv_keyboard
  └── ui_switch_page(BOOT)
        └── ui_page_boot_create()
              └── card (style_card)
                  ├── logo + title + subtitle (labels)
                  ├── user_ta + pass_ta (style_input + ui_event_ta_focused)
                  ├── s_error_label (hidden)
                  └── login_btn (style_btn_primary + boot_login_event)

ui_switch_page(DASH)
  └── ui_page_dash_create()
        └── grid (LV_LAYOUT_GRID, 4列×2行)
            ├── Card A: create_card() → create_height_value() × 2
            ├── Card B: create_card() → diff_val + diff_unit
            ├── Card C: create_card() → alarm_icon + alarm_text + srow × 4
            ├── Card D: create_card() → ui_comp_ctrl_btn_create() × 2
            ├── Card E: create_card() → ui_comp_ctrl_btn_create() × 2
            └── Card F: create_card() → ui_comp_ctrl_btn_create() × 2

ui_switch_page(CTRL)
  └── ui_page_ctrl_create()
        └── panel (LV_FLEX_FLOW_ROW_WRAP)
            └── ui_comp_app_btn_create() × 6

ui_switch_page(SET)
  └── ui_page_set_create()
        └── panel (LV_FLEX_FLOW_COLUMN)
            └── ui_comp_setting_row_create() × 7
                  ├── row 0: dropdown (语言)
                  ├── row 1: clickable label (亮度, 绑定setting_input_click_cb)
                  ├── row 2: clickable label (报警阈值, 绑定setting_input_click_cb)
                  ├── row 3-5: readonly label
                  └── row 6: btn (管理员, style_btn_primary)

ui_switch_page(ABOUT)
  └── ui_page_about_create()
        └── scroll (LV_FLEX_FLOW_COLUMN)
            ├── title_area → main_title + model_lbl
            ├── desc_card (style_card) → desc_title + desc_text
            ├── feat_card (style_card) → feat_title + feat_grid
            │   └── feat_grid (2列3行 Grid)
            │       └── fitem × 6 (style_status_item)
            │           └── ficon_bg (10%绿色) + ficon + flbl
            └── version_lbl
```