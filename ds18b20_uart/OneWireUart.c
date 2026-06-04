#include <OneWireUart.h>
#include "cmsis_os2.h"
#include "stdio.h"
#include "string.h"
#include "stddef.h"
#include "itm.h"

#define ONEWIRE_POSMAP_MAGIC 0x504F5331UL
#define ONEWIRE_POSMAP_VERSION 1U
#define ONEWIRE_HEAT_DELTA_C 1.0f
#define ONEWIRE_HEAT_CONFIRM_DELTA_C 0.5f
#define ONEWIRE_HEAT_POLL_MS 500U
#define ONEWIRE_HEAT_TIMEOUT_MS 60000U
#define ONEWIRE_LEARN_WARMUP_SAMPLES 1U
#define ONEWIRE_LEARN_WARMUP_DELAY_MS 0U
#define ONEWIRE_HEAT_MIN_ELAPSED_MS 1000U
#define ONEWIRE_HEAT_REQUIRED_HITS 1U
#define DS18B20_CFG_RES_12BIT 0x7FU

typedef struct {
  uint32_t magic;
  uint16_t version;
  uint16_t count;
  RomCode rom[ONEWIRE_MAX_DEVICES_LIMIT];
  uint32_t checksum;
} OneWire_PositionMapFlash;

typedef struct {
  OneWire_Config *cfg;
  OneWire_Context *ctx;
} OneWire_BusBinding;

static OneWire_BusBinding s_busBindings[ONEWIRE_MAX_BUS_COUNT];

static void owItmPutHexByte(uint8_t v) {
  char out[3];
  static const char hex[] = "0123456789ABCDEF";

  out[0] = hex[(v >> 4) & 0x0FU];
  out[1] = hex[v & 0x0FU];
  out[2] = '\0';
  itm_print(out);
}

static void owItmPrintRom(const RomCode *rom) {
  const uint8_t *p;
  uint8_t i;

  if (rom == NULL) {
    itm_print("<null>");
    return;
  }

  p = (const uint8_t *)rom;
  for (i = 0U; i < 8U; i++) {
    owItmPutHexByte(p[i]);
    if (i < 7U) {
      itm_print("-");
    }
  }
}

static HAL_StatusTypeDef owEnsureDs18b20Resolution12bit(OneWire_Config *cfg, OneWire_Context *ctx) {
  uint8_t i;
  uint8_t updatedCount = 0U;

  if (cfg == NULL || ctx == NULL) {
    return HAL_ERROR;
  }

  if (get_ROMid(cfg, ctx) != 0 || ctx->devices == 0U) {
    itm_print("[DS18B20][WARN] no sensor found while applying 12-bit config\r\n");
    return HAL_ERROR;
  }

  for (i = 0; i < ctx->devices && i < cfg->maxDevices; i++) {
    uint8_t pad[9];
    uint8_t th;
    uint8_t tl;
    uint8_t cfgReg;

    if (ctx->romIds[i].family != DS18B20) {
      continue;
    }

    owReadScratchpadCmd(cfg, ctx, &ctx->romIds[i], pad);
    th = pad[6];
    tl = pad[5];
    cfgReg = pad[4];

    if ((cfgReg & 0x60U) == 0x60U) {
      continue;
    }

    owMatchRomCmd(cfg, ctx, &ctx->romIds[i]);
    owSendByte(cfg, ctx, ONEWIRE_WRITE_SCRATCHPAD);
    owSendByte(cfg, ctx, th);
    owSendByte(cfg, ctx, tl);
    owSendByte(cfg, ctx, DS18B20_CFG_RES_12BIT);

    owCopyScratchpadCmd(cfg, ctx, &ctx->romIds[i]);
    osDelay(20U);

    owReadScratchpadCmd(cfg, ctx, &ctx->romIds[i], pad);
    if ((pad[4] & 0x60U) == 0x60U) {
      updatedCount++;
    } else {
      itm_print("[DS18B20][WARN] failed to set 12-bit on a sensor\r\n");
    }
  }

  if (updatedCount > 0U) {
    itm_print("[DS18B20] set 12-bit resolution for sensor(s): ");
    itm_put_int((int)updatedCount);
    itm_print("\r\n");
  }

  return HAL_OK;
}

static uint8_t owRomEqual(const RomCode *a, const RomCode *b) {
  return (uint8_t)((a != NULL && b != NULL && memcmp(a, b, sizeof(RomCode)) == 0) ? 1U : 0U);
}

static int owFindRomIndex(const OneWire_Context *ctx, const RomCode *rom) {
  uint8_t i;

  if (ctx == NULL || rom == NULL) {
    return -1;
  }

  for (i = 0; i < ctx->devices; i++) {
    if (owRomEqual(&ctx->romIds[i], rom) != 0U) {
      return (int)i;
    }
  }

  return -1;
}

static uint32_t owMapChecksum(const uint8_t *data, uint32_t len) {
  uint32_t sum = 0xA5A55A5AUL;
  uint32_t i;

  for (i = 0; i < len; i++) {
    sum = (sum << 5) | (sum >> 27);
    sum ^= data[i];
  }

  return sum;
}

