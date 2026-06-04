# ds18b20_uart

Thư viện đọc nhiệt độ **DS18B20 / DS18S20** trên STM32 bằng **UART half-duplex**.

Project này dùng lớp thấp `OneWireUart` để giả lập giao thức 1-Wire qua USART, và lớp cao `ds18b20_app` để:

- khởi tạo bus DS18B20;
- quét và kiểm tra cảm biến;
- tự học vị trí từng cảm biến;
- đọc nhiệt độ theo thứ tự vị trí;
- đẩy dữ liệu sang queue để task khác xử lý;
- phát hiện một số lỗi đường dây cảm biến.

---

## 1) Nguyên lý hoạt động

Thư viện dùng chân **TX của USART** để giao tiếp với bus 1-Wire của DS18B20.

### Yêu cầu phần cứng

- 1 chân UART của STM32 cấu hình **Half-Duplex**.
- Chân data của DS18B20 nối vào chân UART đó.
- Điện trở kéo lên **4.7k** từ data lên VCC.
- Nguồn và GND cho cảm biến phải đúng chuẩn.

### Lưu ý

- Thư viện tự chuyển baud giữa:
  - **9600** cho reset/presence pulse
  - **115200** cho các slot dữ liệu
- Trong `HAL_UART_RxCpltCallback()` phải gọi `owReadHandler(huart)` để thư viện nhận byte phản hồi.

---

## 2) Cấu trúc thư viện

### `OneWireUart.h / OneWireUart.c`

Lớp thấp điều khiển bus 1-Wire qua UART.

Các kiểu dữ liệu chính:

- `OneWire_Config`
- `OneWire_Context`
- `RomCode`
- `Temperature`

Các hàm chính:

- `owInit()`
- `owRegisterBus()`
- `get_ROMid()`
- `get_Temperature()`
- `owInitSensorPositions()`
- `owGetTemperatureByPosition()`
- `owReadHandler()`

### `ds18b20_app.h / ds18b20_app.c`

Lớp ứng dụng, dễ dùng hơn cho project FreeRTOS.

Các hàm chính:

- `Ds18b20Api_Init()`
- `Ds18b20Api_RequestRelearn()`
- `Ds18b20Api_EnsureReady()`
- `Ds18b20Api_ReadAndQueue()`
- `Ds18b20Api_Service()`

---

## 3) Cấu hình CubeMX / STM32CubeIDE

### UART

Ví dụ trong project này dùng `USART1`:

- Mode: **Half-Duplex**
- Baud ban đầu: có thể để mặc định, thư viện sẽ tự đổi khi chạy
- 8 data bits, 1 stop bit, no parity
- Bật interrupt receive nếu cần cho callback

### GPIO

- Không cần thêm chân riêng cho 1-Wire nếu dùng đúng kiểu half-duplex của UART.
- Đảm bảo chân data có pull-up ngoài 4.7k.

### FreeRTOS

Project mẫu dùng:

- 1 task đọc DS18B20
- 1 task in log ra ITM / console
- 1 message queue để truyền dữ liệu nhiệt độ

---

## 4) Cách khởi tạo

### 4.1 Khai báo biến

```c
static OneWire_Config owCfg1;
static OneWire_Context owCtx1;
```

### 4.2 Gán UART và số lượng cảm biến

```c
owCfg1.huart = &huart1;
owCfg1.maxDevices = 5U;
```

`maxDevices` là số cảm biến tối đa dự kiến trên bus.
Thư viện hỗ trợ tối đa **8 thiết bị** trong một bus.

### 4.3 Khởi tạo thư viện

```c
if (Ds18b20Api_Init(&owCfg1, &owCtx1) != HAL_OK) {
  // xử lý lỗi
}
```

`Ds18b20Api_Init()` sẽ:

- đăng ký bus UART;
- reset context;
- thiết lập baud dữ liệu / baud reset;
- cố gắng set DS18B20 về **12-bit resolution**.

---

## 5) Luồng sử dụng khuyến nghị

Luồng chuẩn trong task FreeRTOS:

1. Gọi `Ds18b20Api_Init()` khi hệ thống khởi động.
2. Gọi `Ds18b20Api_Service()` định kỳ.
3. Nhận dữ liệu từ queue và hiển thị / xử lý.

### Ví dụ task

```c
void StartTaskds18b20(void *argument)
{
  if (Ds18b20Api_Init(&owCfg1, &owCtx1) != HAL_OK) {
    // log lỗi
  }

  for (;;) {
    if (Ds18b20Api_Service(&owCfg1, &owCtx1, Queueds18b20Handle, Ds18b20_OnWireFault) != HAL_OK) {
      osDelay(1000U);
      continue;
    }

    osDelay(2000U);
  }
}
```

### Callback lỗi dây

```c
static void Ds18b20_OnWireFault(uint8_t sensorIndex, int16_t tempDeciC)
{
  // tempDeciC == -1 hoặc -2 là marker lỗi đường dây
}
```

---

## 6) Ví dụ callback UART bắt buộc

Trong file `main.c`:

```c
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  owReadHandler(huart);
}
```

