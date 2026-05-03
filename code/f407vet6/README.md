# 举升机控制系统

## 项目信息

- **平台**：STM32F407VET6
- **RTOS**：FreeRTOS (CMSIS-RTOS2)
- **编译器**：IAR EWARM
- **外部 Flash**：W25Q128 (SPI1)

---

## BSP 层 - SPI 总线

### bsp_spi.h / bsp_spi.c

| API | 说明 | 参数 | 返回值 |
|-----|------|------|--------|
| `SPI_Bus_Init(spi_bus_t *bus)` | 初始化 SPI 总线（调用硬件 Init 回调） | bus: SPI 总线对象指针 | 无 |
| `SPI_Bus_Select(spi_bus_t *bus)` | 拉低片选引脚（CS = 0，选中设备） | bus: SPI 总线对象指针 | 无 |
| `SPI_Bus_Deselect(spi_bus_t *bus)` | 拉高片选引脚（CS = 1，释放设备） | bus: SPI 总线对象指针 | 无 |

### 结构体

| 结构体/字段 | 说明 |
|-------------|------|
| `spi_bus_t` | SPI 总线抽象结构体 |
| `cs` | 片选引脚（port + pin） |
| `Init` | 硬件初始化函数指针 |
| `CS_Write` | 片选写函数指针 |
| `Transmit` | SPI 发送函数指针 |
| `Transmit_Receive` | SPI 收发函数指针 |

---

## BSP 层 - W25Q128 驱动

### bsp_w25qxx.h / bsp_w25qxx.c

| API | 说明 | 参数 | 返回值 |
|-----|------|------|--------|
| `W25Q_Init_Device(w25q_t *flash)` | 初始化 W25Q 芯片，等待就绪并验证 JEDEC ID | flash: W25Q 对象指针 | `W25Q_OK` / `W25Q_ERR` |
| `W25Q_Read_JEDEC_ID(w25q_t *flash)` | 读取 JEDEC ID（0x9F），W25Q128 应返回 `0xEF4018` | flash: W25Q 对象指针 | 24 位 JEDEC ID |
| `W25Q_Read_Device_ID(w25q_t *flash)` | 读取设备 ID（0x90），W25Q128 应返回 `0x0017` | flash: W25Q 对象指针 | 16 位设备 ID |
| `W25Q_Read_Byte(w25q_t *flash, uint32_t addr)` | 读取单个字节 | addr: 24 位地址 | 读取到的字节 |
| `W25Q_Read_Buffer(w25q_t *flash, uint32_t addr, uint8_t* buf, uint16_t len)` | 从 Flash 读取任意长度数据 | addr: 起始地址, buf: 接收缓冲区, len: 长度 | `W25Q_OK` / `W25Q_ERR` |
| `W25Q_Sector_Erase(w25q_t *flash, uint32_t addr)` | 擦除 4K 扇区（必须 4K 对齐） | addr: 扇区起始地址（4K 对齐） | `W25Q_OK` / `W25Q_ERR` |
| `W25Q_Chip_Erase(w25q_t *flash)` | 擦除整个芯片 | flash: W25Q 对象指针 | `W25Q_OK` / `W25Q_ERR` |
| `W25Q_Page_Program(w25q_t *flash, uint32_t addr, const uint8_t* data, uint16_t len)` | 页编程写入（最多 256 字节，禁止跨页） | addr: 地址, data: 数据, len: 长度（≤256） | `W25Q_OK` / `W25Q_ERR` |
| `W25Q_Write_MultiPage(w25q_t *flash, uint32_t addr, const uint8_t* data, uint16_t len)` | 自动跨页写入（内部按 256 字节分页循环） | addr: 起始地址, data: 数据, len: 长度 | `W25Q_OK` / `W25Q_ERR` |

### 返回值宏

| 宏 | 值 | 说明 |
|----|----|------|
| `W25Q_OK` | 0 | 操作成功 |
| `W25Q_ERR` | 1 | 操作失败（NULL 指针/SPI 错误/超时/对齐错误） |

### 关键参数

| 宏 | 值 | 说明 |
|----|----|------|
| `W25Q_PAGE_SIZE` | 256 | 页大小（字节） |
| `W25Q_SECTOR_SIZE` | 4096 | 扇区大小（字节） |

---

## APP 层 - SPI 硬件绑定

### app_spi.h / app_spi.c

| API | 说明 | 参数 | 返回值 |
|-----|------|------|--------|
| `App_SPI_System_Init(void)` | 初始化 SPI 总线（拉高 CS，注册函数指针） | 无 | 无 |

| 宏 | 默认值 | 说明 |
|----|--------|------|
| `SPI_BUS_HANDLE` | `&hspi1` | SPI1 句柄 |
| `SPI_CS_PORT` | `GPIOA` | CS 端口 |
| `SPI_CS_PIN` | `GPIO_PIN_4` | CS 引脚 |

| 全局变量 | 说明 |
|----------|------|
| `SPI_Bus` | SPI 总线实例（外部可引用） |

---

## APP 层 - W25Q128 业务入口

### app_w25qxx.h / app_w25qxx.c

| API | 说明 | 参数 | 返回值 |
|-----|------|------|--------|
| `App_W25Qxx_System_Init(void)` | 上电初始化：先 SPI 总线，再 W25Q 芯片 | 无 | 无 |
| `App_W25Qxx_Get_JEDEC_ID(void)` | 查询 JEDEC ID（用于验证通信是否正常） | 无 | 24 位 JEDEC ID |

| 全局变量 | 说明 |
|----------|------|
| `W25Q_Flash` | W25Q128 设备实例（外部可直接调用 BSP API） |

---

## 接线表

### SPI1 - W25Q128

| STM32F407 引脚 | 功能 | W25Q128 引脚 | 说明 |
|----------------|------|-------------|------|
| PA4 | CS（片选） | CS（引脚 1） | CubeMX 配为 GPIO_Output，初始高电平 |
| PA5 | SCK（时钟） | CLK（引脚 6） | CubeMX 配为 SPI1_SCK |
| PA6 | MISO（主入从出） | DO（引脚 2） | CubeMX 配为 SPI1_MISO |
| PA7 | MOSI（主出从入） | DI（引脚 5） | CubeMX 配为 SPI1_MOSI |
| VCC | 电源 | VCC（引脚 8） | 3.3V |
| GND | 地 | GND（引脚 4） | 共地 |

### 其他引脚

| STM32F407 引脚 | 功能 | 说明 |
|----------------|------|------|
| PA1 | 输出 | 当前用途待定 |
| PA2 | 输出 | 当前用途待定 |
| PB2 | 输出 | LED（当前 LED 任务使用） |
| PB8 | 输出 | 当前用途待定 |
| PB9 | 输出 | 当前用途待定 |
| PA9 | USART1_TX | 串口调试输出 |
| PA10 | USART1_RX | 串口调试输入 |
