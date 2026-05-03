/**
 * @file lv_conf.h
 * LVGL v9.3.0-dev 配置文件
 *
 * 使用说明：
 * 1. 将此文件复制为 `lv_conf.h`，放置在 lvgl 文件夹同级目录
 * 2. 或者放置在其他位置，并：
 *    - 定义 `LV_CONF_INCLUDE_SIMPLE`
 *    - 将该路径添加到 include 路径中
 */

/* clang-format off */
#if 1 /* 启用内容 */
#ifndef LV_CONF_H
#define LV_CONF_H

/* 如果需要在此处包含任何内容，请将其放在 `__ASSEMBLY__` 保护宏内 */
#if  0 && defined(__ASSEMBLY__)
#include "my_include.h"
#endif

/*====================
   颜色设置 (COLOR SETTINGS)
 *====================*/

/** 颜色深度：1 (I1), 8 (L8), 16 (RGB565), 24 (RGB888), 32 (XRGB8888)
 *  - 1:  单色
 *  - 8:  8位灰度或调色板模式
 *  - 16: RGB565 (16位, 5+6+5)
 *  - 24: RGB888 (24位, 8+8+8)
 *  - 32: XRGB8888 (32位, 8+8+8+8)
 */
#define LV_COLOR_DEPTH 32

/*=========================
   标准库包装器设置 (STDLIB WRAPPER SETTINGS)
 *=========================*/

/** 内存分配函数的可选实现：
 * - LV_STDLIB_BUILTIN:     使用 LVGL 内置实现
 * - LV_STDLIB_CLIB:        使用标准 C 函数 (malloc, free 等)
 * - LV_STDLIB_MICROPYTHON: 使用 MicroPython 实现
 * - LV_STDLIB_RTTHREAD:    使用 RT-Thread 实现
 * - LV_STDLIB_CUSTOM:      外部自定义实现
 */
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_BUILTIN

/** 字符串处理函数的可选实现（与上述选项相同）*/
#define LV_USE_STDLIB_STRING    LV_STDLIB_BUILTIN

/** 格式化输出函数的可选实现（与上述选项相同）*/
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_BUILTIN

#define LV_STDINT_INCLUDE       <stdint.h>
#define LV_STDDEF_INCLUDE       <stddef.h>
#define LV_STDBOOL_INCLUDE      <stdbool.h>
#define LV_INTTYPES_INCLUDE     <inttypes.h>
#define LV_LIMITS_INCLUDE       <limits.h>
#define LV_STDARG_INCLUDE       <stdarg.h>

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_BUILTIN
    /** lv_malloc() 可用的内存大小（字节），至少 2KB */
    #define LV_MEM_SIZE (1024 * 1024)

    /** lv_malloc() 内存扩展大小（字节）*/
    #define LV_MEM_POOL_EXPAND_SIZE 0

    /** 设置内存池的固定地址，而非动态分配。也可设置在外部 SRAM 中。
     * 设为 0 表示不使用此功能 */
    #define LV_MEM_ADR 0     /**< 0: 不使用 */
    /* 如果 LV_MEM_ADR 为 0，则取消定义内存池相关的包含和分配宏 */
    #if LV_MEM_ADR == 0
        #undef LV_MEM_POOL_INCLUDE
        #undef LV_MEM_POOL_ALLOC
    #endif
#endif  /*LV_USE_STDLIB_MALLOC == LV_STDLIB_BUILTIN*/

/*====================
   硬件抽象层设置 (HAL SETTINGS)
 *====================*/

/** 默认显示刷新、输入设备读取和动画步进的周期（毫秒）*/
#define LV_DEF_REFR_PERIOD  16      /**< [ms] */

/** 默认每英寸点数（DPI）。用于初始化组件默认大小和样式内边距。
 *（这个参数不太重要，调整它可以修改默认大小和间距）*/
#define LV_DPI_DEF 130              /**< [px/inch] */

/*=================
 * 操作系统设置 (OPERATING SYSTEM)
 *=================*/
/** 选择要使用的操作系统。可选值：
 * - LV_OS_NONE       不使用操作系统（裸机）
 * - LV_OS_PTHREAD    POSIX 线程
 * - LV_OS_FREERTOS   FreeRTOS
 * - LV_OS_CMSIS_RTOS2 CMSIS-RTOS2
 * - LV_OS_RTTHREAD   RT-Thread
 * - LV_OS_WINDOWS    Windows
 * - LV_OS_MQX        MQX
 * - LV_OS_SDL2       SDL2
 * - LV_OS_CUSTOM     自定义操作系统
 */
#define LV_USE_OS   LV_OS_NONE

#if LV_USE_OS == LV_OS_CUSTOM
    #define LV_OS_CUSTOM_INCLUDE <stdint.h>
#endif
#if LV_USE_OS == LV_OS_FREERTOS
    /*
     * 使用直接任务通知比使用中间对象（如二进制信号量）解除阻塞任务
     * 快 45% 且占用更少的 RAM。
     * 注意：RTOS 任务通知仅在只有一个任务可以作为事件接收者时才能使用。
     */
    #define LV_USE_FREERTOS_TASK_NOTIFY 1
#endif

/*========================
 * 渲染配置 (RENDERING CONFIGURATION)
 *========================*/

/** 图层和图像的行距对齐字节数 */
#define LV_DRAW_BUF_STRIDE_ALIGN                1

/** draw_buf 地址的起始地址对齐字节数*/
#define LV_DRAW_BUF_ALIGN                       4

/** 使用矩阵进行变换。
 * 要求：
 * - `LV_USE_MATRIX = 1`
 * - 渲染引擎需要支持 3x3 矩阵变换
 */
#define LV_DRAW_TRANSFORM_USE_MATRIX            0

/* 如果组件的 style_opa < 255（不是 bg_opa、text_opa 等）或者不是普通混合模式，
 * 它会在渲染前被缓冲到"简单"图层中。组件可以分块缓冲。
 * "变换图层"（如果设置了 transform_angle/zoom）使用更大的缓冲区，
 * 无法分块绘制。*/

/** 简单图层分块的目标缓冲区大小 */
#define LV_DRAW_LAYER_SIMPLE_BUF_SIZE    (24 * 1024)    /**< [字节]*/

/* 限制为简单图层和变换图层分配的最大内存。
 * 它应该至少是 LV_DRAW_LAYER_SIMPLE_BUF_SIZE 的大小，
 * 但如果也使用变换图层，则应该足够存储最大的组件（宽度 x 高度 x 4 字节）。
 * 设为 0 表示不限制。*/
#define LV_DRAW_LAYER_MAX_MEMORY 0  /**< 默认不限制 [字节]*/

/** 绘制线程的栈大小。
 * 注意：如果启用了 FreeType 或 ThorVG，建议设置为 32KB 或更大。
 */
#define LV_DRAW_THREAD_STACK_SIZE    (8 * 1024)         /**< [字节]*/

/** 绘制任务的线程优先级。
 * 值越高优先级越高。
 * 可使用 lv_os.h 中 lv_thread_prio_t 枚举的值：
 * LV_THREAD_PRIO_LOWEST, LV_THREAD_PRIO_LOW, LV_THREAD_PRIO_MID,
 * LV_THREAD_PRIO_HIGH, LV_THREAD_PRIO_HIGHEST
 * 确保优先级值与特定操作系统的优先级级别一致。
 * 在优先级级别有限的系统上（如 FreeRTOS），较高的值可以提高
 * 渲染性能，但也可能导致其他任务饥饿。*/
#define LV_DRAW_THREAD_PRIO LV_THREAD_PRIO_HIGH

