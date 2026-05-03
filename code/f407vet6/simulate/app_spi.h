#ifndef SIM_APP_SPI_H
#define SIM_APP_SPI_H
#include "bsp_w25qxx.h"
extern spi_bus_t SPI_Bus;
extern const char W25Q_CS_GPIO_Port;
#define W25Q_CS_Pin  0
void App_SPI_System_Init(void);
#endif
