#include "ds18b20_app.h"
#include "app_menu.h"
#include "itm.h"

/* Route all itm_print/itm_put_int calls in this TU through ITM_LIB_DS18B20 */
#undef  itm_print
#define itm_print(s)   itm_print_library(ITM_LIB_DS18B20, (s))
#undef  itm_put_int
#define itm_put_int(n) itm_put_int_library(ITM_LIB_DS18B20, (n))

/* =========================================================================
 * Learn-progress binding (kết nối tiến trình học vị trí với menu context)
 * ========================================================================= */

static app_menu_ctx_t *s_menu_ctx = NULL;

static void Ds18b20_LearnProgressCb(uint8_t pos, uint8_t found)
{
  if (s_menu_ctx != NULL)
  {
    s_menu_ctx->relearn_current_pos = pos;
    s_menu_ctx->relearn_pos_found   = found;
  }
}

/**
 * @brief Liên kết menu context với bus OneWire để tự động cập nhật tiến trình học vị trí.
 * @note  Gọi một lần sau Ds18b20Api_Init, trước khi bắt đầu học vị trí.
 */
void Ds18b20Api_BindMenuCtx(OneWire_Config *cfg, void *menu_ctx)
{
  s_menu_ctx = (app_menu_ctx_t *)menu_ctx;
  if (cfg != NULL)
  {
    cfg->learnCb = Ds18b20_LearnProgressCb;
  }
}

/**
 * @brief Fault callback tích hợp: in ITM + đánh dấu lỗi vào menu context để LCD hiển thị.
 */
static void Ds18b20_FaultCb(uint8_t sensorIndex, int16_t tempDeciC)
{
  /* In ra ITM như bình thường */
  Ds18b20Api_DefaultOnWireFault(sensorIndex, tempDeciC);

  /* Đánh dấu lỗi trong menu context để LCD hiển thị cảnh báo */
  if (s_menu_ctx != NULL && sensorIndex >= 1U)
  {
    uint8_t idx = (uint8_t)(sensorIndex - 1U);
    if (idx < MENU_DS18B20_MAX)
    {
      s_menu_ctx->ds18b20_fault_mask |= (uint8_t)(1U << idx);
    }
  }
}

Ds18b20FaultCallback Ds18b20Api_GetFaultCb(void)
{
  return Ds18b20_FaultCb;
}

void Ds18b20Api_DefaultOnWireFault(uint8_t sensorIndex, int16_t tempDeciC)
{
  itm_print("[DS18B20][FAULT] sensor ");
  itm_put_int((int)sensorIndex);

  if (tempDeciC == -2) {
    itm_print(" disconnected (bus stuck LOW: VCC or DATA wire suspected)\r\n");
  } else if (tempDeciC == -1) {
    itm_print(" disconnected (bus floating HIGH: GND suspected)\r\n");
  } else if (tempDeciC == -3) {
    itm_print(" read error (DATA wire disconnected / CRC fail)\r\n");
  } else {
    itm_print(" disconnected\r\n");
  }
}

void Ds18b20Api_PrintItem(const Ds18b20QueueItem *item)
{
  int16_t absDeci;

  if (item == NULL) {
    return;
  }

  absDeci = item->tempDeciC;

  itm_print("[DS18B20] sensor ");
  itm_put_int((int)item->sensorIndex);
  itm_print(" = ");

  if (absDeci < 0) {
    itm_print("-");
    absDeci = (int16_t)(-absDeci);
  }

  itm_put_int(absDeci / 10);
  itm_print(".");
  itm_put_int(absDeci % 10);
  itm_print(" C, tick=");
  itm_put_int((int)item->tick);
  itm_print("\r\n");
}

typedef struct {
  OneWire_Config *cfg;
  OneWire_Context *ctx;
  uint8_t sensorsReady;
  uint8_t forceRelearn;
  uint8_t noDeviceLogged;      /* 1 = "no device" message already printed, suppress repeats */
  uint8_t ensureReadyLogged;   /* 1 = "devices found/not enough/init failed" already printed once,
                                  suppress repeats until sensors are fully ready again */
  uint8_t faultLoggedMask;     /* bit i = 1 means FAULT for sensor (i+1) already printed */
} Ds18b20StateSlot;

static Ds18b20StateSlot s_dsState[ONEWIRE_MAX_BUS_COUNT];

static Ds18b20StateSlot *Ds18b20State_GetSlot(OneWire_Config *cfg, OneWire_Context *ctx, uint8_t createIfMissing)
{
  uint8_t i;
  Ds18b20StateSlot *freeSlot = NULL;

  for (i = 0U; i < ONEWIRE_MAX_BUS_COUNT; i++) {
    if (s_dsState[i].cfg == cfg && s_dsState[i].ctx == ctx) {
      return &s_dsState[i];
    }
    if (freeSlot == NULL && s_dsState[i].cfg == NULL && s_dsState[i].ctx == NULL) {
      freeSlot = &s_dsState[i];
    }
  }

  if (createIfMissing != 0U && freeSlot != NULL) {
    freeSlot->cfg = cfg;
    freeSlot->ctx = ctx;
    freeSlot->sensorsReady = 0U;
    freeSlot->forceRelearn = 0U;
    freeSlot->noDeviceLogged = 0U;
    freeSlot->ensureReadyLogged = 0U;
    freeSlot->faultLoggedMask = 0U;
    return freeSlot;
  }

  return NULL;
}