#define LV_USE_DRAW_SW 1
#if LV_USE_DRAW_SW == 1
    /*
     * 有选择地禁用颜色格式支持以减小代码大小。
     * 注意：某些功能内部使用某些颜色格式，例如：
     * - 渐变使用 RGB888
     * - 带透明度的位图可能使用 ARGB8888
     */
    #define LV_DRAW_SW_SUPPORT_RGB565       1
    #define LV_DRAW_SW_SUPPORT_RGB565_SWAPPED       1
    #define LV_DRAW_SW_SUPPORT_RGB565A8     1
    #define LV_DRAW_SW_SUPPORT_RGB888       1
    #define LV_DRAW_SW_SUPPORT_XRGB8888     1
    #define LV_DRAW_SW_SUPPORT_ARGB8888     1
    #define LV_DRAW_SW_SUPPORT_ARGB8888_PREMULTIPLIED 1
    #define LV_DRAW_SW_SUPPORT_L8           1
    #define LV_DRAW_SW_SUPPORT_AL88         1
    #define LV_DRAW_SW_SUPPORT_A8           1
    #define LV_DRAW_SW_SUPPORT_I1           1

    /** 索引颜色格式中，像素被视为"活跃"的亮度阈值 */
    #define LV_DRAW_SW_I1_LUM_THRESHOLD 127

    /** 设置绘制单元的数量。
     * - > 1 需要在 LV_USE_OS 中启用操作系统支持
     * - > 1 表示多个线程将并行渲染屏幕
     */
    #define LV_DRAW_SW_DRAW_UNIT_CNT    1

    /** 使用 Arm-2D 加速软件渲染 */
    #define LV_USE_DRAW_ARM2D_SYNC      0

    /** 启用原生 helium 汇编编译 */
    #define LV_USE_NATIVE_HELIUM_ASM    0

    /**
     * - 0: 使用简单渲染器，仅能绘制简单矩形、渐变、图像、文本和直线
     * - 1: 使用复杂渲染器，还能绘制圆角、阴影、斜线和弧线
     */
    #define LV_DRAW_SW_COMPLEX          1

    #if LV_DRAW_SW_COMPLEX == 1
        /** 允许缓冲一些阴影计算。
         * LV_DRAW_SW_SHADOW_CACHE_SIZE 是要缓冲的最大阴影大小，
         * 其中阴影大小 = shadow_width + radius。
         * 缓存占用 LV_DRAW_SW_SHADOW_CACHE_SIZE^2 字节的 RAM。*/
        #define LV_DRAW_SW_SHADOW_CACHE_SIZE 0

        /** 设置要缓存的圆形数据的最大数量。
         * 为了抗锯齿，保存 1/4 圆的周长数据。
         * 每个圆使用 radius * 4 字节（最常用的半径会被保存）。
         * - 0: 禁用缓存 */
        #define LV_DRAW_SW_CIRCLE_CACHE_SIZE 4
    #endif

    #define  LV_USE_DRAW_SW_ASM     LV_DRAW_SW_ASM_NONE

    #if LV_USE_DRAW_SW_ASM == LV_DRAW_SW_ASM_CUSTOM
        #define  LV_DRAW_SW_ASM_CUSTOM_INCLUDE ""
    #endif

    /** 启用软件绘制复杂渐变：线性角度渐变、径向渐变或圆锥渐变 */
    #define LV_USE_DRAW_SW_COMPLEX_GRADIENTS    1

#endif

/** 使用 TSi（NemaGFX，由 Think Silicon 提供）*/
#define LV_USE_NEMA_GFX 0

#if LV_USE_NEMA_GFX
    /** 选择要使用的 NemaGFX HAL。可选值：
     * - LV_NEMA_HAL_CUSTOM
     * - LV_NEMA_HAL_STM32 */
    #define LV_USE_NEMA_HAL LV_NEMA_HAL_CUSTOM
    #if LV_USE_NEMA_HAL == LV_NEMA_HAL_STM32
        #define LV_NEMA_STM32_HAL_INCLUDE <stm32u5xx_hal.h>
    #endif

    /*启用矢量图形操作。仅在有 NemaVG 库时可用*/
    #define LV_USE_NEMA_VG 0
    #if LV_USE_NEMA_VG
        /*定义用于 VG 相关缓冲区分配的应用分辨率*/
        #define LV_NEMA_GFX_MAX_RESX 800
        #define LV_NEMA_GFX_MAX_RESY 600
    #endif
#endif

/** 在 NXP iMX RTxxx 平台上使用 VG-Lite GPU。*/
#define LV_USE_DRAW_VGLITE 0

#if LV_USE_DRAW_VGLITE
    /** 启用位图质量降级补偿，适用于屏幕尺寸 > 352 像素。*/
    #define LV_USE_VGLITE_BLIT_SPLIT 0

    #if LV_USE_OS
        /** 使用额外的绘制线程进行 VG-Lite 处理。*/
        #define LV_USE_VGLITE_DRAW_THREAD 1

        #if LV_USE_VGLITE_DRAW_THREAD
            /** 启用 VGLite 异步绘制。将多个任务排队，一次性刷新到 GPU。*/
            #define LV_USE_VGLITE_DRAW_ASYNC 1
        #endif
    #endif

    /** 启用 VGLite 断言。*/
    #define LV_USE_VGLITE_ASSERT 0

    /** 启用 VGLite 错误检查。*/
    #define LV_USE_VGLITE_CHECK_ERROR 0
#endif

/** 在 NXP iMX RTxxx 平台上使用 PXP。*/
#define LV_USE_PXP 0

#if LV_USE_PXP
    /** 使用 PXP 进行绘制。*/
    #define LV_USE_DRAW_PXP 1

    /** 使用 PXP 旋转显示器。*/
    #define LV_USE_ROTATE_PXP 0

    #if LV_USE_DRAW_PXP && LV_USE_OS
        /** 使用额外的绘制线程进行 PXP 处理。*/
        #define LV_USE_PXP_DRAW_THREAD 1
    #endif

    /** 启用 PXP 断言。*/
    #define LV_USE_PXP_ASSERT 0
#endif

/** 在 NXP MPU 平台上使用 G2D。*/
#define LV_USE_DRAW_G2D 0

#if LV_USE_DRAW_G2D
    /** G2D 绘制单元可存储的最大缓冲区数量。
     * 包括帧缓冲区和资源。*/
    #define LV_G2D_HASH_TABLE_SIZE 50

    #if LV_USE_OS
        /** 使用额外的绘制线程进行 G2D 处理。*/
        #define LV_USE_G2D_DRAW_THREAD 1
    #endif

    /** 启用 G2D 断言。*/
    #define LV_USE_G2D_ASSERT 0
#endif

/** 在 Renesas RA 平台上使用 Dave2D。*/
#define LV_USE_DRAW_DAVE2D 0

/** 使用缓存的 SDL 纹理进行绘制*/
#define LV_USE_DRAW_SDL 0

/** 使用 VG-Lite GPU。*/
#define LV_USE_DRAW_VG_LITE 0

#if LV_USE_DRAW_VG_LITE
    /** 启用 VG-Lite 自定义外部 'gpu_init()' 函数 */
    #define LV_VG_LITE_USE_GPU_INIT 0

    /** 启用 VG-Lite 断言。*/
    #define LV_VG_LITE_USE_ASSERT 0

    /** VG-Lite 刷新提交触发阈值。GPU 将尝试批量处理这些数量的绘制任务。*/
    #define LV_VG_LITE_FLUSH_MAX_COUNT 8

    /** 启用边框来模拟阴影。
     * 注意：这通常可以提高性能，
     * 但不能保证与软件渲染相同的渲染质量。*/
    #define LV_VG_LITE_USE_BOX_SHADOW 1

    /** VG-Lite 渐变最大缓存数量。
     * @note 单个渐变图像的内存使用量为 4K 字节。*/
    #define LV_VG_LITE_GRAD_CACHE_CNT 32

    /** VG-Lite 笔触最大缓存数量。*/
    #define LV_VG_LITE_STROKE_CACHE_CNT 32
#endif