Nếu không gọi callback này, thư viện sẽ không nhận được byte phản hồi từ bus 1-Wire.

---

## 7) Đọc nhiệt độ theo thứ tự vị trí

Thư viện hỗ trợ gán cảm biến theo **vị trí vật lý** bằng cách “học” ROM của từng cảm biến.

### Học vị trí

```c
if (owInitSensorPositions(&owCfg1, &owCtx1, 0U) != HAL_OK) {
  // xử lý lỗi
}
```

- `forceRelearn = 0U`: ưu tiên load map từ Flash nếu có.
- `forceRelearn = 1U`: ép học lại toàn bộ.

### Đọc theo vị trí

```c
if (owGetTemperatureByPosition(&owCfg1, &owCtx1) == HAL_OK) {
  // owCtx1.temp[i] là nhiệt độ của cảm biến ở vị trí i
}
```

---

## 8) Queue dữ liệu nhiệt độ

Struct queue:

```c
typedef struct {
  uint8_t sensorIndex;
  int16_t tempDeciC;
  uint32_t tick;
} Ds18b20QueueItem;
```

Ý nghĩa:

- `sensorIndex`: chỉ số cảm biến bắt đầu từ **1**
- `tempDeciC`: nhiệt độ theo **0.1°C**
- `tick`: tick hệ thống tại thời điểm đọc

Ví dụ `253` nghĩa là `25.3°C`.

### Đẩy dữ liệu vào queue

Trong `Ds18b20Api_ReadAndQueue()` thư viện sẽ tự đẩy dữ liệu từng cảm biến vào queue.

### Đọc queue

```c
Ds18b20QueueItem item;

if (osMessageQueueGet(Queueds18b20Handle, &item, NULL, 1000U) == osOK) {
  // xử lý item.tempDeciC
}
```

---

## 9) API mức thấp của `OneWireUart`

Nếu muốn dùng trực tiếp không qua `ds18b20_app`, có thể dùng các hàm sau.

### Khởi tạo bus

```c
HAL_StatusTypeDef owInit(OneWire_Config *cfg, OneWire_Context *ctx);
```

### Quét ROM

```c
int get_ROMid(OneWire_Config *cfg, OneWire_Context *ctx);
```

- Trả về `0` khi quét hợp lệ.
- `ctx->devices` sẽ chứa số cảm biến tìm thấy.
- `ctx->romIds[i]` chứa ROM của từng cảm biến.

### Đọc nhiệt độ toàn bus

```c
void get_Temperature(OneWire_Config *cfg, OneWire_Context *ctx);
```

Kết quả được lưu trong:

```c
ctx->temp[i]
```

### Đọc theo ROM

```c
Temperature readTemperature(OneWire_Config *cfg, OneWire_Context *ctx, RomCode *rom, uint8_t reSense);
```

- `reSense = 1`: đọc xong thì ra lệnh convert tiếp để lần sau đọc nhanh hơn.

### Hỗ trợ ROM / scratchpad

- `owCRC8()`
- `owReadScratchpadCmd()`
- `owMatchRomCmd()`
- `owConvertTemperatureCmd()`
- `owCopyScratchpadCmd()`
- `owRecallE2Cmd()`

---

## 10) Quy ước lỗi và giới hạn

### Lỗi đường dây

Trong project mẫu, callback lỗi nhận các marker đặc biệt:

- `-1`: nghi ngờ lỗi **GND/DATA**
- `-2`: nghi ngờ lỗi **VCC**

### Giới hạn

- Tối đa **8 sensor** được lưu trong một context.
- Tối đa **5 bus** được bind đồng thời theo code hiện tại.
- Flash map vị trí được lưu tại:

```c
#define ONEWIRE_POSMAP_FLASH_ADDR 0x0800FC00UL
```

Nếu project của bạn dùng vùng Flash này cho dữ liệu khác, cần đổi địa chỉ này.

---

## 11) Ví dụ tối thiểu

```c
static OneWire_Config owCfg;
static OneWire_Context owCtx;

void App_Init(void)
{
  owCfg.huart = &huart1;
  owCfg.maxDevices = 5U;

  if (Ds18b20Api_Init(&owCfg, &owCtx) != HAL_OK) {
    // error
  }
}

void App_Loop(void)
{
  (void)Ds18b20Api_Service(&owCfg, &owCtx, Queueds18b20Handle, NULL);
}
```

---

## 12) Gợi ý kiểm tra nhanh

1. Nạp firmware.
2. Mở ITM / SWV console.
3. Quan sát log:
   - `Taskds18b20 started`
   - `devices found: ...`
   - `position map saved to flash`
4. Kiểm tra giá trị nhiệt độ được đẩy vào queue.

---

## 13) Ghi chú triển khai

File ví dụ đang dùng:

- `Core/Src/main.c`
- `ds18b20_uart/OneWireUart.c`
- `ds18b20_uart/ds18b20_app.c`

Nếu bạn muốn, có thể mở rộng README này thành bản đầy đủ hơn với:

- sơ đồ chân phần cứng,
- flow chart đọc cảm biến,
- ví dụ cho nhiều UART bus,
- hoặc hướng dẫn tích hợp vào project STM32CubeIDE khác.
