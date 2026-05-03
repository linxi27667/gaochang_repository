# 丝杆举升机 PC 模拟测试 Makefile
# 用法: make && ./sim_test

CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra
INC     = -I ../APP/Inc -I ../Driver/Inc -I ../BSP/Inc -I ../Core/Inc -I .

# 桩代码
SIM_SRC = main.c stm32f4xx_hal.c bsp_w25qxx.c app_spi.c \
          elog.c SEGGER_RTT.c cmsis_os.c mx_init.c

# 真实的 APP 层代码
APP_SRC = ../APP/Src/motor.c ../APP/Src/key.c ../APP/Src/encoder.c \
          ../APP/Src/balance.c ../APP/Src/safety.c ../APP/Src/app_w25qxx.c

SRC = $(SIM_SRC) $(APP_SRC)
OBJ = $(SRC:.c=.o)
TARGET = sim_test

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) $(INC) -c -o $@ $<

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(OBJ)