/** 使用 STM32 DMA2D 加速混合、填充等操作 */
#define LV_USE_DRAW_DMA2D 0

#if LV_USE_DRAW_DMA2D
    #define LV_DRAW_DMA2D_HAL_INCLUDE "stm32h7xx_hal.h"

    /* 如果启用，用户需要在收到 DMA2D 全局中断时调用
     * `lv_draw_dma2d_transfer_complete_interrupt_handler` */
    #define LV_USE_DRAW_DMA2D_INTERRUPT 0
#endif

/** 使用缓存的 OpenGLES 纹理进行绘制 */
#define LV_USE_DRAW_OPENGLES 0

/*=======================
 * 功能配置 (FEATURE CONFIGURATION)
 *=======================*/

/*-------------
 * 日志 (Logging)
 *-----------*/

/** 启用日志模块 */
#define LV_USE_LOG 1
#if LV_USE_LOG
    /** 设置日志详细级别：
     * - LV_LOG_LEVEL_TRACE   详细日志信息
     * - LV_LOG_LEVEL_INFO    重要事件日志
     * - LV_LOG_LEVEL_WARN    不希望发生但不影响系统运行的事件
     * - LV_LOG_LEVEL_ERROR   仅记录可能导致系统失败的严重问题
     * - LV_LOG_LEVEL_USER    仅记录用户添加的自定义日志
     * - LV_LOG_LEVEL_NONE    不记录任何日志
     */
    #define LV_LOG_LEVEL LV_LOG_LEVEL_WARN

    /** - 1: 使用 printf 打印日志
     * - 0: 用户需要使用 `lv_log_register_print_cb()` 注册回调函数
     */
    #define LV_LOG_PRINTF 1

    /** 设置日志打印回调函数。
     * 例如：`my_print`。函数原型应为：`void my_print(lv_log_level_t level, const char * buf)`
     * 可被 `lv_log_register_print_cb` 覆盖
     */
    //#define LV_LOG_PRINT_CB

    /** - 1: 启用时间戳打印
     * - 0: 禁用时间戳打印
     */
    #define LV_LOG_USE_TIMESTAMP 1

    /** - 1: 打印日志所在文件和行号
     * - 0: 不打印文件和行号
     */
    #define LV_LOG_USE_FILE_LINE 1

    /* 启用/禁用会产生大量日志的模块的跟踪日志 */
    #define LV_LOG_TRACE_MEM        1   /**< 启用/禁用内存操作的跟踪日志 */
    #define LV_LOG_TRACE_TIMER      1   /**< 启用/禁用定时器操作的跟踪日志 */
    #define LV_LOG_TRACE_INDEV      1   /**< 启用/禁用输入设备操作的跟踪日志 */
    #define LV_LOG_TRACE_DISP_REFR  1   /**< 启用/禁用显示器重绘操作的跟踪日志 */
    #define LV_LOG_TRACE_EVENT      1   /**< 启用/禁用事件分发逻辑的跟踪日志 */
    #define LV_LOG_TRACE_OBJ_CREATE 1   /**< 启用/禁用对象创建（核心对象创建及每个组件）的跟踪日志 */
    #define LV_LOG_TRACE_LAYOUT     1   /**< 启用/禁用 Flex 和 Grid 布局操作的跟踪日志 */
    #define LV_LOG_TRACE_ANIM       1   /**< 启用/禁用动画逻辑的跟踪日志 */
    #define LV_LOG_TRACE_CACHE      1   /**< 启用/禁用缓存操作的跟踪日志 */
#endif  /*LV_USE_LOG*/

/*-------------
 * 断言 (Asserts)
 *-----------*/

/* 当操作失败或发现无效数据时启用断言失败提示。
 * 如果启用了 LV_USE_LOG，失败时会打印错误消息。*/
#define LV_USE_ASSERT_NULL          1   /**< 检查参数是否为 NULL（非常快速，推荐启用）*/
#define LV_USE_ASSERT_MALLOC        1   /**< 检查内存是否成功分配（非常快速，推荐启用）*/
#define LV_USE_ASSERT_STYLE         1   /**< 检查样式相关断言 */
#define LV_USE_ASSERT_MEM_INTEGRITY 1   /**< 检查内存完整性 */
#define LV_USE_ASSERT_OBJ           1   /**< 检查对象相关断言 */

/** 断言发生时添加自定义处理程序，例如重启 MCU */
#define LV_ASSERT_HANDLER_INCLUDE <stdint.h>
#define LV_ASSERT_HANDLER while(1);     /**< 默认行为：死循环*/

/*-------------
 * 调试 (Debug)
 *-----------*/

/** 1: 在重绘区域上绘制随机颜色的矩形 */
#define LV_USE_REFR_DEBUG 0

/** 1: 为 ARGB 图层绘制红色叠加层，为 RGB 图层绘制绿色叠加层*/
#define LV_USE_LAYER_DEBUG 0

/** 1: 添加以下调试行为：
 * - 为每个 draw_unit 的任务用不同颜色绘制叠加层
 * - 在白色背景上绘制 draw unit 的索引编号
 * - 对于图层，在黑色背景上绘制 draw unit 的索引编号*/
#define LV_USE_PARALLEL_DRAW_DEBUG 0

/*-------------
 * 其他设置 (Others)
 *-----------*/

#define LV_ENABLE_GLOBAL_CUSTOM 0
#if LV_ENABLE_GLOBAL_CUSTOM
    /** 为自定义 'lv_global' 函数包含的头文件 */
    #define LV_GLOBAL_CUSTOM_INCLUDE <stdint.h>
#endif

/** 默认缓存大小（字节）。
 * 供图像解码器使用，如 `lv_lodepng`，用于在内存中保存解码后的图像。
 * 如果不设置为 0，当缓存满时解码器将无法解码。
 * 如果设置为 0，则不启用缓存功能，解码后的内存会在使用后立即释放。*/
#define LV_CACHE_DEF_SIZE       0

/** 默认图像头缓存条目数量。缓存用于存储图像的头信息。
 * 逻辑与 `LV_CACHE_DEF_SIZE` 类似，但是针对图像头。*/
#define LV_IMAGE_HEADER_CACHE_DEF_CNT 0

/** 每个渐变允许的过渡点数量。增加此值可允许更多过渡点。
 * 每增加一个过渡点，会增加 (sizeof(lv_color_t) + 1) 字节。*/
#define LV_GRADIENT_MAX_STOPS   2

/** 调整颜色混合函数的取整方式。不同的 GPU 可能会以不同方式计算颜色混合（混合）。
 * - 0:   向下取整
 * - 64:  从 x.75 向上取整
 * - 128: 从中间值向上取整
 * - 192: 从 x.25 向上取整
 * - 254: 向上取整*/
#define LV_COLOR_MIX_ROUND_OFS  0

/** 为每个 `lv_obj_t` 添加 2 个 32 位变量，以加速获取样式属性 */
#define LV_OBJ_STYLE_CACHE      1

/** 为 `lv_obj_t` 添加 `id` 字段 */
#define LV_USE_OBJ_ID           0

/** 启用组件名称支持*/
#define LV_USE_OBJ_NAME         0

/** 创建对象时自动分配 ID */
#define LV_OBJ_ID_AUTO_ASSIGN   LV_USE_OBJ_ID

/** 使用内置的对象 ID 处理函数：
 * - lv_obj_assign_id:      当组件被创建时调用，为每个组件类使用独立的计数器作为 ID
 * - lv_obj_id_compare:     比较 ID 以决定是否与请求的值匹配
 * - lv_obj_stringify_id:   返回字符串形式的标识符，例如 "button3"
 * - lv_obj_free_id:        不执行任何操作，因为 ID 不需要内存分配
 * 禁用后，这些函数需要由用户自己实现。*/
#define LV_USE_OBJ_ID_BUILTIN   1

/** 使用对象属性获取/设置 API。*/
#define LV_USE_OBJ_PROPERTY 1