static HAL_StatusTypeDef owLoadPositionMap(OneWire_Config *cfg, OneWire_Context *ctx) {
  const OneWire_PositionMapFlash *stored;
  uint32_t expectedChecksum;
  uint8_t i;

  (void)cfg;
  if (ctx == NULL) {
    return HAL_ERROR;
  }

  stored = (const OneWire_PositionMapFlash *)ONEWIRE_POSMAP_FLASH_ADDR;
  if (stored->magic != ONEWIRE_POSMAP_MAGIC ||
      stored->version != ONEWIRE_POSMAP_VERSION ||
      stored->count == 0U ||
      stored->count > ONEWIRE_MAX_DEVICES_LIMIT) {
    return HAL_ERROR;
  }

  expectedChecksum = owMapChecksum((const uint8_t *)stored, (uint32_t)offsetof(OneWire_PositionMapFlash, checksum));
  if (expectedChecksum != stored->checksum) {
    return HAL_ERROR;
  }

  for (i = 0; i < stored->count; i++) {
    if (owFindRomIndex(ctx, &stored->rom[i]) < 0) {
      return HAL_ERROR;
    }
    ctx->positionRom[i] = stored->rom[i];
  }

  ctx->positionCount = (uint8_t)stored->count;
  ctx->positionValid = 1U;
  return HAL_OK;
}

static HAL_StatusTypeDef owSavePositionMap(const OneWire_Config *cfg, const OneWire_Context *ctx) {
  OneWire_PositionMapFlash map;
  FLASH_EraseInitTypeDef eraseInit;
  uint32_t pageError = 0U;
  uint32_t addr;
  uint32_t i;
  uint32_t words;
  const uint32_t *src;

  if (cfg == NULL || ctx == NULL || ctx->positionValid == 0U) {
    return HAL_ERROR;
  }

  memset(&map, 0, sizeof(map));
  map.magic = ONEWIRE_POSMAP_MAGIC;
  map.version = ONEWIRE_POSMAP_VERSION;
  map.count = ctx->positionCount;
  for (i = 0; i < map.count && i < cfg->maxDevices; i++) {
    map.rom[i] = ctx->positionRom[i];
  }
  map.checksum = owMapChecksum((const uint8_t *)&map, (uint32_t)offsetof(OneWire_PositionMapFlash, checksum));

  if (HAL_FLASH_Unlock() != HAL_OK) {
    return HAL_ERROR;
  }

  eraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
  eraseInit.PageAddress = ONEWIRE_POSMAP_FLASH_ADDR;
  eraseInit.NbPages = 1;
  if (HAL_FLASHEx_Erase(&eraseInit, &pageError) != HAL_OK) {
    (void)HAL_FLASH_Lock();
    return HAL_ERROR;
  }

  addr = ONEWIRE_POSMAP_FLASH_ADDR;
  src = (const uint32_t *)&map;
  words = (uint32_t)((sizeof(map) + 3U) / 4U);
  for (i = 0; i < words; i++) {
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, src[i]) != HAL_OK) {
      (void)HAL_FLASH_Lock();
      return HAL_ERROR;
    }
    addr += 4U;
  }

  (void)HAL_FLASH_Lock();
  return HAL_OK;
}

static uint16_t USART_ReceiveData(USART_TypeDef *USARTx) {
  assert_param(IS_USART_ALL_PERIPH(USARTx));
  return (uint16_t)(USARTx->DR & (uint16_t)0x01FF);
}

static void USART_SendData(USART_TypeDef *USARTx, uint16_t Data) {
  assert_param(IS_USART_ALL_PERIPH(USARTx));
  assert_param(IS_USART_DATA(Data));
  USARTx->DR = (Data & (uint16_t)0x01FF);
}

static OneWire_BusBinding *owFindBindingByHandle(UART_HandleTypeDef *huart) {
  uint32_t i;
  if (huart == NULL) {
    return NULL;
  }
  for (i = 0; i < ONEWIRE_MAX_BUS_COUNT; i++) {
    if (s_busBindings[i].cfg != NULL && s_busBindings[i].cfg->huart == huart) {
      return &s_busBindings[i];
    }
  }
  return NULL;
}

static OneWire_BusBinding *owFindBindingByInstance(USART_TypeDef *instance) {
  uint32_t i;
  if (instance == NULL) {
    return NULL;
  }
  for (i = 0; i < ONEWIRE_MAX_BUS_COUNT; i++) {
    if (s_busBindings[i].cfg != NULL &&
        s_busBindings[i].cfg->huart != NULL &&
        s_busBindings[i].cfg->huart->Instance == instance) {
      return &s_busBindings[i];
    }
  }
  return NULL;
}

/**
 * Re-init UART at new baud and immediately re-arm RX interrupt.
 * HAL_HalfDuplex_Init resets the peripheral (disables all IRQs), so we
 * MUST call HAL_UART_Receive_IT again afterwards — otherwise owEchoRead
 * will spin-wait for 1 s per slot (1000 × osDelay(1)) and the whole
 * ROM search hangs for ~3 minutes.
 */
static HAL_StatusTypeDef owUsartSetup(OneWire_Config *cfg, OneWire_Context *ctx, uint32_t baud) {
  UART_HandleTypeDef *huart;

  if (cfg == NULL || cfg->huart == NULL) {
    return HAL_ERROR;
  }

  huart = cfg->huart;
  huart->Init.BaudRate = baud;
  huart->Init.WordLength = UART_WORDLENGTH_8B;
  huart->Init.StopBits = UART_STOPBITS_1;
  huart->Init.Parity = UART_PARITY_NONE;
  huart->Init.Mode = UART_MODE_TX_RX;
  huart->Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart->Init.OverSampling = UART_OVERSAMPLING_16;

  if (HAL_HalfDuplex_Init(huart) != HAL_OK) {
    return HAL_ERROR;
  }

  /* Re-arm RX interrupt lost during HAL_HalfDuplex_Init reset */
  if (ctx != NULL) {
    ctx->recvPending = 0U;
    (void)HAL_UART_Receive_IT(huart, &ctx->rxByte, 1);
  }

  return HAL_OK;
}

static void owContextReset(OneWire_Config *cfg, OneWire_Context *ctx) {
  uint8_t i;

  memset(ctx, 0, sizeof(*ctx));
  for (i = 0; i < cfg->maxDevices; i++) {
    ctx->temp[i] = 0.0f;
  }
  ctx->lastDiscrepancy = 64;
}

