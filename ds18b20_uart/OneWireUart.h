/**
    @author Stanislav Lakhtin
    @date   11.07.2016
    @brief  Реализация протокола 1wire на базе библиотеки libopencm3 для микроконтроллера STM32F103
            Возможно, библиотека будет корректно работать и на других uK (требуется проверка).
            Общая идея заключается в использовании аппаратного USART uK для иммитации работы 1wire.
            Подключение устройств осуществляется на выбранный USART к TX пину, который должен быть подтянут к линии питания сопротивлением 4.7К.
            Реализация библиотеки осуществляет замыкание RX на TX внутри uK, оставляя ножку RX доступной для использования в других задачах.
 */

#ifndef STM32_DS18X20_ONEWIRE_H
#define STM32_DS18X20_ONEWIRE_H

#include <stdint.h>
#include "stm32f1xx_hal.h"

#define ONEWIRE_NOBODY 0xF0
#define ONEWIRE_SEARCH 0xF0
#define ONEWIRE_SKIP_ROM 0xCC
#define ONEWIRE_READ_ROM 0x33
#define ONEWIRE_MATCH_ROM 0x55
#define ONEWIRE_CONVERT_TEMPERATURE 0x44
#define ONEWIRE_READ_SCRATCHPAD 0xBE
#define ONEWIRE_WRITE_SCRATCHPAD 0x4E
#define ONEWIRE_COPY_SCRATCHPAD 0x48
#define ONEWIRE_RECALL_E2 0xB8

#define DS18B20 0x28
#define DS18S20 0x10

#define WIRE_0 0x00
#define WIRE_1 0xFF
#define OW_READ 0xFF

#define ONEWIRE_MAX_BUS_COUNT 5U
#define ONEWIRE_MAX_DEVICES_LIMIT 8U
#define ONEWIRE_POSMAP_FLASH_ADDR 0x0800FC00UL

typedef struct {
  int8_t inCelsus;
  uint8_t frac;
} Temperature; //

typedef struct {
  uint8_t family;
  uint8_t code[6];
  uint8_t crc;
} RomCode; //

typedef struct {
  uint8_t crc;
  uint8_t reserved[3];
  uint8_t configuration;
  uint8_t tl;
  uint8_t th;
  uint8_t temp_msb;
  uint8_t temp_lsb;
} Scratchpad_DS18B20;//

typedef struct {
  uint8_t crc;
  uint8_t count_per;
  uint8_t count_remain;
  uint8_t reserved[2];
  uint8_t tl;
  uint8_t th;
  uint8_t temp_msb;
  uint8_t temp_lsb;
} Scratchpad_DS18S20;//

/**
 * @brief Callback để thông báo tiến trình học vị trí từng cảm biến.
 * @param pos   Vị trí đang học (1-based).
 * @param found 0 = đang yêu cầu người dùng hơ nóng, 1 = đã tìm thấy vị trí này.
 */
typedef void (*owLearnProgressCb)(uint8_t pos, uint8_t found);

typedef struct {
  UART_HandleTypeDef *huart;
  uint8_t maxDevices;
  uint32_t baudData;
  uint32_t baudReset;
  owLearnProgressCb learnCb; /**< Callback tiến trình học vị trí (có thể NULL) */
} OneWire_Config;

typedef struct {
  RomCode romIds[ONEWIRE_MAX_DEVICES_LIMIT];
  RomCode positionRom[ONEWIRE_MAX_DEVICES_LIMIT];
  float temp[ONEWIRE_MAX_DEVICES_LIMIT];
  uint8_t lastROM[8];
  int lastDiscrepancy;
  uint8_t devices;
  uint8_t positionCount;
  uint8_t positionValid;
  volatile uint8_t recvPending;
  volatile uint16_t rxWord;
  uint8_t rxByte;
} OneWire_Context;

typedef struct {
	int device;
	char info[30];
}DEVInfo;

HAL_StatusTypeDef owInit(OneWire_Config *cfg, OneWire_Context *ctx);

HAL_StatusTypeDef owRegisterBus(OneWire_Config *cfg, OneWire_Context *ctx);

OneWire_Context *owGetContextByUart(UART_HandleTypeDef *huart);

uint16_t owResetCmd(OneWire_Config *cfg, OneWire_Context *ctx);

int owSearchCmd(OneWire_Config *cfg, OneWire_Context *ctx);

void owSkipRomCmd(OneWire_Config *cfg, OneWire_Context *ctx);

uint8_t owCRC8(RomCode *rom);

HAL_StatusTypeDef owMatchRomCmd(OneWire_Config *cfg, OneWire_Context *ctx, RomCode *rom);

void owConvertTemperatureCmd(OneWire_Config *cfg, OneWire_Context *ctx, RomCode *rom);

uint8_t* owReadScratchpadCmd(OneWire_Config *cfg, OneWire_Context *ctx, RomCode *rom, uint8_t *data);

void owCopyScratchpadCmd(OneWire_Config *cfg, OneWire_Context *ctx, RomCode *rom);

void owRecallE2Cmd(OneWire_Config *cfg, OneWire_Context *ctx, RomCode *rom);

Temperature readTemperature(OneWire_Config *cfg, OneWire_Context *ctx, RomCode *rom, uint8_t reSense);

void owSend(OneWire_Config *cfg, OneWire_Context *ctx, uint16_t data);

void owSendByte(OneWire_Config *cfg, OneWire_Context *ctx, uint8_t data);

uint16_t owEchoRead(OneWire_Config *cfg, OneWire_Context *ctx);

void owReadHandler(UART_HandleTypeDef *huart);

/**
 * owErrorHandler – gọi từ HAL_UART_ErrorCallback trong main.c.
 * Xóa cờ ORE/FE/NE, giải phóng owEchoRead, tái kích hoạt RX interrupt.
 */
void owErrorHandler(UART_HandleTypeDef *huart);

int get_ROMid(OneWire_Config *cfg, OneWire_Context *ctx);

void get_Temperature(OneWire_Config *cfg, OneWire_Context *ctx);

HAL_StatusTypeDef owInitSensorPositions(OneWire_Config *cfg, OneWire_Context *ctx, uint8_t forceRelearn);

HAL_StatusTypeDef owGetTemperatureByPosition(OneWire_Config *cfg, OneWire_Context *ctx);

#endif //STM32_DS18X20_ONEWIRE_H