/** 启用属性名称支持。*/
#define LV_USE_PROP_NAME 1

/* 使用 VG-Lite 模拟器。
 * 在 PC 上模拟 LVGL 与 VG-Lite 的集成，无需实际的 GPU 硬件。*/
#define LV_USE_VG_LITE_SIMULATOR 0

#if LV_USE_VG_LITE_SIMULATOR
    /** 启用 LVGL 的混合模式支持 */
    #define LV_VG_LITE_USE_HAL 0

    /** 启用 YUV 颜色格式支持 */
    #define LV_VG_LITE_USE_YUV 0

    /** 启用线性渐变扩展支持 */
    #define LV_VG_LITE_USE_LINEAR_GRADIENT 0

    /** 启用 16 像素对齐支持 */
    #define LV_VG_LITE_USE_BOX_SHADOW 0

    /** 启用多线程渲染 */
    #define LV_VG_LITE_USE_MULTITHREAD 0
#endif

/* 使用 VG-Lite ThorVG 后端。
 * - 需要：LV_USE_THORVG_INTERNAL 或 LV_USE_THORVG_EXTERNAL */
#define LV_USE_VG_LITE_THORVG  0

#if LV_USE_VG_LITE_THORVG
    /** 启用 LVGL 的混合模式支持 */
    #define LV_VG_LITE_THORVG_LVGL_BLEND_SUPPORT 0

    /** 启用 YUV 颜色格式支持 */
    #define LV_VG_LITE_THORVG_YUV_SUPPORT 0

    /** 启用线性渐变扩展支持 */
    #define LV_VG_LITE_THORVG_LINEAR_GRADIENT_EXT_SUPPORT 0

    /** 启用 16 像素对齐支持 */
    #define LV_VG_LITE_THORVG_16PIXELS_ALIGN 1

    /** Buffer address alignment (must be 64 bytes) */
    #define LV_VG_LITE_THORVG_BUF_ADDR_ALIGN 64

    /** 启用多线程渲染 */
    #define LV_VG_LITE_THORVG_THREAD_RENDER 0
#endif

/* 启用多点触摸手势识别功能 */
/* 手势识别需要使用浮点数 */
#define LV_USE_GESTURE_RECOGNITION 0

/*=====================
 *  编译器设置 (COMPILER SETTINGS)
 *====================*/

/** 对于大端系统设置为 1 */
#define LV_BIG_ENDIAN_SYSTEM 0

/** 为 `lv_tick_inc` 函数定义自定义属性 */
#define LV_ATTRIBUTE_TICK_INC

/** 为 `lv_timer_handler` 函数定义自定义属性 */
#define LV_ATTRIBUTE_TIMER_HANDLER

/** 为 `lv_display_flush_ready` 函数定义自定义属性 */
#define LV_ATTRIBUTE_FLUSH_READY

/** VG_LITE 缓冲区按此字节数对齐。
 * @note vglite_src_buf_aligned() 使用此值来验证传递的缓冲区指针的对齐方式。*/
#define LV_ATTRIBUTE_MEM_ALIGN_SIZE 1

/** 需要对齐内存的位置（使用 -Os 编译时，数据默认可能不会对齐到边界）。
 * 例如：__attribute__((aligned(4)))*/
#define LV_ATTRIBUTE_MEM_ALIGN

/** 标记大常量数组的属性，例如字体位图 */
#define LV_ATTRIBUTE_LARGE_CONST

/** 在 RAM 中声明大数组的编译器前缀 */
#define LV_ATTRIBUTE_LARGE_RAM_ARRAY

/** 将性能关键函数放入更快的内存（例如 RAM）*/
#define LV_ATTRIBUTE_FAST_MEM

/** 将整数常量导出到绑定。此宏与 LV_<CONST> 形式的常量一起使用，
 * 应该也出现在 LVGL 绑定 API 中，如 MicroPython。*/
#define LV_EXPORT_CONST_INT(int_value) struct _silence_gcc_warning  /**< 默认值仅用于阻止 GCC 警告 */

/** 使用此前缀标记所有全局外部数据 */
#define LV_ATTRIBUTE_EXTERN_DATA

/** 使用 `float` 作为 `lv_value_precise_t` */
#define LV_USE_FLOAT            1

/** 启用矩阵支持
 * - 需要 `LV_USE_FLOAT = 1` */
#define LV_USE_MATRIX           1

/** 默认在 `lvgl.h` 中包含 `lvgl_private.h` 以访问内部数据和函数 */
#ifndef LV_USE_PRIVATE_API
    #define LV_USE_PRIVATE_API  0
#endif

/*==================
 *   字体使用 (FONT USAGE)
 *===================*/

/* Montserrat 字体，包含 ASCII 范围和一些使用 bpp = 4 的符号
 * https://fonts.google.com/specimen/Montserrat */
#define LV_FONT_MONTSERRAT_8  0
#define LV_FONT_MONTSERRAT_10 0
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_22 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_26 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_30 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_34 1
#define LV_FONT_MONTSERRAT_36 1
#define LV_FONT_MONTSERRAT_38 1
#define LV_FONT_MONTSERRAT_40 1
#define LV_FONT_MONTSERRAT_42 1
#define LV_FONT_MONTSERRAT_44 1
#define LV_FONT_MONTSERRAT_46 1
#define LV_FONT_MONTSERRAT_48 1

/* 演示特殊功能 */
#define LV_FONT_MONTSERRAT_28_COMPRESSED    1
#define LV_FONT_DEJAVU_16_PERSIAN_HEBREW    1
#define LV_FONT_SIMSUN_14_CJK               0  /**< 1000 个最常用的 CJK 部首 */
#define LV_FONT_SIMSUN_16_CJK               1
#define LV_FONT_SOURCE_HAN_SANS_SC_14_CJK   0  /**< 1338 个最常用的 CJK 部首 */
#define LV_FONT_SOURCE_HAN_SANS_SC_16_CJK   0  /**< 1338 个最常用的 CJK 部首 */

/** 像素完美的等宽字体 */
#define LV_FONT_UNSCII_8  1
#define LV_FONT_UNSCII_16 0

/** 可选：在此处声明自定义字体。
 *
 * 你可以使用这些字体中的任何一个作为默认字体，它们将全局可用。例如：
 *
 * @code
 * #define LV_FONT_CUSTOM_DECLARE   LV_FONT_DECLARE(my_font_1) LV_FONT_DECLARE(my_font_2)
 * @endcode
 */
#define LV_FONT_CUSTOM_DECLARE

/** 始终设置默认字体 */
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/** 启用处理大字体和/或包含大量字符的字体。
 * 限制取决于字体大小、字体类型和 bpp。
 * 如果字体需要此功能，将触发编译器错误。*/
#define LV_FONT_FMT_TXT_LARGE 0

/** 启用/禁用压缩字体支持。*/
#define LV_USE_FONT_COMPRESSED 0

/** 当找不到字形描述时，启用绘制占位符。*/
#define LV_USE_FONT_PLACEHOLDER 1

/*=================
 *  文本设置 (TEXT SETTINGS)
 *=================*/

/**
 * 选择字符串的字符编码。
 * 你的 IDE 或编辑器应使用相同的字符编码。
 * - LV_TXT_ENC_UTF8
 * - LV_TXT_ENC_ASCII
 */
#define LV_TXT_ENC LV_TXT_ENC_UTF8

/** 渲染文本字符串时，在这些字符处断行（换行）。*/
#define LV_TXT_BREAK_CHARS " ,.;:-_)]}"

/** 如果一个单词至少有这么长，将在任何"最美观"的位置断行。
 * 要禁用，请设置为 <= 0 的值。*/
#define LV_TXT_LINE_BREAK_LONG_LEN 0

/** 长单词在断行前放在一行上的最少字符数。
 * 取决于 LV_TXT_LINE_BREAK_LONG_LEN。*/