HAL_StatusTypeDef owRegisterBus(OneWire_Config *cfg, OneWire_Context *ctx) {
  OneWire_BusBinding *slot;
  uint32_t i;

  if (cfg == NULL || ctx == NULL || cfg->huart == NULL || cfg->huart->Instance == NULL) {
    return HAL_ERROR;
  }

  if (cfg->maxDevices == 0U || cfg->maxDevices > ONEWIRE_MAX_DEVICES_LIMIT) {
    return HAL_ERROR;
  }

  slot = owFindBindingByInstance(cfg->huart->Instance);
  if (slot == NULL) {
    for (i = 0; i < ONEWIRE_MAX_BUS_COUNT; i++) {
      if (s_busBindings[i].cfg == NULL) {
        slot = &s_busBindings[i];
        break;
      }
    }
  }

  if (slot == NULL) {
    return HAL_ERROR;
  }

  slot->cfg = cfg;
  slot->ctx = ctx;
  owContextReset(cfg, ctx);

  /* owUsartSetup already arms HAL_UART_Receive_IT internally */
  return owUsartSetup(cfg, ctx, cfg->baudData);
}

HAL_StatusTypeDef owInit(OneWire_Config *cfg, OneWire_Context *ctx) {
  HAL_StatusTypeDef st;

  if (cfg == NULL || ctx == NULL) {
    return HAL_ERROR;
  }

  /* DS18B20 UART 1-Wire timing is fixed for this project. */
  cfg->baudData = 115200U;
  cfg->baudReset = 9600U;

  st = owRegisterBus(cfg, ctx);
  if (st != HAL_OK) {
    return st;
  }

  /* Best-effort: apply DS18B20 12-bit resolution for all discovered devices. */
  (void)owEnsureDs18b20Resolution12bit(cfg, ctx);

  return HAL_OK;
}

HAL_StatusTypeDef owInitSensorPositions(OneWire_Config *cfg, OneWire_Context *ctx, uint8_t forceRelearn) {
  uint8_t assigned[ONEWIRE_MAX_DEVICES_LIMIT] = {0};
  float baseline[ONEWIRE_MAX_DEVICES_LIMIT] = {0.0f};
  uint8_t warm;
  uint8_t pos;
  uint8_t i;

  if (cfg == NULL || ctx == NULL) {
    return HAL_ERROR;
  }

  if (get_ROMid(cfg, ctx) != 0 || ctx->devices < cfg->maxDevices) {
    /* Caller (Ds18b20Api_EnsureReady) already checks device count with its
     * own suppression guard before calling this function, so do NOT print
     * here — doing so would bypass the guard and spam the ITM console on
     * every retry while the bus is disconnected. */
    return HAL_ERROR;
  }

  if (forceRelearn == 0U) {
    if (owLoadPositionMap(cfg, ctx) == HAL_OK && ctx->positionCount >= cfg->maxDevices) {
      itm_print("[DS18B20] position map loaded from flash\r\n");
      return HAL_OK;
    }
    /* No valid position map and user has not requested learning — do NOT
     * auto-learn.  Return HAL_ERROR so the caller retries; the user must
     * navigate to Menu > Vi Tri DS18B20 > Hoc vi tri to start learning. */
    return HAL_ERROR;
  }

  itm_print("[DS18B20] start heating-based position learning\r\n");

  /* Discard first conversion results to avoid startup transient mis-mapping. */
  for (warm = 0U; warm < ONEWIRE_LEARN_WARMUP_SAMPLES; warm++) {
    get_Temperature(cfg, ctx);
    osDelay(ONEWIRE_LEARN_WARMUP_DELAY_MS);
  }

  for (pos = 0; pos < cfg->maxDevices; pos++) {
    uint32_t elapsed = 0U;
    uint8_t foundIdx = 0xFFU;
    uint8_t candidateIdx = 0xFFU;
    uint8_t candidateHits = 0U;

    get_Temperature(cfg, ctx);
    for (i = 0; i < ctx->devices; i++) {
      baseline[i] = ctx->temp[i];
    }

    itm_print("[DS18B20] please heat sensor at position ");
    itm_put_int((int)(pos + 1U));
    itm_print("\r\n");

    /* Thông báo cho LCD: đang yêu cầu người dùng hơ nóng vị trí này */
    if (cfg->learnCb != NULL)
    {
      cfg->learnCb((uint8_t)(pos + 1U), 0U);
    }

    while (elapsed < ONEWIRE_HEAT_TIMEOUT_MS) {
      float bestDelta = -1000.0f;
      float secondDelta = -1000.0f;
      uint8_t bestIdx = 0xFFU;

      get_Temperature(cfg, ctx);
      for (i = 0; i < ctx->devices; i++) {
        float delta;
        if (assigned[i] != 0U) {
          continue;
        }
        delta = ctx->temp[i] - baseline[i];
        if (delta > bestDelta) {
          secondDelta = bestDelta;
          bestDelta = delta;
          bestIdx = i;
        } else if (delta > secondDelta) {
          secondDelta = delta;
        }
      }

      if (bestIdx != 0xFFU &&
          bestDelta >= ONEWIRE_HEAT_DELTA_C &&
          (bestDelta - secondDelta) >= ONEWIRE_HEAT_CONFIRM_DELTA_C) {
        if (bestIdx == candidateIdx) {
          if (candidateHits < 0xFFU) {
            candidateHits++;
          }
        } else {
          candidateIdx = bestIdx;
          candidateHits = 1U;
        }

        if (elapsed >= ONEWIRE_HEAT_MIN_ELAPSED_MS &&
            candidateHits >= ONEWIRE_HEAT_REQUIRED_HITS) {
          foundIdx = bestIdx;
          break;
        }
      } else {
        candidateIdx = 0xFFU;
        candidateHits = 0U;
      }

      osDelay(ONEWIRE_HEAT_POLL_MS);
      elapsed += ONEWIRE_HEAT_POLL_MS;
    }

    if (foundIdx == 0xFFU) {
      itm_print("[DS18B20][ERR] learning timeout at position ");
      itm_put_int((int)(pos + 1U));
      itm_print("\r\n");
      ctx->positionValid = 0U;
      ctx->positionCount = 0U;
      return HAL_TIMEOUT;
    }

    assigned[foundIdx] = 1U;
    ctx->positionRom[pos] = ctx->romIds[foundIdx];
    itm_print("[DS18B20] position ");
    itm_put_int((int)(pos + 1U));
    itm_print(" mapped to ROM ");
    owItmPrintRom(&ctx->positionRom[pos]);
    itm_print("\r\n");

    /* Thông báo cho LCD: đã tìm thấy vị trí này */
    if (cfg->learnCb != NULL)
    {
      cfg->learnCb((uint8_t)(pos + 1U), 1U);
    }

    osDelay(1000U);
  }

  ctx->positionCount = cfg->maxDevices;
  ctx->positionValid = 1U;
  if (owSavePositionMap(cfg, ctx) != HAL_OK) {
    itm_print("[DS18B20][WARN] failed to save position map to flash\r\n");
    return HAL_ERROR;
  }

  itm_print("[DS18B20] position map saved to flash\r\n");
  return HAL_OK;
}

