# Hướng dẫn tích hợp `ds18b20_uart` vào project STM32CubeIDE khác

Tài liệu này hướng dẫn từng bước copy thư viện `ds18b20_uart` vào một project STM32CubeIDE mới,
đồng thời giải thích chi tiết các cơ chế quan trọng:
- **`Ds18b20Api_Service()`** — hàm service tổng hợp.
- **Cơ chế học vị trí cảm biến (sensor position learning)** — `owInitSensorPositions()`.
- **Xử lý lỗi phần cứng UART** — `owErrorHandler()`.
- **Lọc phantom device** — 3 lớp kiểm tra ROM trong `get_ROMid()`.

---

## Mục lục

1. [Yêu cầu](#1-yêu-cầu)
2. [Copy file vào project](#2-copy-file-vào-project)
3. [Cấu hình STM32CubeMX / CubeIDE](#3-cấu-hình-stm32cubemx--cubeide)
4. [Thêm include path trong CubeIDE](#4-thêm-include-path-trong-cubeide)
5. [Khai báo và khởi tạo trong code](#5-khai-báo-và-khởi-tạo-trong-code)
6. [Callback UART bắt buộc](#6-callback-uart-bắt-buộc)
7. [Tạo queue và task FreeRTOS](#7-tạo-queue-và-task-freertos)
8. [Giải thích chi tiết `Ds18b20Api_Service()`](#8-giải-thích-chi-tiết-ds18b20api_service)
9. [Giải thích cơ chế học vị trí cảm biến](#9-giải-thích-cơ-chế-học-vị-trí-cảm-biến)
10. [Áp dụng học lại vị trí cảm biến (Relearn)](#10-áp-dụng-học-lại-vị-trí-cảm-biến-relearn)
11. [Xử lý lỗi phần cứng UART (ORE/FE/NE)](#11-xử-lý-lỗi-phần-cứng-uart-orefene)
12. [Cơ chế lọc phantom device](#12-cơ-chế-lọc-phantom-device)
13. [Sơ đồ tóm tắt](#13-sơ-đồ-tóm-tắt)
14. [Các lỗi thường gặp khi tích hợp](#14-các-lỗi-thường-gặp-khi-tích-hợp)

---

## 1) Yêu cầu

| Yêu cầu            | Chi tiết                                                        |
|--------------------|-----------------------------------------------------------------|
| MCU                | STM32F1xx (có thể dùng F4xx với sửa nhỏ về HAL include)        |
| Middleware         | FreeRTOS (CMSIS-RTOS v2)                                        |
| UART               | 1 cổng USART cấu hình **Half-Duplex**                           |
| Phần cứng          | Điện trở pull-up **4.7 kΩ** từ chân Data DS18B20 lên VCC       |
| Debug (tuỳ chọn)   | ITM/SWV để xem log — hoặc thay `itm_print()` bằng `printf()`   |

---

## 2) Copy file vào project

Copy **toàn bộ thư mục** `ds18b20_uart/` vào project của bạn:

```
YourProject/
  ds18b20_uart/
    OneWireUart.h
    OneWireUart.c
    ds18b20_app.h
    ds18b20_app.c
```

> Nếu bạn không dùng ITM, hãy xem mục [14 — Lỗi thường gặp](#14-các-lỗi-thường-gặp-khi-tích-hợp) để thay thế `itm_print`.

---

## 3) Cấu hình STM32CubeMX / CubeIDE

### 3.1 UART — Half-Duplex

Trong CubeMX, chọn `USARTx` → **Mode: Single Wire (Half-Duplex)**:

| Tham số                | Giá trị                                  |
|------------------------|------------------------------------------|
| Mode                   | Half-Duplex                              |
| Baud Rate              | tuỳ (thư viện tự đổi về 9600 / 115200)   |
| Word Length            | 8 bits                                   |
| Stop Bits              | 1                                        |
| Parity                 | None                                     |
| UART Global Interrupt  | **Enabled** ✅                            |

> **Quan trọng:** Phải bật **UART Global Interrupt** để `HAL_UART_RxCpltCallback()` và
> `HAL_UART_ErrorCallback()` được gọi.

### 3.2 FreeRTOS

Bật FreeRTOS → Interface: **CMSIS_V2**.

Tạo:
- **1 Task** — ví dụ `TaskDs18b20`, stack ≥ 512 words.
- **1 Queue** — ví dụ `QueueDs18b20`, 10 phần tử, kích thước = `sizeof(Ds18b20QueueItem)`.

### 3.3 Flash — kiểm tra địa chỉ

Thư viện lưu position map tại:
```c
#define ONEWIRE_POSMAP_FLASH_ADDR  0x0800FC00UL
```
Đây là page cuối của **64 KB Flash** (STM32F103C8). Nếu MCU của bạn có Flash lớn hơn hoặc khác
layout, hãy sửa địa chỉ này trong `OneWireUart.h` để tránh xung đột với code/data khác.

---

## 4) Thêm include path trong CubeIDE

1. Chuột phải project → **Properties**.
2. **C/C++ Build → Settings → MCU GCC Compiler → Include paths**.
3. Thêm: `${workspace_loc:/${ProjName}/ds18b20_uart}`.
4. Tương tự cho **MCU G++ Compiler** nếu có.
5. Vào **C/C++ General → Paths and Symbols → Source Location**, thêm thư mục `ds18b20_uart`
   để CubeIDE biên dịch các file `.c` trong đó.

---

## 5) Khai báo và khởi tạo trong code

### 5.1 Include header

Trong file task hoặc `main.c`:

```c
#include "OneWireUart.h"
#include "ds18b20_app.h"
```

### 5.2 Khai báo biến và khởi tạo trong task

`OneWire_Config` và `OneWire_Context` được khai báo là biến **local** trong task.
Vì `StartTaskds18b20` là FreeRTOS task **không bao giờ return** (vòng `for(;;)` vô hạn),
stack frame tồn tại suốt vòng đời hệ thống — không cần `static`.

> **Cỡ xấp xỉ:** `OneWire_Config` ≈ 13 byte, `OneWire_Context` ≈ 180 byte.
> Với stack task ≥ 512 words (2048 byte) là hoàn toàn vừa.

```c
void StartTaskds18b20(void *argument)
{
  /* USER CODE BEGIN 5 */
  OneWire_Config  owCfg1;
  OneWire_Context owCtx1;

  owCfg1.huart      = &huart1;   /* UART Half-Duplex đã cấu hình trong CubeMX */
  owCfg1.maxDevices = 4U;        /* Số cảm biến DS18B20 thực tế trên bus */

  Ds18b20Api_Init(&owCfg1, &owCtx1);

  for (;;)
  {
    if (Ds18b20Api_Service(&owCfg1, &owCtx1,
                           Queueds18b20Handle,
                           Ds18b20Api_DefaultOnWireFault) != HAL_OK)
    {
      osDelay(1000U);
      continue;
    }
    osDelay(1250U);
  }
  /* USER CODE END 5 */
}
```

> **Lưu ý `owCfg1.maxDevices`:** Đặt đúng số cảm biến thực tế cắm vào bus.
> Nếu set lớn hơn, `EnsureReady()` sẽ liên tục chờ và không bao giờ bắt đầu đọc nhiệt độ.

---

## 6) Callback UART bắt buộc

Trong `Core/Src/main.c`, thêm **cả hai** callback:

```c
/* USER CODE BEGIN 4 */

/* Bắt buộc: thư viện cần byte echo này để biết bus đã phản hồi */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  owReadHandler(huart);
}

/* Bắt buộc: xử lý lỗi phần cứng ORE/FE/NE khi rút/cắm nóng cảm biến */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  owErrorHandler(huart);
}

/* USER CODE END 4 */
```

> Nếu bạn đã có `HAL_UART_RxCpltCallback` phục vụ UART khác (ví dụ USART2 debug),
> cả `owReadHandler()` và `owErrorHandler()` tự kiểm tra handle bên trong —
> không ảnh hưởng đến UART khác.

---

## 7) Tạo queue và task FreeRTOS

### Đọc dữ liệu từ queue

Trong task khác (ví dụ task display / task log):

```c
void StartTaskITMconsole(void *argument)
{
  /* USER CODE BEGIN StartTaskITMconsole */
  Ds18b20QueueItem item;

  for (;;)
  {
    if (osMessageQueueGet(Queueds18b20Handle, &item, NULL, 1000U) == osOK) {
      Ds18b20Api_PrintItem(&item);
    }
  }
  /* USER CODE END StartTaskITMconsole */
}
```

### Callback lỗi dây (tuỳ chọn — có thể dùng hàm mặc định)

Thư viện cung cấp sẵn `Ds18b20Api_DefaultOnWireFault()` in log ra ITM Console.
Nếu muốn xử lý riêng, truyền callback tự định nghĩa vào `Ds18b20Api_Service()`:

```c
static void MyOnWireFault(uint8_t sensorIndex, int16_t tempDeciC)
{
  if (tempDeciC == -1) {
    /* Bus floating HIGH: dây GND bị hở */
  } else if (tempDeciC == -2) {
    /* Bus stuck LOW: dây VCC hoặc DATA bị hở/ngắn mạch GND */
  } else if (tempDeciC == -3) {
    /* CRC fail: dây DATA nhiễu hoặc hở */
  }
}
```

| `tempDeciC` | Loại lỗi                                  |
|-------------|-------------------------------------------|
| `-1`        | Bus floating HIGH — nghi đứt dây GND      |
| `-2`        | Bus stuck LOW — nghi đứt VCC hoặc DATA    |
| `-3`        | CRC fail — nhiễu hoặc đứt DATA            |

> **Chỉ log một lần:** `faultLoggedMask` trong `Ds18b20StateSlot` đảm bảo `onFault`
> chỉ được gọi **1 lần duy nhất** cho mỗi sensor trong một chu kỳ lỗi.
> Flag được xóa khi bus hoàn toàn phục hồi và `EnsureReady()` thành công.

---

## 8) Giải thích chi tiết `Ds18b20Api_Service()`

### Chữ ký

```c
HAL_StatusTypeDef Ds18b20Api_Service(
    OneWire_Config        *cfg,
    OneWire_Context       *ctx,
    osMessageQueueId_t     queueId,
    Ds18b20FaultCallback   onFault   /* NULL nếu không cần */
);
```

### Luồng thực thi bên trong

```
Ds18b20Api_Service()
│
├─ [Kiểm tra tham số NULL] ──────────────────────────► HAL_ERROR
│
├─ [Lấy slot trạng thái nội bộ (s_dsState[])]
│    Mỗi cặp (cfg, ctx) có 1 slot riêng, tối đa ONEWIRE_MAX_BUS_COUNT slot
│
├─ [slot->sensorsReady == 0 ?]
│    └── Ds18b20Api_EnsureReady()
│         ├── get_ROMid()              ← quét bus, lọc phantom (3 lớp)
│         ├── Kiểm tra ctx->devices >= cfg->maxDevices
│         └── owInitSensorPositions()  ← load Flash HOẶC học nhiệt
│         ──► FAIL: return HAL_ERROR   (task delay rồi thử lại)
│         ──► OK  : sensorsReady = 1, xóa các guard flag
│
├─ [Ds18b20Api_ReadAndQueue()]
│    ├── owGetTemperatureByPosition()
│    │    (Match ROM từng cảm biến theo thứ tự vị trí, đọc scratchpad)
│    ├── FAIL: positionValid=0, sensorsReady=0
│    │    └── onFault(-1) cho toàn bộ sensor ──► HAL_ERROR
│    └── Vòng lặp i = 0 .. maxDevices-1:
│         item = { sensorIndex=i+1, tempDeciC=temp[i]×10, tick }
│         Bỏ qua nếu tempDeciC ∈ {-1, -2, -3}  ← fault marker, không queue
│         osMessageQueuePut(queueId, &item)
│
└─ [onFault != NULL ?]
     Vòng lặp kiểm tra từng ctx->temp[i]:
       tempDeciC ∈ {-1,-2,-3} → onFault(i+1, tempDeciC) (chỉ lần đầu)
                               → sensorsReady=0, positionValid=0
```

### Cơ chế tự phục hồi

| Tình huống                           | Hành động của Service()                               |
|--------------------------------------|-------------------------------------------------------|
| Lần đầu chạy                         | `sensorsReady=0` → tự gọi `EnsureReady()`             |
| Thiếu cảm biến (chưa cắm đủ)         | `EnsureReady()` trả `HAL_ERROR`, task delay retry     |
| Cảm biến bị rút ra rồi cắm lại       | Lần đọc tiếp thất bại → `sensorsReady=0` → `EnsureReady()` tự quét ROM lại |
| Rút nóng toàn bộ bus (VCC + GND)     | `owErrorHandler` xóa ORE, `onFault` log 1 lần, hệ thống im lặng chờ |
| Cần học lại vị trí                   | Gọi `Ds18b20Api_RequestRelearn()` → `sensorsReady=0`, `forceRelearn=1` |

### Sơ đồ trạng thái `sensorsReady`

```
  [Init / RequestRelearn]
           │
           ▼
    sensorsReady = 0
           │
           │  Service() → EnsureReady()
           ├── FAIL ──────────────────────────────────► return HAL_ERROR
           │                                            (giữ sensorsReady = 0)
           │
           └── OK  ─────────────────────────────────► sensorsReady = 1
                                                       xóa noDeviceLogged,
                                                       faultLoggedMask,
                                                       ensureReadyLogged
                    │
                    │  Service() → ReadAndQueue()
                    ├── FAIL ──► sensorsReady = 0 ───► onFault mỗi sensor (1 lần)
                    │                                  return HAL_ERROR
                    └── OK ───► queue data, kiểm tra fault ► return HAL_OK
```

---

## 9) Giải thích cơ chế học vị trí cảm biến

### Vấn đề cần giải quyết

DS18B20 nhận dạng bằng **ROM code 64-bit duy nhất**. Khi quét bus (`get_ROMid()`), thứ tự trả về
**không cố định** — phụ thuộc vào thuật toán 1-Wire Search. Nghĩa là sau khi reset MCU, cảm biến
vật lý số 1 có thể nằm ở `romIds[2]`. Cơ chế học vị trí giải quyết vấn đề này.

### Hai chế độ hoạt động

#### Chế độ 1 — Load từ Flash (`forceRelearn = 0`)

```
owInitSensorPositions(cfg, ctx, forceRelearn=0)
│
├── get_ROMid()          ← quét bus, kiểm tra đủ maxDevices
├── owLoadPositionMap()  ← đọc Flash tại ONEWIRE_POSMAP_FLASH_ADDR
│    Kiểm tra: magic, version, checksum, tất cả ROM vẫn có mặt trên bus
├── positionCount >= maxDevices ?
│    └── YES: ctx->positionRom[] đã có map
│         ──► "position map loaded from flash" ► return HAL_OK (nhanh)
└── NO: chuyển sang chế độ học nhiệt
```

#### Chế độ 2 — Học bằng nhiệt (`forceRelearn = 1` hoặc Flash không hợp lệ)

```
Bước 1: Warm-up 2 lần đọc để ổn định giá trị ban đầu
         get_Temperature() × 2, mỗi lần delay 800 ms

Bước 2: Lặp pos = 0 .. maxDevices-1:

  2a. Đọc baseline[] của tất cả cảm biến chưa được gán vị trí

  2b. Log: "[DS18B20] please heat sensor at position X"
      → Người dùng hơ nhẹ cảm biến số X

  2c. Polling 500 ms/lần, tối đa 60 giây:
      - Đọc lại nhiệt độ tất cả cảm biến
      - delta[i] = temp[i] - baseline[i]
      - bestDelta, secondDelta = 2 delta cao nhất
      - Điều kiện xác nhận:
          bestDelta >= 1.0 °C               ← tăng đủ ngưỡng
          bestDelta - secondDelta >= 0.5 °C  ← rõ ràng hơn cảm biến kế
          candidateHits >= 1                ← ổn định ít nhất 1 vòng poll
          elapsed >= 1000 ms                ← đã chờ đủ lâu tránh nhiễu khởi động
      → Khi thoả: ctx->positionRom[pos] = ctx->romIds[foundIdx]

  2d. Timeout 60 s: return HAL_TIMEOUT

Bước 3: Lưu map vào Flash (owSavePositionMap)
         Cấu trúc OneWire_PositionMapFlash:
           { magic, version, count, rom[0..N-1], checksum }
```

### Ví dụ thực tế học 4 cảm biến

```
[DS18B20] start heating-based position learning
[DS18B20] please heat sensor at position 1
  ... (hơ cảm biến vị trí 1) ...
[DS18B20] position 1 mapped to ROM 28-FF-A1-B2-C3-D4-E5-F6

[DS18B20] please heat sensor at position 2
  ... (hơ cảm biến vị trí 2) ...
[DS18B20] position 2 mapped to ROM 28-AA-11-22-33-44-55-66

[DS18B20] please heat sensor at position 3
  ... (hơ cảm biến vị trí 3) ...
[DS18B20] position 3 mapped to ROM 28-CC-77-88-99-AA-BB-CC

[DS18B20] please heat sensor at position 4
  ... (hơ cảm biến vị trí 4) ...
[DS18B20] position 4 mapped to ROM 28-DD-12-34-56-78-9A-BC

[DS18B20] position map saved to flash
[DS18B20] all sensors ready, start temperature polling
```

### Ép học lại

```c
// Gọi bất kỳ lúc nào trong runtime (không block):
Ds18b20Api_RequestRelearn(&owCfg1, &owCtx1);
// → Lần Service() tiếp theo sẽ tự học lại toàn bộ
```

### Lưu ý quan trọng về học vị trí

- **Hơ từng cảm biến một** — thuật toán nhận cảm biến nóng nhất, phải rõ hơn kế ≥ 0.5 °C.
- **Tăng ít nhất 1 °C** so với nhiệt độ ban đầu để thuật toán xác nhận.
- Sau khi học xong, map lưu Flash → **tồn tại qua các lần reset**.
- Nếu thay cảm biến vật lý, phải ép học lại bằng `Ds18b20Api_RequestRelearn()`.

---

## 10) Áp dụng học lại vị trí cảm biến (Relearn)

### Khi nào cần học lại?

| Tình huống                                         | Cần học lại? |
|----------------------------------------------------|:------------:|
| Lần đầu nạp firmware (Flash chưa có map)           | ✅ Tự động    |
| Reset MCU bình thường (Flash còn map hợp lệ)       | ❌ Load Flash |
| Thay một cảm biến DS18B20 bằng cái khác            | ✅ Bắt buộc   |
| Thay đổi thứ tự cắm dây vật lý                     | ✅ Bắt buộc   |
| Nhiệt độ đọc về sai thứ tự vị trí                  | ✅ Bắt buộc   |
| Nạp lại firmware đè vào vùng Flash lưu map         | ✅ Bắt buộc   |
| Thêm hoặc bớt cảm biến trên bus                    | ✅ Bắt buộc — cập nhật `maxDevices` trước |

### API học lại

```c
HAL_StatusTypeDef Ds18b20Api_RequestRelearn(OneWire_Config *cfg, OneWire_Context *ctx);
```

Hàm này **không block** — chỉ đặt cờ nội bộ (`forceRelearn = 1`, `sensorsReady = 0`,
`positionValid = 0`, `positionCount = 0`). Việc học lại thực sự diễn ra ở lần
`Ds18b20Api_Service()` kế tiếp trong task.

### Cách 1 — Kích hoạt qua nút bấm (GPIO polling)

```c
void StartTaskds18b20(void *argument)
{
  OneWire_Config  owCfg1;
  OneWire_Context owCtx1;

  owCfg1.huart      = &huart1;
  owCfg1.maxDevices = 4U;
  Ds18b20Api_Init(&owCfg1, &owCtx1);

  for (;;)
  {
    /* Nút học lại active-low trên PA0 */
    if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET)
    {
      osDelay(50U);   /* debounce */
      if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET)
      {
        Ds18b20Api_RequestRelearn(&owCfg1, &owCtx1);
        while (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET) {
          osDelay(50U);
        }
      }
    }

    if (Ds18b20Api_Service(&owCfg1, &owCtx1,
                           Queueds18b20Handle,
                           Ds18b20Api_DefaultOnWireFault) != HAL_OK) {
      osDelay(1000U);
      continue;
    }
    osDelay(1250U);
  }
}
```

### Cách 2 — Kích hoạt qua UART command

**Khai báo cờ toàn cục:**
```c
/* USER CODE BEGIN PV */
volatile uint8_t g_requestRelearn = 0U;
uint8_t          g_cmdByte        = 0U;
/* USER CODE END PV */
```

**Khởi động nhận trong `main()`:**
```c
HAL_UART_Receive_IT(&huart2, &g_cmdByte, 1U);
```

**Callback:**
```c
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1) {
    owReadHandler(huart);               /* DS18B20 */
  }
  if (huart->Instance == USART2) {
    if (g_cmdByte == 'R') {
      g_requestRelearn = 1U;
    }
    HAL_UART_Receive_IT(&huart2, &g_cmdByte, 1U);
  }
}
```

**Trong task:**
```c
if (g_requestRelearn != 0U) {
  g_requestRelearn = 0U;
  Ds18b20Api_RequestRelearn(&owCfg1, &owCtx1);
}
```

### Cách 3 — EXTI (ngắt cạnh nút bấm)

```c
volatile uint8_t g_requestRelearn = 0U;

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == GPIO_PIN_0) {
    g_requestRelearn = 1U;   /* chỉ đặt cờ, không gọi API trong ISR */
  }
}
```

### Cách 4 — Học lại theo lịch tự động

```c
uint32_t lastRelearn = osKernelGetTickCount();
const uint32_t RELEARN_INTERVAL_MS = 24UL * 3600UL * 1000UL; /* 24 giờ */

for (;;)
{
  if ((osKernelGetTickCount() - lastRelearn) >= RELEARN_INTERVAL_MS) {
    Ds18b20Api_RequestRelearn(&owCfg1, &owCtx1);
    lastRelearn = osKernelGetTickCount();
  }
  /* ... Service() ... */
}
```

### Lưu ý khi thực hiện học lại

> ⚠️ **Trong quá trình học, hệ thống KHÔNG đọc nhiệt độ** — task block tại `EnsureReady()`.

- Mỗi cảm biến có tối đa **60 giây** để phát hiện; nếu timeout → `EnsureReady()` trả `HAL_ERROR` và task tự retry.
- Các cảm biến cần cách nhau về vật lý để chênh lệch nhiệt độ đủ lớn.

---

## 11) Xử lý lỗi phần cứng UART (ORE/FE/NE)

### Vấn đề

Khi rút/cắm nóng, nhiễu điện từ trên bus có thể gây **ORE** (Overrun Error), **FE** (Framing Error)
hoặc **NE** (Noise Error) trên ngoại vi USART. Khi đó HAL gọi `HAL_UART_ErrorCallback` thay vì
`HAL_UART_RxCpltCallback`, ngắt RX-IT bị tắt và **không** được tái kích hoạt tự động.

**Hệ quả nếu không xử lý:**
- `ctx->recvPending` mãi bằng `1`.
- `owEchoRead()` block hết timeout (10 ms × 64 bit × 3 lần/bit ≈ 1.9 giây) cho mỗi lần quét bus.
- Hệ thống trông như bị đóng băng hoàn toàn.

### Giải pháp — `owErrorHandler()`

Hàm `owErrorHandler()` (khai báo trong `OneWireUart.h`) thực hiện 3 bước theo RM0008 §27.6.4:

```
Bước 1: Xóa cờ lỗi phần cứng
         Đọc SR → DR theo thứ tự (cơ chế duy nhất xóa ORE/FE/NE trên STM32F1)
         huart->ErrorCode = HAL_UART_ERROR_NONE

Bước 2: Giải phóng owEchoRead đang block
         ctx->recvPending = 0  → owEchoRead thoát ngay lập tức

Bước 3: Tái kích hoạt luồng nhận
         HAL_UART_Receive_IT(huart, &ctx->rxByte, 1U)
```

**Bắt buộc thêm vào `main.c` (xem mục 6):**

```c
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  owErrorHandler(huart);
}
```

### Timeout `owEchoRead` — 10 ms

| Baud rate | Thời gian 1 byte | Timeout `owEchoRead` | Hệ số dự phòng |
|-----------|-----------------|----------------------|----------------|
| 115 200   | ~86 µs          | 10 ms                | ~116 ×         |
| 9 600     | ~1.04 ms        | 10 ms                | ~10 ×          |

Thời gian xấu nhất khi bus chết hoàn toàn:
`64 bit × 3 lần/bit × 10 ms = 1.92 giây` — phục hồi trong 1 chu kỳ polling.

---

## 12) Cơ chế lọc phantom device

### Vấn đề

Khi bus điện không ổn định (rút/cắm nóng), noise sinh ra một **false presence pulse**.
`owSearchCmd()` "tìm thấy" một ROM rác. Xác suất 1/256 để CRC-8 trùng khớp tình cờ là đủ để
pass kiểm tra đơn giản, khiến hệ thống báo `devices found: 1` dù không có cảm biến thật nào.

Ngoài ra, khi bus bị kéo xuống GND hoàn toàn, tất cả bit đọc về đều là `0`
→ ROM = `00-00-00-00-00-00-00-00` có CRC = `0x00` → cũng pass kiểm tra CRC.

Thêm nữa, `owSearchCmd()` trả về `-1` khi bus lỗi; nếu cast trực tiếp sang `uint8_t`
sẽ cho `255` → vòng `for` đọc `romIds[0..254]` — out-of-bounds — tình cờ tìm được CRC match.

### Ba lớp bảo vệ trong `get_ROMid()`

| Lớp | Điều kiện kiểm tra | Mục đích |
|-----|-------------------|----------|
| **0 – Kiểm tra `owSearchCmd`** | `searchResult <= 0` → trả `-1` ngay | Ngăn cast `-1` → `255` (overflow bug) |
| **1 – CRC-8** | `owCRC8(rom) == rom->crc` | Loại ROM ngẫu nhiên |
| **2 – Family code** | `family == 0x28` (DS18B20) hoặc `0x10` (DS18S20) | ROM giả hiếm khi có family code hợp lệ **và** CRC khớp cùng lúc |
| **3 – Serial không all-zero** | Ít nhất 1 trong 6 byte serial ≠ `0x00` | Loại artefact bus-stuck-LOW |

Nếu tất cả ROM không qua 4 lớp → `ctx->devices = 0`, `get_ROMid()` trả `-1`
→ `EnsureReady()` vào nhánh "no device" (im lặng nhờ `noDeviceLogged` guard).

### Kết hợp với guard chống spam log

`noDeviceLogged` và `ensureReadyLogged` trong `Ds18b20StateSlot` đảm bảo:

```
Poll 1: get_ROMid FAIL         → in "no device", noDeviceLogged=1
Poll 2: get_ROMid OK(1 giả)   → 1 phantom bị lọc → thực ra FAIL
                               → noDeviceLogged==1 → IM LẶNG ✓
Poll N: get_ROMid OK(4 thật)  → 4 ROM hợp lệ → EnsureReady thành công
                               → xóa tất cả flag, in "all sensors ready" ✓
```

**Rule:** `noDeviceLogged` và `faultLoggedMask` chỉ được reset một nơi duy nhất —
sau khi `owInitSensorPositions()` thành công, tức là khi hệ thống **thực sự fully ready**.

---

## 13) Sơ đồ tóm tắt

```
                    STM32 Project của bạn
┌──────────────────────────────────────────────────────────────┐
│                                                              │
│   main.c                                                     │
│   ├── HAL_UART_RxCpltCallback() → owReadHandler()            │
│   └── HAL_UART_ErrorCallback()  → owErrorHandler()  ◄── MỚI │
│                                                              │
│   TaskDs18b20                                                │
│   ├── owCfg1.huart = &huart1;  owCfg1.maxDevices = 4U;      │
│   ├── Ds18b20Api_Init()                                      │
│   └── loop:                                                  │
│        Ds18b20Api_Service()                                  │
│         ├── EnsureReady()                                    │
│         │    ├── get_ROMid()          [1-Wire scan + lọc phantom] │
│         │    └── owInitSensorPositions()                     │
│         │         ├── Load Flash  (nếu có)                   │
│         │         └── Học nhiệt   (nếu cần)                  │
│         ├── ReadAndQueue()                                   │
│         │    ├── owGetTemperatureByPosition()                │
│         │    ├── skip fault markers (-1/-2/-3)       ◄── MỚI │
│         │    └── osMessageQueuePut() × maxDevices            │
│         └── onFault() nếu -1/-2/-3  (chỉ 1 lần/sensor) ◄── MỚI
│                                                              │
│   TaskDisplay / TaskLog                                      │
│   └── osMessageQueueGet() → Ds18b20Api_PrintItem()           │
│                                                              │
│              UART Half-Duplex (TX+RX internal loopback)      │
│                      │                                       │
└──────────────────────┼───────────────────────────────────────┘
                       │ 4.7 kΩ pull-up
              [DS18B20 #1] [DS18B20 #2] [DS18B20 #3] [DS18B20 #4]
```

---

## 14) Các lỗi thường gặp khi tích hợp

### Lỗi biên dịch: `itm_print` undefined

Thư viện dùng `itm_print()` từ `ITMConsole_lib`. Nếu project của bạn không có lib này, tạo file stub:

```c
// ds18b20_uart/itm_stub.h
#include <stdio.h>
#define itm_print(s)     printf(s)
#define itm_put_int(n)   printf("%d", (int)(n))
```

Rồi include trong `OneWireUart.c` và `ds18b20_app.c`:
```c
#include "itm_stub.h"   // thay cho #include "itm.h"
```

### Lỗi: `No device found` liên tục

- Kiểm tra pull-up 4.7 kΩ đã hàn chưa.
- Kiểm tra UART đúng Half-Duplex mode trong CubeMX.
- Kiểm tra `owCfg1.huart` trỏ đúng handle (`&huart1` không phải `&huart2`).
- Đảm bảo `HAL_UART_RxCpltCallback` đã gọi `owReadHandler()`.
- Đảm bảo `HAL_UART_ErrorCallback` đã gọi `owErrorHandler()`.

### Lỗi: `missing sensor(s)` mãi không qua

- `owCfg1.maxDevices` đang set lớn hơn số cảm biến thực tế cắm vào.
- Hãy set đúng số cảm biến thực tế.

### Lỗi: nhiệt độ trả về sai thứ tự vị trí

- Position map Flash có thể bị lỗi (do nạp firmware mới đè vào vùng Flash đó).
- Gọi `Ds18b20Api_RequestRelearn()` để xóa và học lại.
- Kiểm tra `ONEWIRE_POSMAP_FLASH_ADDR` không xung đột với Flash của project.

### Lỗi: học vị trí timeout

- Không hơ đúng cảm biến trong 60 giây.
- Nhiệt độ tăng không đủ 1 °C — hơ gần hơn hoặc lâu hơn.
- Hai cảm biến ở quá gần nhau, cả hai cùng tăng — đặt cách nhau về vật lý khi học.

### Lỗi: hệ thống spam log khi rút cả VCC và GND

Đã được xử lý trong phiên bản hiện tại bằng 3 cơ chế:

1. **`owErrorHandler`** — xóa ORE/FE/NE, tái kích hoạt RX-IT ngay lập tức.
2. **`faultLoggedMask`** — `onFault` chỉ được gọi 1 lần duy nhất cho mỗi sensor.
3. **`noDeviceLogged` + `ensureReadyLogged`** — các dòng log trạng thái chỉ in 1 lần cho mỗi chu kỳ lỗi.

Log kỳ vọng khi rút đồng thời VCC + GND:
```
[DS18B20] sensor 1 = 29.5 C, tick=...
[DS18B20] sensor 2 = 29.8 C, tick=...
[DS18B20] sensor 3 = 29.6 C, tick=...
[DS18B20] sensor 4 = 30.0 C, tick=...
[DS18B20][FAULT] sensor 1 disconnected (...)
[DS18B20][FAULT] sensor 2 disconnected (...)
[DS18B20][FAULT] sensor 3 disconnected (...)
[DS18B20][FAULT] sensor 4 disconnected (...)
[DS18B20] no device, waiting for sensors...
```
*(im lặng hoàn toàn cho đến khi cắm lại)*
```
[DS18B20] devices found: 4
[DS18B20] position map loaded from flash
[DS18B20] all sensors ready, start temperature polling
[DS18B20] sensor 1 = 29.5 C, tick=...
```

### Lỗi: `ONEWIRE_MAX_BUS_COUNT exceeded`

Mặc định hỗ trợ tối đa 5 bus đồng thời (`ONEWIRE_MAX_BUS_COUNT 5U` trong `OneWireUart.h`).
Nếu project dùng nhiều hơn 5 UART cho DS18B20, tăng hằng số này.

### Lỗi: `ONEWIRE_MAX_DEVICES_LIMIT exceeded`

Mặc định tối đa 8 cảm biến trên một bus (`ONEWIRE_MAX_DEVICES_LIMIT 8U` trong `OneWireUart.h`).
Nếu cần nhiều hơn, tăng hằng số này.