#define LV_TXT_LINE_BREAK_LONG_PRE_MIN_LEN 3

/** 长单词在断行后放在一行上的最少字符数。
 * 取决于 LV_TXT_LINE_BREAK_LONG_LEN。*/
#define LV_TXT_LINE_BREAK_LONG_POST_MIN_LEN 3

/** 支持双向文本。允许混合从左到右和从右到左的文本。
 * 方向将根据 Unicode 双向算法处理：
 * https://www.w3.org/International/articles/inline-bidi-markup/uba-basics */
#define LV_USE_BIDI 0
#if LV_USE_BIDI
    /*设置默认方向。可选值：
    *`LV_BASE_DIR_LTR` 从左到右
    *`LV_BASE_DIR_RTL` 从右到左
    *`LV_BASE_DIR_AUTO` 自动检测文本基础方向*/
    #define LV_BIDI_BASE_DIR_DEF LV_BASE_DIR_AUTO
#endif

/** 启用阿拉伯语/波斯语处理
 * 在这些语言中，字符应根据其在文本中的位置被替换为另一种形式*/
#define LV_USE_ARABIC_PERSIAN_CHARS 0

/*用于标记文本重新着色的控制字符*/
#define LV_TXT_COLOR_CMD "#"

/*==================
 * 组件 (WIDGETS)
 *================*/
/* 组件文档：https://docs.lvgl.io/master/details/widgets/index.html 。*/

/** 1: 使这些组件在创建时具有默认值。
 *  - lv_buttonmatrix_t:  获取默认映射：{"Btn1", "Btn2", "Btn3", "\n", "Btn4", "Btn5", ""}，否则映射未设置。
 *  - lv_checkbox_t    :  字符串标签设置为"Check box"，否则设置为空字符串。
 *  - lv_dropdown_t    :  选项设置为"Option 1"、"Option 2"、"Option 3"，否则不设置任何值。
 *  - lv_roller_t      :  选项设置为"Option 1"、"Option 2"、"Option 3"、"Option 4"、"Option 5"，否则不设置任何值。
 *  - lv_label_t       :  文本设置为"Text"，否则为空字符串。
 * */
#define LV_WIDGETS_HAS_DEFAULT_VALUE  1

#define LV_USE_ANIMIMG    1

#define LV_USE_ARC        1

#define LV_USE_BAR        1

#define LV_USE_BUTTON        1

#define LV_USE_BUTTONMATRIX  1

#define LV_USE_CALENDAR   1
#if LV_USE_CALENDAR
    #define LV_CALENDAR_WEEK_STARTS_MONDAY 0
    #if LV_CALENDAR_WEEK_STARTS_MONDAY
        #define LV_CALENDAR_DEFAULT_DAY_NAMES {"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"}
    #else
        #define LV_CALENDAR_DEFAULT_DAY_NAMES {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"}
    #endif

    #define LV_CALENDAR_DEFAULT_MONTH_NAMES {"January", "February", "March",  "April", "May",  "June", "July", "August", "September", "October", "November", "December"}
    #define LV_USE_CALENDAR_HEADER_ARROW 1
    #define LV_USE_CALENDAR_HEADER_DROPDOWN 1
    #define LV_USE_CALENDAR_CHINESE 0
#endif  /*LV_USE_CALENDAR*/

#define LV_USE_CANVAS     1

#define LV_USE_CHART      1

#define LV_USE_CHECKBOX   1

#define LV_USE_DROPDOWN   1   /**< 需要：lv_label */

#define LV_USE_IMAGE      1   /**< 需要：lv_label */

#define LV_USE_IMAGEBUTTON     1

#define LV_USE_KEYBOARD   1

#define LV_USE_LABEL      1
#if LV_USE_LABEL
    #define LV_LABEL_TEXT_SELECTION 1   /**< 启用标签文本选择 */
    #define LV_LABEL_LONG_TXT_HINT 1    /**< 在标签中存储一些额外信息，以加速绘制非常长的文本 */
    #define LV_LABEL_WAIT_CHAR_COUNT 3  /**< 等待字符计数 */
#endif

#define LV_USE_LED        1

#define LV_USE_LINE       1

#define LV_USE_LIST       1

#define LV_USE_LOTTIE     1

#define LV_USE_MENU       1

#define LV_USE_MSGBOX     1

#define LV_USE_ROLLER     1   /**< 需要：lv_label */

#define LV_USE_SCALE      1

#define LV_USE_SLIDER     1   /**< 需要：lv_bar */

#define LV_USE_SPAN       1
#if LV_USE_SPAN
    /** 一行文本可以包含的最大跨度描述符数量。*/
    #define LV_SPAN_SNIPPET_STACK_SIZE 64
#endif

#define LV_USE_SPINBOX    1

#define LV_USE_SPINNER    1

#define LV_USE_SWITCH     1

#define LV_USE_TABLE      1

#define LV_USE_TABVIEW    1

#define LV_USE_TEXTAREA   1   /**< 需要：lv_label */
#if LV_USE_TEXTAREA != 0
    #define LV_TEXTAREA_DEF_PWD_SHOW_TIME 1500    /**< [毫秒] */
#endif

#define LV_USE_TILEVIEW   1

#define LV_USE_WIN        1

#define LV_USE_3DTEXTURE  0

/*==================
 * 主题 (THEMES)
 *==================*/
/* 主题文档：https://docs.lvgl.io/master/details/common-widget-features/styles/style.html#themes */

/** 一个简洁、令人印象深刻且功能完整的主题 */
#define LV_USE_THEME_DEFAULT 1
#if LV_USE_THEME_DEFAULT
    /** 0: 浅色模式；1: 深色模式 */
    #define LV_THEME_DEFAULT_DARK 0

    /** 1: 按下时启用放大效果 */
    #define LV_THEME_DEFAULT_GROW 1

    /** 默认过渡时间（毫秒）*/
    #define LV_THEME_DEFAULT_TRANSITION_TIME 80
#endif /*LV_USE_THEME_DEFAULT*/

/** 一个非常简洁的主题，是自定义主题的良好起点 */
#define LV_USE_THEME_SIMPLE 1

/** 为单色显示器设计的主题 */
#define LV_USE_THEME_MONO 1

/*==================
 * 布局 (LAYOUTS)
 *==================*/
/* 布局文档：https://docs.lvgl.io/master/details/common-widget-features/layouts/index.html */

/** 类似 CSS Flexbox 的布局 */
#define LV_USE_FLEX 1

/** 类似 CSS Grid 的布局 */
#define LV_USE_GRID 1

/*====================
 * 第三方库 (3RD PARTS LIBRARIES)
 *====================*/
/* 库文档：https://docs.lvgl.io/master/details/libs/index.html */

/* 文件系统接口 */

/** 设置默认驱动器字母，可跳过文件路径中的驱动器前缀。
 * 文档：https://docs.lvgl.io/master/details/main-modules/fs.html#lv-fs-identifier-letters */
#define LV_FS_DEFAULT_DRIVER_LETTER '\0'

/** fopen、fread 等的 API */
#define LV_USE_FS_STDIO 1
#if LV_USE_FS_STDIO
    #define LV_FS_STDIO_LETTER 'A'
    #define LV_FS_STDIO_PATH ""         /**< 设置工作目录，文件/目录路径会附加到其后 */
    #define LV_FS_STDIO_CACHE_SIZE 0    /**< >0 在 lv_fs_read() 中缓存此字节数 */
#endif

/** open、read 等的 API（ POSIX 标准）*/
#define LV_USE_FS_POSIX 0
#if LV_USE_FS_POSIX
    #define LV_FS_POSIX_LETTER '\0'     /**< 为此驱动器设置大写驱动器标识符字母（例如 'A'）*/
    #define LV_FS_POSIX_PATH ""         /**< 设置工作目录，文件/目录路径会附加到其后 */
    #define LV_FS_POSIX_CACHE_SIZE 0    /**< >0 在 lv_fs_read() 中缓存此字节数 */