OneWire_Context *owGetContextByUart(UART_HandleTypeDef *huart) {
  OneWire_BusBinding *binding = owFindBindingByHandle(huart);
  if (binding == NULL) {
    return NULL;
  }
  return binding->ctx;
}

void owReadHandler(UART_HandleTypeDef *huart) {
  OneWire_BusBinding *binding;
  OneWire_Context *ctx;

  if (huart == NULL || huart->Instance == NULL) {
    return;
  }

  binding = owFindBindingByInstance(huart->Instance);
  if (binding == NULL || binding->ctx == NULL || binding->cfg == NULL) {
    return;
  }

  ctx = binding->ctx;

  /* Prefer byte captured by HAL receive IT; fallback to DR for robustness. */
  ctx->rxWord = (uint16_t)ctx->rxByte;
  if ((huart->Instance->SR & UART_FLAG_RXNE) != (uint16_t)RESET) {
    ctx->rxWord = USART_ReceiveData(huart->Instance);
  }
  ctx->recvPending = 0U;

  (void)HAL_UART_Receive_IT(binding->cfg->huart, &ctx->rxByte, 1);
}

/**
 * owErrorHandler – phải được gọi từ HAL_UART_ErrorCallback trong main.c.
 *
 * Vấn đề: Khi cắm/rút nóng, nhiễu điện từ trên bus gây ra ORE (Overrun
 * Error), FE (Framing Error) hoặc NE (Noise Error) trên ngoại vi USART.
 * Trong trường hợp đó HAL gọi HAL_UART_ErrorCallback thay vì
 * HAL_UART_RxCpltCallback, ngắt RX-IT bị tắt và KHÔNG được tái kích hoạt.
 * Hệ quả: ctx->recvPending mãi bằng 1, owEchoRead block hết timeout cho
 * mỗi bit, toàn bộ task bị đóng băng.
 *
 * Xử lý:
 *  1. Đọc SR rồi DR theo thứ tự để xóa cờ lỗi ORE/FE/NE trên STM32F1
 *     (cơ chế "read SR then read DR" – RM0008 §27.6.4).
 *  2. Reset recvPending = 0 để owEchoRead thoát ngay thay vì chờ hết timeout.
 *  3. Tái kích hoạt HAL_UART_Receive_IT để luồng nhận bình thường trở lại.
 */
void owErrorHandler(UART_HandleTypeDef *huart) {
  OneWire_BusBinding *binding;
  OneWire_Context *ctx;
  volatile uint32_t tmpSR;
  volatile uint32_t tmpDR;

  if (huart == NULL || huart->Instance == NULL) {
    return;
  }

  binding = owFindBindingByInstance(huart->Instance);
  if (binding == NULL || binding->ctx == NULL || binding->cfg == NULL) {
    return;
  }

  ctx = binding->ctx;

  /* --- Bước 1: Xóa cờ lỗi phần cứng bằng chuỗi đọc SR → DR --- */
  tmpSR = huart->Instance->SR;
  tmpDR = huart->Instance->DR;
  (void)tmpSR;
  (void)tmpDR;

  /* Xóa trường ErrorCode của HAL để lần gọi HAL tiếp theo không bị nhầm. */
  huart->ErrorCode = HAL_UART_ERROR_NONE;

  /* --- Bước 2: Giải phóng owEchoRead đang block --- */
  ctx->recvPending = 0U;

  /* --- Bước 3: Tái kích hoạt RX interrupt --- */
  (void)HAL_UART_Receive_IT(binding->cfg->huart, &ctx->rxByte, 1U);
}

uint16_t owResetCmd(OneWire_Config *cfg, OneWire_Context *ctx) {
  uint16_t owPresence;

  if (cfg == NULL || ctx == NULL) {
    return ONEWIRE_NOBODY;
  }

  if (owUsartSetup(cfg, ctx, cfg->baudReset) != HAL_OK) {
    return ONEWIRE_NOBODY;
  }

  owSend(cfg, ctx, 0xF0);
  owPresence = owEchoRead(cfg, ctx);

  (void)owUsartSetup(cfg, ctx, cfg->baudData);
  return owPresence;
}