HAL_StatusTypeDef Ds18b20Api_Init(OneWire_Config *cfg, OneWire_Context *ctx)
{
  Ds18b20StateSlot *slot;

  if (cfg == NULL || ctx == NULL || cfg->huart == NULL || cfg->maxDevices == 0U) {
    return HAL_ERROR;
  }

  slot = Ds18b20State_GetSlot(cfg, ctx, 1U);
  if (slot == NULL) {
    return HAL_ERROR;
  }
  slot->sensorsReady = 0U;
  slot->forceRelearn = 0U;
  slot->noDeviceLogged = 0U;
  slot->ensureReadyLogged = 0U;
  slot->faultLoggedMask = 0U;

  return owInit(cfg, ctx);
}

HAL_StatusTypeDef Ds18b20Api_RequestRelearn(OneWire_Config *cfg, OneWire_Context *ctx)
{
  Ds18b20StateSlot *slot;

  if (cfg == NULL || ctx == NULL) {
    return HAL_ERROR;
  }

  slot = Ds18b20State_GetSlot(cfg, ctx, 1U);
  if (slot == NULL) {
    return HAL_ERROR;
  }

  slot->forceRelearn = 1U;
  slot->sensorsReady = 0U;
  ctx->positionValid = 0U;
  ctx->positionCount = 0U;
  itm_print("[DS18B20] relearn requested for this bus\r\n");
  return HAL_OK;
}

HAL_StatusTypeDef Ds18b20Api_EnsureReady(OneWire_Config *cfg, OneWire_Context *ctx)
{
  Ds18b20StateSlot *slot;

  if (cfg == NULL || ctx == NULL) {
    return HAL_ERROR;
  }

  slot = Ds18b20State_GetSlot(cfg, ctx, 1U);
  if (slot == NULL) {
    return HAL_ERROR;
  }

  if (get_ROMid(cfg, ctx) != 0) {
    /* Print "no device" only on the first failed attempt, not every retry. */
    if (slot->noDeviceLogged == 0U) {
      itm_print("[DS18B20] no device, waiting for sensors...\r\n");
      slot->noDeviceLogged = 1U;
    }
    return HAL_ERROR;
  }

  /* ── Devices found on bus ────────────────────────────────────────────────
   * Do NOT clear noDeviceLogged or faultLoggedMask here.
   *
   * Bug if we clear here: bus is unstable after disconnection, so get_ROMid
   * alternates between failing (no device) and succeeding with a phantom
   * device (1 ghost ROM during the electrical transient).  Every time it
   * succeeds we would reset noDeviceLogged → 0, so the very next failure
   * prints "no device" again → cleared again → loop prints the message on
   * every other poll cycle until sensors are truly reconnected.
   *
   * Correct place to clear suppression flags is after FULL success below,
   * when we are certain real sensors are present and position map is valid. */

  /* Print device count + any subsequent retry-phase messages only ONCE per
   * waiting cycle. ensureReadyLogged is cleared only when fully ready, so
   * repeated "devices found: N / not enough / position init failed" lines
   * that appear every 1 s during recovery no longer flood the ITM console. */
  if (slot->ensureReadyLogged == 0U) {
    itm_print("[DS18B20] devices found: ");
    itm_put_int((int)ctx->devices);
    itm_print("\r\n");
  }

  if (ctx->devices < cfg->maxDevices) {
    if (slot->ensureReadyLogged == 0U) {
      itm_print("[DS18B20][WARN] missing sensor(s): expected ");
      itm_put_int((int)cfg->maxDevices);
      itm_print(", detected ");
      itm_put_int((int)ctx->devices);
      itm_print(". Waiting for enough sensors...\r\n");
      slot->ensureReadyLogged = 1U;
    }
    return HAL_ERROR;
  }

  if (owInitSensorPositions(cfg, ctx, slot->forceRelearn) != HAL_OK) {
    if (slot->ensureReadyLogged == 0U) {
      itm_print("[DS18B20][ERR] position init failed, waiting and retry...\r\n");
      slot->ensureReadyLogged = 1U;
    }
    return HAL_ERROR;
  }

  /* Fully ready — NOW clear all suppression flags so the next disconnect
   * cycle starts fresh: "no device" and FAULT messages will appear again,
   * and the device-count/position-init path will log once more if needed. */
  slot->noDeviceLogged = 0U;
  slot->faultLoggedMask = 0U;
  slot->ensureReadyLogged = 0U;
  slot->forceRelearn = 0U;

  itm_print("[DS18B20] all sensors ready, start temperature polling\r\n");
  return HAL_OK;
}