#endif

/** CreateFile、ReadFile 等的 API（ Windows API）*/
#define LV_USE_FS_WIN32 0
#if LV_USE_FS_WIN32
    #define LV_FS_WIN32_LETTER '\0'     /**< 为此驱动器设置大写驱动器标识符字母（例如 'A'）*/
    #define LV_FS_WIN32_PATH ""         /**< 设置工作目录，文件/目录路径会附加到其后 */
    #define LV_FS_WIN32_CACHE_SIZE 0    /**< >0 在 lv_fs_read() 中缓存此字节数 */
#endif

/** FATFS 的 API（需要单独添加）*/
#define LV_USE_FS_FATFS 0
#if LV_USE_FS_FATFS
    #define LV_FS_FATFS_LETTER '\0'     /**< 为此驱动器设置大写驱动器标识符字母（例如 'A'）*/
    #define LV_FS_FATFS_PATH ""         /**< 设置工作目录，文件/目录路径会附加到其后 */
    #define LV_FS_FATFS_CACHE_SIZE 0    /**< >0 在 lv_fs_read() 中缓存此字节数 */
#endif

/** 内存映射文件访问的 API */
#define LV_USE_FS_MEMFS 0
#if LV_USE_FS_MEMFS
    #define LV_FS_MEMFS_LETTER '\0'     /**< 为此驱动器设置大写驱动器标识符字母（例如 'A'）*/
#endif

/** LittleFs 的 API */
#define LV_USE_FS_LITTLEFS 0
#if LV_USE_FS_LITTLEFS
    #define LV_FS_LITTLEFS_LETTER '\0'  /**< 为此驱动器设置大写驱动器标识符字母（例如 'A'）*/
    #define LV_FS_LITTLEFS_PATH ""      /**< 设置工作目录，文件/目录路径会附加到其后 */
#endif

/** Arduino LittleFs 的 API。*/
#define LV_USE_FS_ARDUINO_ESP_LITTLEFS 0
#if LV_USE_FS_ARDUINO_ESP_LITTLEFS
    #define LV_FS_ARDUINO_ESP_LITTLEFS_LETTER '\0'  /**< 为此驱动器设置大写驱动器标识符字母（例如 'A'）。*/
    #define LV_FS_ARDUINO_ESP_LITTLEFS_PATH ""      /**< 设置工作目录。文件/目录路径将附加到其后。*/
#endif

/** Arduino Sd 的 API。*/
#define LV_USE_FS_ARDUINO_SD 0
#if LV_USE_FS_ARDUINO_SD
    #define LV_FS_ARDUINO_SD_LETTER '\0'  /**< 为此驱动器设置大写驱动器标识符字母（例如 'A'）。*/
    #define LV_FS_ARDUINO_SD_PATH ""      /**< 设置工作目录。文件/目录路径将附加到其后。*/
#endif

/** UEFI 的 API。*/
#define LV_USE_FS_UEFI 0
#if LV_USE_FS_UEFI
    #define LV_FS_UEFI_LETTER '\0'      /**< 为此驱动器设置大写驱动器标识符字母（例如 'A'）。*/
#endif

/** LODEPNG 解码器库 */
#define LV_USE_LODEPNG 1

/** PNG 解码器(libpng)库 */
#define LV_USE_LIBPNG 0

/** BMP 解码器库 */
#define LV_USE_BMP 1

/** JPG + split JPG 解码器库。
 * Split JPG 是一种专为嵌入式系统优化的自定义格式。*/
#define LV_USE_TJPGD 1

/** libjpeg-turbo 解码器库。
 * 支持完整的 JPEG 规范和高性能 JPEG 解码。*/
#define LV_USE_LIBJPEG_TURBO 0

/** GIF 解码器库 */
#define LV_USE_GIF 0
#if LV_USE_GIF
    /** GIF 解码器加速 */
    #define LV_GIF_CACHE_DECODE_DATA 0
#endif


/** 将 bin 图像解码到 RAM 中 */
#define LV_BIN_DECODER_RAM_LOAD 1

/** RLE 解压缩库 */
#define LV_USE_RLE 1

/** 二维码库 */
#define LV_USE_QRCODE 1

/** 条形码库 */
#define LV_USE_BARCODE 1

/** FreeType 库 */
#define LV_USE_FREETYPE 0
#if LV_USE_FREETYPE
    /** 让 FreeType 使用 LVGL 的内存和文件移植层 */
    #define LV_FREETYPE_USE_LVGL_PORT 0

    /** FreeType 中缓存的字形数量，即可以缓存的字形数量。
     * 值越高，占用的内存越多。*/
    #define LV_FREETYPE_CACHE_FT_GLYPH_CNT 256
#endif

/** 内置 TTF 解码器 */
#define LV_USE_TINY_TTF 1
#if LV_USE_TINY_TTF
    /** 启用从文件加载 TTF 数据 */
    #define LV_TINY_TTF_FILE_SUPPORT 0
    #define LV_TINY_TTF_CACHE_GLYPH_CNT 256
#endif

/** Rlottie 库 */
#define LV_USE_RLOTTIE 0

/** 启用矢量图形 API
 * - 需要 `LV_USE_MATRIX = 1` */
#define LV_USE_VECTOR_GRAPHIC  1

/** 启用 ThorVG（矢量图形库），来自 src/libs 文件夹 */
#define LV_USE_THORVG_INTERNAL 1

/** 假设 ThorVG 已安装并链接到项目，启用 ThorVG */
#define LV_USE_THORVG_EXTERNAL 0

/** 使用 lvgl 内置的 LZ4 库 */
#define LV_USE_LZ4_INTERNAL  1

/** 使用外部 LZ4 库 */
#define LV_USE_LZ4_EXTERNAL  0

/*SVG 库
 * - 需要 `LV_USE_VECTOR_GRAPHIC = 1` */
#define LV_USE_SVG 0
#define LV_USE_SVG_ANIMATION 0
#define LV_USE_SVG_DEBUG 0

/** FFmpeg 库，用于图像解码和视频播放。
 * 支持所有主要图像格式，因此启用此库时请勿同时启用其他图像解码器。*/
#define LV_USE_FFMPEG 0
#if LV_USE_FFMPEG
    /** 将输入信息转储到 stderr */
    #define LV_FFMPEG_DUMP_FORMAT 0
    /** 在 FFmpeg 播放器组件中使用 lvgl 文件路径
     * 启用此功能后，将无法打开 URL。
     * 注意：FFmpeg 图像解码器始终使用 lvgl 文件系统。*/
    #define LV_FFMPEG_PLAYER_USE_LV_FS 0
#endif

/*==================
 * 其他 (OTHERS)
 *==================*/
/* 以下几项的文档：https://docs.lvgl.io/master/details/auxiliary-modules/index.html */

/** 1: 启用 API 以获取对象快照 */
#define LV_USE_SNAPSHOT 0

/** 1: 启用系统监视器组件 */
#define LV_USE_SYSMON   1
#if LV_USE_SYSMON
    /** 获取空闲百分比。例如：uint32_t my_get_idle(void); */
    #define LV_SYSMON_GET_IDLE lv_os_get_idle_percent

    /** 1: 显示 CPU 使用率和 FPS 计数
     * - 需要 `LV_USE_SYSMON = 1` */
    #define LV_USE_PERF_MONITOR 1
    #if LV_USE_PERF_MONITOR
        #define LV_USE_PERF_MONITOR_POS LV_ALIGN_BOTTOM_RIGHT

        /** 0: 在屏幕上显示性能数据；1: 通过日志打印性能数据 */
        #define LV_USE_PERF_MONITOR_LOG_MODE 0
    #endif

    /** 1: 显示已用内存和内存碎片情况
     * - 需要 `LV_USE_STDLIB_MALLOC = LV_STDLIB_BUILTIN`
     * - 需要 `LV_USE_SYSMON = 1`*/
    #define LV_USE_MEM_MONITOR 0
    #if LV_USE_MEM_MONITOR
        #define LV_USE_MEM_MONITOR_POS LV_ALIGN_BOTTOM_LEFT
    #endif