void owSend(OneWire_Config *cfg, OneWire_Context *ctx, uint16_t data) {
  uint32_t pause = 10000U;

  if (cfg == NULL || ctx == NULL || cfg->huart == NULL || cfg->huart->Instance == NULL) {
    return;
  }

  ctx->recvPending = 1U;
  USART_SendData(cfg->huart->Instance, data);

  while ((__HAL_UART_GET_FLAG(cfg->huart, UART_FLAG_TC) == RESET) && (pause-- > 0U)) {
  }
}

uint8_t owReadSlot(uint16_t data) {
  return (data == OW_READ) ? 1U : 0U;
}

uint16_t owEchoRead(OneWire_Config *cfg, OneWire_Context *ctx) {
  /*
   * Tại sao timeout nhỏ lại đúng hơn:
   *  - Ở 115200 baud (data slot), 1 byte UART = 10 bit → ~86 µs.
   *  - Ở 9600 baud (reset pulse), 1 byte UART = 10 bit → ~1.04 ms.
   *  - owEchoRead được gọi sau owSend(), tức là byte đã được truyền đi,
   *    ngắt RxCplt phải đến trong < 1 chu kỳ byte.
   *  - Mỗi vòng lặp gọi osDelay(1) = 1 ms, nên pause = 10 cho phép chờ
   *    tối đa 10 ms — gấp hơn 9× so với trường hợp xấu nhất ở 9600 baud.
   *  - Giá trị cũ pause = 1000 → 1000 ms / lần gọi. Với 64 bit ROM Search
   *    (3 lần gọi owEchoRead mỗi bit), bus chết sẽ block tổng cộng
   *    64 × 3 × 1000 ms = 192 giây — khiến hệ thống trông như bị treo cứng.
   *  - Với pause = 10: thời gian tệ nhất = 64 × 3 × 10 ms = 1,92 giây,
   *    hoàn toàn phục hồi được trong vòng một chu kỳ polling.
   */
  uint32_t pause = 10U;

  (void)cfg;
  if (ctx == NULL) {
    return ONEWIRE_NOBODY;
  }

  while ((ctx->recvPending != 0U) && (pause-- > 0U)) {
    if (osKernelGetState() == osKernelRunning) {
      osDelay(1U);
    }
  }

  return ctx->rxWord;
}

static uint8_t *byteToBits(uint8_t ow_byte, uint8_t *bits) {
  uint8_t i;
  for (i = 0; i < 8U; i++) {
    *bits = (ow_byte & 0x01U) ? WIRE_1 : WIRE_0;
    bits++;
    ow_byte = ow_byte >> 1;
  }
  return bits;
}

void owSendByte(OneWire_Config *cfg, OneWire_Context *ctx, uint8_t d) {
  uint8_t data[8];
  uint8_t i;
  byteToBits(d, data);
  for (i = 0; i < 8U; ++i) {
    owSend(cfg, ctx, data[i]);
  }
}

static uint8_t owCRC(uint8_t *mas, uint8_t Len) {
  uint8_t i, dat, crc, fb, st_byt;
  st_byt = 0;
  crc = 0;
  do {
    dat = mas[st_byt];
    for (i = 0; i < 8U; i++) {
      fb = crc ^ dat;
      fb &= 1U;
      crc >>= 1;
      dat >>= 1;
      if (fb == 1U) {
        crc ^= 0x8CU;
      }
    }
    st_byt++;
  } while (st_byt < Len);
  return crc;
}

uint8_t owCRC8(RomCode *rom) {
  return owCRC((uint8_t *)rom, 7U);
}

static int hasNextRom(OneWire_Config *cfg, OneWire_Context *ctx, uint8_t *ROM) {
  uint8_t bitNumber = 0;
  int zeroFork = -1;
  uint8_t i = 0;

  {
    uint16_t pres = owResetCmd(cfg, ctx);
    /* ONEWIRE_NOBODY (0xF0) = bus floating HIGH, no sensor.
     * 0x00 = bus stuck LOW (sensor without VCC but GND still connected,
     *        or DATA shorted to GND).  Both mean no valid device present. */
    if (pres == ONEWIRE_NOBODY || pres == 0x00U) {
      return 0;
    }
  }

  owSendByte(cfg, ctx, ONEWIRE_SEARCH);

  do {
    uint8_t answerBit = 0;
    int byteNum = bitNumber / 8;
    uint8_t *current = ROM + byteNum;
    uint8_t cB, cmp_cB, searchDirection = 0;

    owSend(cfg, ctx, OW_READ);
    cB = owReadSlot(owEchoRead(cfg, ctx));

    owSend(cfg, ctx, OW_READ);
    cmp_cB = owReadSlot(owEchoRead(cfg, ctx));

    if (cB == cmp_cB && cB == 1U) {
      return -1;
    }

    if (cB != cmp_cB) {
      searchDirection = cB;
    } else {
      if (bitNumber == ctx->lastDiscrepancy) {
        searchDirection = 1U;
      } else {
        if (bitNumber > ctx->lastDiscrepancy) {
          searchDirection = 0U;
        } else {
          searchDirection = (uint8_t)((ctx->lastROM[byteNum] >> (bitNumber % 8U)) & 0x01U);
        }
        if (searchDirection == 0U) {
          zeroFork = bitNumber;
        }
      }
    }

    if (searchDirection != 0U) {
      *current |= (uint8_t)(1U << (bitNumber % 8U));
    } else {
      *current &= (uint8_t)~(1U << (bitNumber % 8U));
    }

    answerBit = (searchDirection == 0U) ? WIRE_0 : WIRE_1;
    owSend(cfg, ctx, answerBit);
    bitNumber++;
  } while (bitNumber < 64U);

  ctx->lastDiscrepancy = zeroFork;
  for (; i < 7U; i++) {
    ctx->lastROM[i] = ROM[i];
  }

  return (ctx->lastDiscrepancy > 0) ? 1 : 0;
}

