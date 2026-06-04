#ifndef DS18B20_APP_H
#define DS18B20_APP_H

#include "cmsis_os.h"
#include <OneWireUart.h>
#include "itm.h"

/* Forward declaration để tránh include vòng tròn */
struct app_menu_ctx_t;

typedef struct {
  uint8_t sensorIndex;
  int16_t tempDeciC;
  uint32_t tick;
} Ds18b20QueueItem;

typedef void (*Ds18b20FaultCallback)(uint8_t sensorIndex, int16_t tempDeciC);

void Ds18b20Api_DefaultOnWireFault(uint8_t sensorIndex, int16_t tempDeciC);
void Ds18b20Api_PrintItem(const Ds18b20QueueItem *item);

HAL_StatusTypeDef Ds18b20Api_Init(OneWire_Config *cfg, OneWire_Context *ctx);
HAL_StatusTypeDef Ds18b20Api_RequestRelearn(OneWire_Config *cfg, OneWire_Context *ctx);
HAL_StatusTypeDef Ds18b20Api_EnsureReady(OneWire_Config *cfg, OneWire_Context *ctx);
HAL_StatusTypeDef Ds18b20Api_ReadAndQueue(OneWire_Config *cfg, OneWire_Context *ctx, osMessageQueueId_t queueId);
HAL_StatusTypeDef Ds18b20Api_Service(OneWire_Config *cfg, OneWire_Context *ctx, osMessageQueueId_t queueId, Ds18b20FaultCallback onFault);

/**
 * @brief Liên kết menu context với bus để cập nhật tiến trình học vị trí lên LCD.
 * @param cfg      Con trỏ cấu hình bus OneWire (sẽ gắn learnCb tự động).
 * @param menu_ctx Con trỏ app_menu_ctx_t (truyền dưới dạng void* để tránh include vòng).
 */
void Ds18b20Api_BindMenuCtx(OneWire_Config *cfg, void *menu_ctx);

/**
 * @brief Trả về fault callback tích hợp: in ITM + đánh dấu lỗi lên LCD.
 * @note  Dùng thay cho Ds18b20Api_DefaultOnWireFault khi đã gọi BindMenuCtx.
 */
Ds18b20FaultCallback Ds18b20Api_GetFaultCb(void);

#endif