#endif /*LV_USE_SYSMON*/

/** 1: 启用运行时性能分析器 */
#define LV_USE_PROFILER 0
#if LV_USE_PROFILER
    /** 1: 启用内置性能分析器 */
    #define LV_USE_PROFILER_BUILTIN 1
    #if LV_USE_PROFILER_BUILTIN
        /** 默认性能分析器跟踪缓冲区大小 */
        #define LV_PROFILER_BUILTIN_BUF_SIZE (16 * 1024)     /**< [字节] */
        #define LV_PROFILER_BUILTIN_DEFAULT_ENABLE 1
    #endif

    /** 性能分析器要包含的头文件 */
    #define LV_PROFILER_INCLUDE "lvgl/src/misc/lv_profiler_builtin.h"

    /** 性能分析器起点函数 */
    #define LV_PROFILER_BEGIN    LV_PROFILER_BUILTIN_BEGIN

    /** 性能分析器终点函数 */
    #define LV_PROFILER_END      LV_PROFILER_BUILTIN_END

    /** 带自定义标签的性能分析器起点函数 */
    #define LV_PROFILER_BEGIN_TAG LV_PROFILER_BUILTIN_BEGIN_TAG

    /** 带自定义标签的性能分析器终点函数 */
    #define LV_PROFILER_END_TAG   LV_PROFILER_BUILTIN_END_TAG

    /*启用布局性能分析器*/
    #define LV_PROFILER_LAYOUT 1

    /*启用显示刷新性能分析器*/
    #define LV_PROFILER_REFR 1

    /*启用绘制性能分析器*/
    #define LV_PROFILER_DRAW 1

    /*启用输入设备性能分析器*/
    #define LV_PROFILER_INDEV 1

    /*启用解码器性能分析器*/
    #define LV_PROFILER_DECODER 1

    /*启用字体性能分析器*/
    #define LV_PROFILER_FONT 1

    /*启用文件系统性能分析器*/
    #define LV_PROFILER_FS 1

    /*启用样式性能分析器*/
    #define LV_PROFILER_STYLE 0

    /*启用定时器性能分析器*/
    #define LV_PROFILER_TIMER 1

    /*启用缓存性能分析器*/
    #define LV_PROFILER_CACHE 1

    /*启用事件性能分析器*/
    #define LV_PROFILER_EVENT 1
#endif

/** 1: 启用猴子测试（随机输入测试）*/
#define LV_USE_MONKEY 0

/** 1: 启用网格导航 */
#define LV_USE_GRIDNAV 0

/** 1: 启用 `lv_obj` 片段逻辑 */
#define LV_USE_FRAGMENT 0

/** 1: 支持在标签或跨度组件中使用图像作为字体 */
#define LV_USE_IMGFONT 1

/** 1: 启用观察者模式实现 */
#define LV_USE_OBSERVER 1

/** 1: 启用拼音输入法
 * - 需要：lv_keyboard */
#define LV_USE_IME_PINYIN 0
#if LV_USE_IME_PINYIN
    /** 1: 使用默认词典。
     * @note 如果不使用默认词典，请在设置词典后使用 `lv_ime_pinyin`。 */
    #define LV_IME_PINYIN_USE_DEFAULT_DICT 1
    /** 设置可显示的候选面板最大数量。
     * @note 需要根据屏幕大小进行调整。*/
    #define LV_IME_PINYIN_CAND_TEXT_NUM 6

    /** 使用 9 键输入 (k9)。*/
    #define LV_IME_PINYIN_USE_K9_MODE      1
    #if LV_IME_PINYIN_USE_K9_MODE == 1
        #define LV_IME_PINYIN_K9_CAND_TEXT_NUM 3
    #endif /*LV_IME_PINYIN_USE_K9_MODE*/
#endif

/** 1: 启用文件浏览器。
 * - 需要：lv_table */
#define LV_USE_FILE_EXPLORER                     0
#if LV_USE_FILE_EXPLORER
    /** 路径最大长度 */
    #define LV_FILE_EXPLORER_PATH_MAX_LEN        (128)
    /** 快速访问栏，1:启用，0:禁用
     * - 需要：lv_list */
    #define LV_FILE_EXPLORER_QUICK_ACCESS        1
#endif

/** 1: 启用字体管理器 */
#define LV_USE_FONT_MANAGER                     0
#if LV_USE_FONT_MANAGER

/**字体管理器名称最大长度*/
#define LV_FONT_MANAGER_NAME_MAX_LEN            32

#endif

/** 启用模拟输入设备、时间模拟和截图比较。*/
#define LV_USE_TEST 0
#if LV_USE_TEST

/** 启用 `lv_test_screenshot_compare`。
 * 需要 libpng 和几 MB 的额外内存。*/
#define LV_USE_TEST_SCREENSHOT_COMPARE 0
#endif /*LV_USE_TEST*/

/** 启用运行时加载 XML UI */
#define LV_USE_XML    0

/** 1: 启用颜色滤镜样式*/
#define LV_USE_COLOR_FILTER     0
/*==================
 * 设备 (DEVICES)
 *==================*/

/** 使用 SDL 在 PC 上打开窗口，并处理鼠标和键盘 */
#define LV_USE_SDL              1
#if LV_USE_SDL
    #define LV_SDL_INCLUDE_PATH     <SDL2/SDL.h>
    #define LV_SDL_RENDER_MODE      LV_DISPLAY_RENDER_MODE_DIRECT   /**< 建议使用 LV_DISPLAY_RENDER_MODE_DIRECT 以获得最佳性能 */
    #define LV_SDL_BUF_COUNT        1    /**< 1 或 2 */
    #define LV_SDL_ACCELERATED      1    /**< 1: 使用硬件加速 */
    #define LV_SDL_FULLSCREEN       0    /**< 1: 默认全屏 */
    #define LV_SDL_DIRECT_EXIT      1    /**< 1: 关闭所有 SDL 窗口时退出应用程序 */
    #define LV_SDL_MOUSEWHEEL_MODE  LV_SDL_MOUSEWHEEL_MODE_ENCODER  /*LV_SDL_MOUSEWHEEL_MODE_ENCODER/CROWN*/
#endif

/** 使用 X11 在 Linux 桌面上打开窗口，并处理鼠标和键盘 */
#define LV_USE_X11              0
#if LV_USE_X11
    #define LV_X11_DIRECT_EXIT         1  /**< 所有 X11 窗口关闭时退出应用程序 */
    #define LV_X11_DOUBLE_BUFFER       1  /**< 使用双缓冲区进行渲染 */
    /* 只能选择以下一种渲染模式（推荐 LV_X11_RENDER_MODE_PARTIAL）*/
    #define LV_X11_RENDER_MODE_PARTIAL 1  /**< 部分渲染模式（推荐）*/
    #define LV_X11_RENDER_MODE_DIRECT  0  /**< 直接渲染模式 */
    #define LV_X11_RENDER_MODE_FULL    0  /**< 完全渲染模式 */
#endif

/** 使用 Wayland 在 Linux 或 BSD 桌面上打开窗口并处理输入 */
#define LV_USE_WAYLAND          0
#if LV_USE_WAYLAND
    #define LV_WAYLAND_WINDOW_DECORATIONS   0    /**< 仅在 Mutter/GNOME 上绘制客户端窗口装饰 */
    #define LV_WAYLAND_WL_SHELL             0    /**< 使用传统的 wl_shell 协议而非默认的 XDG shell */
#endif