int owSearchCmd(OneWire_Config *cfg, OneWire_Context *ctx) {
  int device = 0;
  int nextROM;

  if (cfg == NULL || ctx == NULL) {
    return -1;
  }

  owContextReset(cfg, ctx);

  do {
    nextROM = hasNextRom(cfg, ctx, (uint8_t *)(&ctx->romIds[device]));
    if (nextROM < 0) {
      return -1;
    }
    device++;
  } while (nextROM != 0 && device < (int)cfg->maxDevices);

  return device;
}

void owSkipRomCmd(OneWire_Config *cfg, OneWire_Context *ctx) {
  (void)owResetCmd(cfg, ctx);
  owSendByte(cfg, ctx, ONEWIRE_SKIP_ROM);
}

HAL_StatusTypeDef owMatchRomCmd(OneWire_Config *cfg, OneWire_Context *ctx, RomCode *rom) {
  uint8_t i;
  uint16_t presence;

  if (rom == NULL) {
    return HAL_ERROR;
  }

  presence = owResetCmd(cfg, ctx);
  if (presence == ONEWIRE_NOBODY || presence == 0x00U) {
    return HAL_ERROR;   /* bus lost — caller should treat as fault */
  }

  owSendByte(cfg, ctx, ONEWIRE_MATCH_ROM);
  for (i = 0; i < 8U; i++) {
    owSendByte(cfg, ctx, *(((uint8_t *)rom) + i));
  }
  return HAL_OK;
}

void owConvertTemperatureCmd(OneWire_Config *cfg, OneWire_Context *ctx, RomCode *rom) {
  (void)owMatchRomCmd(cfg, ctx, rom);
  owSendByte(cfg, ctx, ONEWIRE_CONVERT_TEMPERATURE);
}

uint8_t *owReadScratchpadCmd(OneWire_Config *cfg, OneWire_Context *ctx, RomCode *rom, uint8_t *data) {
  uint16_t b = 0;
  uint16_t p;

  if (rom == NULL || data == NULL) {
    return data;
  }

  switch (rom->family) {
    case DS18B20:
    case DS18S20:
      p = 72;
      break;
    default:
      return data;
  }

  memset(data, 0, 9U);
  owMatchRomCmd(cfg, ctx, rom);
  owSendByte(cfg, ctx, ONEWIRE_READ_SCRATCHPAD);

  while (b < p) {
    uint8_t pos = (uint8_t)(((p - 8U) / 8U) - (b / 8U));
    uint8_t bt;

    owSend(cfg, ctx, OW_READ);
    bt = owReadSlot(owEchoRead(cfg, ctx));
    if (bt == 1U) {
      data[pos] |= (uint8_t)(1U << (b % 8U));
    } else {
      data[pos] &= (uint8_t)~(1U << (b % 8U));
    }
    b++;
  }

  return data;
}

void owCopyScratchpadCmd(OneWire_Config *cfg, OneWire_Context *ctx, RomCode *rom) {
  (void)owMatchRomCmd(cfg, ctx, rom);
  owSendByte(cfg, ctx, ONEWIRE_COPY_SCRATCHPAD);
}

void owRecallE2Cmd(OneWire_Config *cfg, OneWire_Context *ctx, RomCode *rom) {
  (void)owMatchRomCmd(cfg, ctx, rom);
  owSendByte(cfg, ctx, ONEWIRE_RECALL_E2);
}

Temperature readTemperature(OneWire_Config *cfg, OneWire_Context *ctx, RomCode *rom, uint8_t reSense) {
  Scratchpad_DS18B20 *sp;
  Scratchpad_DS18S20 *spP;
  Temperature t;
  uint8_t pad[9];

  t.inCelsus = 0;
  t.frac = 0;

  if (rom == NULL) {
    return t;
  }

  sp = (Scratchpad_DS18B20 *)&pad;
  spP = (Scratchpad_DS18S20 *)&pad;

  switch (rom->family) {
    case DS18B20:
      owReadScratchpadCmd(cfg, ctx, rom, pad);
      t.inCelsus = (int8_t)((sp->temp_msb << 4) | (sp->temp_lsb >> 4));
      t.frac = (uint8_t)((((sp->temp_lsb & 0x0FU)) * 10U) >> 4);
      break;
    case DS18S20:
      owReadScratchpadCmd(cfg, ctx, rom, pad);
      t.inCelsus = (int8_t)(spP->temp_lsb >> 1);
      t.frac = (uint8_t)(5U * (spP->temp_lsb & 0x01U));
      break;
    default:
      return t;
  }

  if (reSense != 0U) {
    owConvertTemperatureCmd(cfg, ctx, rom);
  }

  return t;
}