HAL_StatusTypeDef Ds18b20Api_ReadAndQueue(OneWire_Config *cfg, OneWire_Context *ctx, osMessageQueueId_t queueId)
{
  uint8_t i;
  Ds18b20QueueItem item;

  if (cfg == NULL || ctx == NULL || queueId == NULL) {
    return HAL_ERROR;
  }

  if (owGetTemperatureByPosition(cfg, ctx) != HAL_OK) {
    itm_print("[DS18B20][WARN] read by position failed, re-detect sensors...\r\n");
    ctx->positionValid = 0U;
    ctx->positionCount = 0U;
    return HAL_ERROR;
  }

  for (i = 0; i < cfg->maxDevices; i++) {
    item.sensorIndex = (uint8_t)(i + 1U);
    item.tempDeciC = (int16_t)(ctx->temp[i] * 10.0f);
    /* Skip fault-marker values (-0.1 / -0.2 / -0.3 °C → tempDeciC -1/-2/-3).
     * These are already reported once by the onFault callback in
     * Ds18b20Api_Service.  Queuing them would cause TaskITMconsole to print
     * an additional "sensor N = -0.X C" line every polling cycle while the
     * bus is disconnected, cluttering the console unnecessarily. */
    if ((item.tempDeciC == -1) || (item.tempDeciC == -2) || (item.tempDeciC == -3)) {
      continue;
    }
    item.tick = osKernelGetTickCount();
    (void)osMessageQueuePut(queueId, &item, 0U, 0U);
  }

  return HAL_OK;
}

static uint8_t Ds18b20_IsWireFaultMarker(int16_t tempDeciC)
{
  return (uint8_t)((tempDeciC == -1) || (tempDeciC == -2) || (tempDeciC == -3));
}

HAL_StatusTypeDef Ds18b20Api_Service(OneWire_Config *cfg, OneWire_Context *ctx, osMessageQueueId_t queueId, Ds18b20FaultCallback onFault)
{
  Ds18b20StateSlot *slot;
  uint8_t i;

  if (cfg == NULL || ctx == NULL || queueId == NULL) {
    return HAL_ERROR;
  }

  slot = Ds18b20State_GetSlot(cfg, ctx, 1U);
  if (slot == NULL) {
    return HAL_ERROR;
  }

  if (slot->sensorsReady == 0U) {
    if (Ds18b20Api_EnsureReady(cfg, ctx) != HAL_OK) {
      return HAL_ERROR;
    }
    slot->sensorsReady = 1U;
  }

  if (Ds18b20Api_ReadAndQueue(cfg, ctx, queueId) != HAL_OK) {
    slot->sensorsReady = 0U;
    /*
     * ReadAndQueue thất bại nghĩa là owGetTemperatureByPosition() trả về
     * HAL_ERROR — tức là position map không hợp lệ (positionValid = 0) hoặc
     * ROM không còn trong cache.  Đây là tình huống bus chết hoàn toàn TRƯỚC
     * khi kịp lưu giá trị fault vào ctx->temp[].
     *
     * Nếu không gọi onFault ở đây, ITM Console sẽ im lặng hoàn toàn vì:
     *  - Không có item nào được đưa vào queue → TaskITMconsole không in gì.
     *  - Vòng lặp kiểm tra ctx->temp[] bên dưới bị bỏ qua (return sớm).
     *
     * Giải pháp: Kích hoạt onFault cho tất cả sensor với mã -1
     * ("bus floating HIGH / GND wire suspected") để ITM Console vẫn in log
     * và người dùng biết hệ thống đang trong trạng thái lỗi.
     */
    if (onFault != NULL) {
      for (i = 0U; i < cfg->maxDevices; i++) {
        uint8_t bit = (uint8_t)(1U << i);
        if ((slot->faultLoggedMask & bit) == 0U) {
          onFault((uint8_t)(i + 1U), -1);
          slot->faultLoggedMask |= bit;
        }
      }
    }
    return HAL_ERROR;
  }

  if (onFault != NULL) {
    for (i = 0U; i < cfg->maxDevices; i++) {
      int16_t tempDeciC = (int16_t)(ctx->temp[i] * 10.0f);
      if (Ds18b20_IsWireFaultMarker(tempDeciC) != 0U) {
        /* Print FAULT message only on the first occurrence per sensor. */
        uint8_t bit = (uint8_t)(1U << i);
        if ((slot->faultLoggedMask & bit) == 0U) {
          onFault((uint8_t)(i + 1U), tempDeciC);
          slot->faultLoggedMask |= bit;
        }
        slot->sensorsReady = 0U;
        ctx->positionValid = 0U;
        ctx->positionCount = 0U;
      }
    }
  }

  return HAL_OK;
}