/** /dev/fb 的驱动 */
#define LV_USE_LINUX_FBDEV      0
#if LV_USE_LINUX_FBDEV
    #define LV_LINUX_FBDEV_BSD           0
    #define LV_LINUX_FBDEV_RENDER_MODE   LV_DISPLAY_RENDER_MODE_PARTIAL
    #define LV_LINUX_FBDEV_BUFFER_COUNT  0
    #define LV_LINUX_FBDEV_BUFFER_SIZE   60
    #define LV_LINUX_FBDEV_MMAP          1
#endif

/** 使用 Nuttx 打开窗口并处理触摸屏 */
#define LV_USE_NUTTX    0

#if LV_USE_NUTTX
    #define LV_USE_NUTTX_INDEPENDENT_IMAGE_HEAP 0

    /** 为默认绘制缓冲区使用独立的图像堆 */
    #define LV_NUTTX_DEFAULT_DRAW_BUF_USE_INDEPENDENT_IMAGE_HEAP    0

    #define LV_USE_NUTTX_LIBUV    0

    /** 使用 Nuttx 自定义初始化 API 打开窗口并处理触摸屏 */
    #define LV_USE_NUTTX_CUSTOM_INIT    0

    /** /dev/lcd 的驱动 */
    #define LV_USE_NUTTX_LCD      0
    #if LV_USE_NUTTX_LCD
        #define LV_NUTTX_LCD_BUFFER_COUNT    0
        #define LV_NUTTX_LCD_BUFFER_SIZE     60
    #endif

    /** /dev/input 的驱动 */
    #define LV_USE_NUTTX_TOUCHSCREEN    0

    /*触摸屏光标大小（像素）（<=0：禁用光标）*/
    #define LV_NUTTX_TOUCHSCREEN_CURSOR_SIZE    0
#endif

/** /dev/dri/card 的驱动 */
#define LV_USE_LINUX_DRM        0

#if LV_USE_LINUX_DRM

    /* 使用 MESA GBM 库分配 DMA 缓冲区，这些缓冲区可以通过 Linux DMA-BUF API 在子系统
     * 和库之间共享。GBM 库旨在提供平台无关的内存管理系统，支持主要 GPU 供应商
     * 此选项需要链接 libgbm */
    #define LV_LINUX_DRM_GBM_BUFFERS 0
#endif

/** TFT_eSPI 的接口 */
#define LV_USE_TFT_ESPI         0

/** evdev 输入设备的驱动 */
#define LV_USE_EVDEV    0

/** libinput 输入设备的驱动 */
#define LV_USE_LIBINPUT    0

#if LV_USE_LIBINPUT
    #define LV_LIBINPUT_BSD    0

    /** 完整键盘支持 */
    #define LV_LIBINPUT_XKB             0
    #if LV_LIBINPUT_XKB
        /** "setxkbmap -query" can help find the right values for your keyboard */
        #define LV_LIBINPUT_XKB_KEY_MAP { .rules = NULL, .model = "pc101", .layout = "us", .variant = NULL, .options = NULL }
    #endif
#endif

/* 通过 SPI/并行端口连接的 LCD 设备的驱动 */
#define LV_USE_ST7735        0
#define LV_USE_ST7789        0
#define LV_USE_ST7796        0
#define LV_USE_ILI9341       0
#define LV_USE_FT81X         0

#if (LV_USE_ST7735 | LV_USE_ST7789 | LV_USE_ST7796 | LV_USE_ILI9341)
    #define LV_USE_GENERIC_MIPI 1
#else
    #define LV_USE_GENERIC_MIPI 0
#endif

/** Renesas GLCD 的驱动 */
#define LV_USE_RENESAS_GLCDC    0

/** ST LTDC 的驱动 */
#define LV_USE_ST_LTDC    0
#if LV_USE_ST_LTDC
    /* 仅用于部分渲染。*/
    #define LV_ST_LTDC_USE_DMA2D_FLUSH 0
#endif

/** LVGL Windows 后端 */
#define LV_USE_WINDOWS    0

/** LVGL UEFI 后端 */
#define LV_USE_UEFI 0
#if LV_USE_UEFI
    #define LV_USE_UEFI_INCLUDE "myefi.h"   /**< 隐藏实际框架的头文件（EDK2、gnu-efi、...）*/
    #define LV_UEFI_USE_MEMORY_SERVICES 0   /**< 使用引导服务表中的内存函数 */
#endif

/** 使用 OpenGL 在 PC 上打开窗口并处理鼠标和键盘 */
#define LV_USE_OPENGLES   0
#if LV_USE_OPENGLES
    #define LV_USE_OPENGLES_DEBUG        1    /**< 启用或禁用 opengles 的调试 */
#endif

/** QNX Screen 显示和输入驱动 */
#define LV_USE_QNX              0
#if LV_USE_QNX
    #define LV_QNX_BUF_COUNT        1    /**< 1 或 2 */
#endif

/*=====================
 * 构建选项 (BUILD OPTIONS)
 *=====================*/

/** 启用示例程序与库一起构建 */
#define LV_BUILD_EXAMPLES 1

/** 构建演示程序 */
#define LV_BUILD_DEMOS 1

/*===================
 * 演示程序使用 (DEMO USAGE)
 ====================*/

#if LV_BUILD_DEMOS
    /** 显示一些组件。可能需要增加 `LV_MEM_SIZE`。*/
    #define LV_USE_DEMO_WIDGETS 1

    /** 演示编码器和键盘的使用。*/
    #define LV_USE_DEMO_KEYPAD_AND_ENCODER 1

    /** 系统性能基准测试 */
    #define LV_USE_DEMO_BENCHMARK 1

    #if LV_USE_DEMO_BENCHMARK
        /** 使用位图对齐 16 字节、步距为 Nx16 字节的字体 */
        #define LV_DEMO_BENCHMARK_ALIGNED_FONTS 0
    #endif

    /** 每个图元的渲染测试。
     * - 至少需要 480x272 的显示器。*/
    #define LV_USE_DEMO_RENDER 1

    /** LVGL 压力测试 */
    #define LV_USE_DEMO_STRESS 1

    /** 音乐播放器演示 */
    #define LV_USE_DEMO_MUSIC 1
    #if LV_USE_DEMO_MUSIC
        #define LV_DEMO_MUSIC_SQUARE    0
        #define LV_DEMO_MUSIC_LANDSCAPE 0
        #define LV_DEMO_MUSIC_ROUND     0
        #define LV_DEMO_MUSIC_LARGE     0
        #define LV_DEMO_MUSIC_AUTO_PLAY 0
    #endif

    /** 矢量图形演示 */
    #define LV_USE_DEMO_VECTOR_GRAPHIC  0

    /*---------------------------
     * 来自 lvgl/lv_demos 的演示
      ---------------------------*/

    /** Flex 布局演示 */
    #define LV_USE_DEMO_FLEX_LAYOUT     1

    /** 类似智能手机的多语言演示 */
    #define LV_USE_DEMO_MULTILANG       1

    /** 组件变换演示 */
    #define LV_USE_DEMO_TRANSFORM       1

    /** 演示滚动设置 */
    #define LV_USE_DEMO_SCROLL          1

    /*带有 Lottie 动画的电动自行车演示（如果启用了 LV_USE_LOTTIE）*/
    #define LV_USE_DEMO_EBIKE           0
    #if LV_USE_DEMO_EBIKE
        #define LV_DEMO_EBIKE_PORTRAIT  0    /*0: 适用于 480x270..480x320，1: 适用于 480x800..720x1280*/
    #endif

    /** 高分辨率演示 */
    #define LV_USE_DEMO_HIGH_RES        0

    /* 智能手表演示 */
    #define LV_USE_DEMO_SMARTWATCH      0
#endif /* LV_BUILD_DEMOS */

/*--LV_CONF_H 结束--*/

#endif /*LV_CONF_H*/

#endif /*"内容启用"结束*/