int get_ROMid(OneWire_Config *cfg, OneWire_Context *ctx) {
 // int i;
  uint16_t presence;

  if (cfg == NULL || ctx == NULL) {
    return -1;
  }

  presence = owResetCmd(cfg, ctx);
  /* Treat both "no presence pulse" (0xF0) and "bus stuck LOW" (0x00) as
   * no device.  Without the 0x00 check, a stuck-LOW bus makes owSearchCmd
   * discover fake all-zero ROMs whose CRC also evaluates to 0, causing
   * get_ROMid to return success and triggering a spurious 4-minute
   * heating-based re-learn cycle. */
  if (presence == ONEWIRE_NOBODY || presence == 0x00U) {
    ctx->devices = 0;
    return -1;
  }

  /* owSearchCmd returns -1 on bus error (e.g. two-sensor collision or
   * noise).  Casting -1 directly to uint8_t gives 255, which then passes
   * the ctx->devices == 0 guard and causes the CRC loop below to read
   * ctx->romIds[0..254] — far out of the 8-entry array — occasionally
   * hitting a spurious CRC match and returning success with 255 "devices".
   * Fix: check for negative return value first and treat it as no device. */
  {
    int searchResult = owSearchCmd(cfg, ctx);
    if (searchResult <= 0) {
      ctx->devices = 0U;
      return -1;
    }
    if (searchResult > (int)ONEWIRE_MAX_DEVICES_LIMIT) {
      searchResult = (int)ONEWIRE_MAX_DEVICES_LIMIT;
    }
    ctx->devices = (uint8_t)searchResult;
  }

  /* ── Strict ROM validation ──────────────────────────────────────────────
   *
   * A phantom device can appear when the bus is electrically unstable during
   * hot-plug/unplug: noise produces a false presence pulse, owSearchCmd
   * "discovers" a garbled ROM, and by chance the CRC-8 of 7 random bytes
   * happens to match byte 8 (probability 1/256 per device slot).
   *
   * Three-layer defence:
   *  1. CRC-8 must match (existing check).
   *  2. Family byte must be a known DS18x20 code (0x28 = DS18B20,
   *     0x10 = DS18S20).  Noise-generated ROMs almost never land on a
   *     valid family code AND produce a matching CRC simultaneously.
   *  3. All 6 serial "code" bytes must NOT all be 0x00 (all-zero ROM is a
   *     classic stuck-LOW artefact that also satisfies CRC == 0x00).
   *
   * Only ROMs that pass all three checks are counted as real devices.
   * ctx->devices is updated to the VALID count; if none pass, return -1.
   */
  {
    int validCount = 0;
    int src;
    int dst = 0;

    for (src = 0; src < (int)ctx->devices; src++) {
      RomCode *r = &ctx->romIds[src];
      uint8_t crc = owCRC8(r);
      uint8_t allZeroCode = 1U;
      uint8_t j;

      /* Check 1: CRC-8 */
      if (crc != r->crc) {
        continue;
      }

      /* Check 2: known DS18x20 family code */
      if (r->family != DS18B20 && r->family != DS18S20) {
        continue;
      }

      /* Check 3: serial bytes not all zero */
      for (j = 0U; j < 6U; j++) {
        if (r->code[j] != 0x00U) {
          allZeroCode = 0U;
          break;
        }
      }
      if (allZeroCode != 0U) {
        continue;
      }

      /* Valid — compact to front of array so callers see a contiguous list */
      if (dst != src) {
        ctx->romIds[dst] = ctx->romIds[src];
      }
      dst++;
      validCount++;
    }

    if (validCount == 0) {
      ctx->devices = 0U;
      return -1;
    }

    ctx->devices = (uint8_t)validCount;
  }

  return 0;
}

void get_Temperature(OneWire_Config *cfg, OneWire_Context *ctx) {
  uint8_t i;
  Temperature t;

  if (cfg == NULL || ctx == NULL) {
    return;
  }

  /* Simultaneous CONVERT T for all sensors, then read each scratchpad. */
  owSkipRomCmd(cfg, ctx);
  owSendByte(cfg, ctx, ONEWIRE_CONVERT_TEMPERATURE);
  osDelay(750U);

  for (i = 0; i < ctx->devices && i < cfg->maxDevices; i++) {
    switch (ctx->romIds[i].family) {
      case DS18B20:
      case DS18S20:
        t = readTemperature(cfg, ctx, &ctx->romIds[i], 0U);
        ctx->temp[i] = (float)(t.inCelsus * 10 + t.frac) / 10.0f;
        break;
      default:
        break;
    }
  }
}

HAL_StatusTypeDef owGetTemperatureByPosition(OneWire_Config *cfg, OneWire_Context *ctx) {
  uint8_t i;

  if (cfg == NULL || ctx == NULL || ctx->positionValid == 0U || ctx->positionCount == 0U) {
    return HAL_ERROR;
  }

  /* Verify all position ROMs are still present in cached ROM list (RAM check). */
  for (i = 0; i < ctx->positionCount && i < cfg->maxDevices; i++) {
    if (owFindRomIndex(ctx, &ctx->positionRom[i]) < 0) {
      return HAL_ERROR;
    }
  }

  /*
   * Check bus presence with a real Reset Pulse BEFORE issuing any conversion.
   *
   * Three possible echo values after sending 0xF0 at 9600 baud:
   *
   *  0xF0  (ONEWIRE_NOBODY) – bus stayed HIGH the whole time.
   *         No sensor pulled the line low.
   *         Physical cause: GND wire disconnected — sensor has VCC but no
   *         ground return, so its open-drain output cannot sink current and
   *         the bus is held HIGH by the pull-up resistor the whole time.
   *         → set temp = -0.1 °C (tempDeciC = -1, "GND/DATA suspected").
   *
   *  0x00  – bus was LOW the entire time and never went HIGH.
   *         Physical cause: VCC wire disconnected — sensor has GND but no
   *         supply; it pulls DATA to GND through internal ESD diodes, keeping
   *         the bus stuck LOW even during the presence-pulse window.
   *         In this state the scratchpad reads return all-0x00 → 0.0 °C
   *         which is NOT caught by the existing fault markers, so we must
   *         intercept it here.
   *         → set temp = -0.2 °C (tempDeciC = -2, "VCC suspected").
   *
   *  other – valid presence pulse detected; at least one sensor responded.
   *         Proceed with conversion and scratchpad read.
   */
  {
    uint16_t presence = owResetCmd(cfg, ctx);
    if (presence == ONEWIRE_NOBODY || presence == 0x00U) {
      float faultTemp = (presence == ONEWIRE_NOBODY) ? -0.1f : -0.2f;
      for (i = 0; i < cfg->maxDevices; i++) {
        ctx->temp[i] = faultTemp;
      }
      /* Return HAL_OK so the caller queues the fault values and the fault
       * callback fires.  Recovery (sensorsReady=0) is set by the caller
       * when it detects the fault marker in ctx->temp[]. */
      return HAL_OK;
    }
    /* Presence confirmed — owSkipRomCmd will issue its own reset; that is fine. */
  }

  /*
   * Presence confirmed. Issue a simultaneous CONVERT T to ALL sensors at once
   * (Skip ROM + 0x44) then wait the full 12-bit conversion time (750 ms).
   * owSkipRomCmd internally does another reset — that is fine (sensors are
   * still present and will respond with a presence pulse again).
   * This approach avoids the problem where a per-sensor Reset Pulse aborts
   * another sensor's in-progress conversion (DS18B20 datasheet §Table 2).
   */
  owSkipRomCmd(cfg, ctx);
  owSendByte(cfg, ctx, ONEWIRE_CONVERT_TEMPERATURE);
  osDelay(750U);

  /*
   * Re-check bus presence AFTER conversion wait.
   * The wire may have been disconnected during the 750 ms delay.
   * Without this check, all subsequent scratchpad reads would return
   * all-0xFF or all-0x00 — caught later by CRC — but only AFTER queuing
   * wrong data.  An early exit here sets the correct fault marker immediately
   * for ALL sensors and avoids partial-fault scenarios.
   */
  {
    uint16_t presenceAfter = owResetCmd(cfg, ctx);
    if (presenceAfter == ONEWIRE_NOBODY || presenceAfter == 0x00U) {
      float faultTemp = (presenceAfter == ONEWIRE_NOBODY) ? -0.1f : -0.2f;
      for (i = 0; i < cfg->maxDevices; i++) {
        ctx->temp[i] = faultTemp;
      }
      return HAL_OK;
    }
  }

  /* Read each sensor's scratchpad with CRC validation.
   * owReadScratchpadCmd stores bytes reversed in pad[]:
   *   pad[8] = DS18B20 byte 0 (temp LSB) ... pad[0] = byte 8 (CRC).
   * Dallas CRC-8 must be computed over bytes 0..7 IN ORDER (pad[8]..pad[1]).
   *
   * Special cases that bypass normal CRC:
   *  - All 9 bytes == 0x00: CRC(8 zeros)=0x00=pad[0] → false pass, bus stuck LOW.
   *  - All 9 bytes == 0xFF: bus floating HIGH (pull-up), no pull-down from sensor.
   * Both → set -0.3f (CRC-fail marker).
   *
   * owMatchRomCmd now returns HAL_ERROR if its Reset detects no presence.
   * When that happens we set fault for ALL remaining sensors (not just the
   * current one) because a bus disconnect affects all sensors simultaneously.
   */
  for (i = 0; i < ctx->positionCount && i < cfg->maxDevices; i++) {
    uint8_t pad[9];
    uint8_t ordered[8];
    uint8_t j;
    uint8_t allZero = 1U;
    uint8_t allFF   = 1U;
    Scratchpad_DS18B20 *sp = (Scratchpad_DS18B20 *)pad;

    memset(pad, 0, sizeof(pad));

    /* Check Match ROM Reset — if bus lost, fault ALL remaining sensors */
    if (owMatchRomCmd(cfg, ctx, &ctx->positionRom[i]) != HAL_OK) {
      uint8_t k;
      for (k = i; k < cfg->maxDevices; k++) {
        ctx->temp[k] = -0.1f;   /* bus stuck LOW / lost */
      }
      return HAL_OK;
    }

    /* Send READ_SCRATCHPAD and read 72 bits (owReadScratchpadCmd would do a
     * second Match ROM reset internally, so we call the lower-level pieces
     * directly after we already issued Match ROM above). */
    {
      uint16_t b = 0;
      const uint16_t p = 72U;
      owSendByte(cfg, ctx, ONEWIRE_READ_SCRATCHPAD);
      while (b < p) {
        uint8_t pos = (uint8_t)(((p - 8U) / 8U) - (b / 8U));
        uint8_t bt;
        owSend(cfg, ctx, OW_READ);
        bt = owReadSlot(owEchoRead(cfg, ctx));
        if (bt == 1U) {
          pad[pos] |= (uint8_t)(1U << (b % 8U));
        } else {
          pad[pos] &= (uint8_t)~(1U << (b % 8U));
        }
        b++;
      }
    }

    /* Quick sanity: detect degenerate all-0x00 / all-0xFF payloads */
    for (j = 0U; j < 9U; j++) {
      if (pad[j] != 0x00U) { allZero = 0U; }
      if (pad[j] != 0xFFU) { allFF   = 0U; }
    }

    if (allZero != 0U || allFF != 0U) {
      ctx->temp[i] = -0.3f;
    } else {
      /* Re-order for Dallas CRC: pad[8]->ordered[0] .. pad[1]->ordered[7] */
      for (j = 0U; j < 8U; j++) {
        ordered[j] = pad[8U - j];
      }

      if (owCRC(ordered, 8U) != pad[0]) {
        ctx->temp[i] = -0.3f;
      } else {
        int8_t degC = (int8_t)((sp->temp_msb << 4) | (sp->temp_lsb >> 4));
        uint8_t frac = (uint8_t)(((sp->temp_lsb & 0x0FU) * 10U) >> 4);
        ctx->temp[i] = (float)(degC * 10 + (int8_t)frac) / 10.0f;
      }
    }
  }

  return HAL_OK;
}
