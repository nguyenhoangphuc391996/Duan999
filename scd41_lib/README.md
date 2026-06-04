# `scd41_lib` — Hướng dẫn sử dụng

## 1. Tổng quan

Thư viện gồm 3 tầng:

| Tầng 			| File 						| Mô tả 											|
|---			|---						|---												|
| Driver 		| `scd4x_i2c.h/.c` 			| API cao cấp: runtime, poll, queue, fault recovery |
| HAL wrapper 	| `sensirion_i2c_hal.h/.c` 	| Bridge giữa driver và STM32 HAL + RTOS mutex 		|
| Print helper	| `scd41_print.h/.c`  		| In kết quả đo ra ITM console 						|

---

## 2. Struct cấu hình

```c
scd41_config_t config = {0};
config.i2c_handle             = &hi2c1;            // I2C handle của STM32 HAL
config.i2c_mutex              = MutexI2CHandle;    // Mutex FreeRTOS bảo vệ bus I2C
config.i2c_address            = SCD41_I2C_ADDR_62; // 0x62 (mặc định, tự set trong init)
config.restart_after_failures = 3;                 // Tự restart sau N lỗi liên tiếp (0 = tắt)
```

```c
scd41_context_t context = {0}; // Output: dữ liệu đo + trạng thái lỗi runtime
```

---

## 3. API quan trọng

### Khởi tạo

```c
void scd4x_runtime_init(scd41_config_t *config, scd41_context_t *context);
```
Áp dụng giá trị mặc định vào `config`, xóa `context`, và bind handle/mutex vào HAL layer.
**Gọi một lần trước khi dùng bất kỳ API nào khác.**

```c
int16_t scd4x_runtime_start_periodic_measurement(scd41_config_t *config,
                                                  scd41_context_t *context);
```
Gửi lệnh `start_periodic_measurement` xuống sensor. Sensor sẽ đo định kỳ mỗi 5 giây.
**Gọi một lần sau `scd4x_runtime_init`.**

---

### Poll & đọc dữ liệu

```c
bool scd4x_runtime_poll(const scd41_config_t *config, scd41_context_t *context);
```
Kiểm tra xem sensor đã có dữ liệu mới chưa. Nếu có thì đọc vào `context`.
- Trả về `true` nếu có mẫu mới → đọc `context->co2`, `context->temperature_m_deg_c`, `context->humidity_m_percent_rh`.
- Trả về `false` nếu chưa có dữ liệu hoặc lỗi.
- Tự gọi lại `start_periodic_measurement` nếu phát hiện sensor mất kết nối rồi phục hồi.

```c
int16_t scd4x_runtime_read_if_ready(const scd41_config_t *config,
                                     scd41_context_t *context);
```
Phiên bản thấp hơn của `poll`: chỉ đọc nếu ready, không xử lý fault/restart.

---

### Service API (khuyên dùng trong FreeRTOS)

```c
bool Scd41Api_Service(const scd41_config_t *config,
                      scd41_context_t *context,
                      osMessageQueueId_t queue,
                      scd4x_runtime_event_callback_t on_event);
```
Gọi định kỳ từ task. Bên trong:
1. Set callback event nếu có thay đổi.
2. Gọi `scd4x_runtime_poll()`.
3. Nếu có mẫu mới → đóng gói `scd41_queue_item_t` và push vào `queue`.
4. Trả về `true` khi push thành công.

---

### Callback sự kiện

```c
void scd4x_runtime_set_event_callback(const scd41_config_t *config,
                                       scd41_context_t *context,
                                       scd4x_runtime_event_callback_t callback,
                                       void *user_context);
```
Đăng ký callback được gọi khi:
- `SCD4X_RUNTIME_EVENT_FAULT` — phát hiện lỗi I2C mới
- `SCD4X_RUNTIME_EVENT_RECOVERED` — bus phục hồi sau lỗi
- `SCD4X_RUNTIME_EVENT_RESTART_ATTEMPT` — thư viện tự restart sensor

Callback mặc định đã có sẵn (in ra ITM):
```c
scd4x_runtime_default_itm_event_handler
```

---

## 4. Sử dụng một SCD41 (trường hợp thông thường)

```c

static scd41_config_t  g_scd41_cfg = {0};
static scd41_context_t g_scd41_ctx = {0};

/* --- Trong task khởi tạo --- */
scd41_config_t  g_scd41_cfg = {0};
scd41_context_t g_scd41_ctx = {0};
g_scd41_cfg.i2c_handle = &hi2c1;
g_scd41_cfg.i2c_mutex  = MutexI2C1Handle;

scd4x_runtime_init(&g_scd41_cfg, &g_scd41_ctx);
scd4x_runtime_start_periodic_measurement(&g_scd41_cfg, &g_scd41_ctx);

/* --- Trong vòng lặp task (poll mỗi 500ms là đủ) --- */
for (;;)
{
    Scd41Api_Service(&g_scd41_cfg,
                     &g_scd41_ctx,
                     QueueSCD41Handle,
                     scd4x_runtime_default_itm_event_handler);
    osDelay(500);
}

/* --- Task consumer đọc queue --- */
scd41_queue_item_t measurement;
if (osMessageQueueGet(QueueSCD41Handle, &measurement, NULL, osWaitForever) == osOK)
{
    scd41_print_scd41_measurement(measurement.co2,
                                  measurement.temperature_m_deg_c,
                                  measurement.humidity_m_percent_rh);
}
```

---

## 5. Sử dụng 2 SCD41 trên 2 đường I2C khác nhau

### Vấn đề cần hiểu

Thư viện dùng một biến global duy nhất `g_i2c_handle` trong HAL layer. Mỗi lần gọi API runtime, thư viện luôn set lại `g_i2c_handle` và `g_i2c_mutex` từ `config` trước khi giao tiếp I2C:

```c
// Bên trong scd4x_runtime_read_if_ready()
scd4x_runtime_lock(config);          // acquire mutex của config này
sensirion_i2c_hal_set_handle(...);   // ghi đè g_i2c_handle
sensirion_i2c_hal_set_mutex(...);    // ghi đè g_i2c_mutex
// ... thực hiện I2C ...
scd4x_runtime_unlock(config);
```

**Vấn đề race condition:** Nếu 2 instance dùng 2 mutex khác nhau (`MutexI2C1` và `MutexI2C2`),
cả hai có thể acquire mutex của mình cùng lúc, rồi tranh nhau ghi đè `g_i2c_handle` —
dẫn đến instance A giao tiếp bằng bus I2C của instance B.

### Giải pháp: dùng 1 mutex chung cho cả 2 instance

```c
/* Tạo 1 mutex duy nhất cho toàn bộ HAL layer SCD41 */
osMutexId_t MutexSCD41Handle;
MutexSCD41Handle = osMutexNew(&MutexSCD41_attributes);

/* Instance 1: I2C1 */
static scd41_config_t  g_scd41_1_cfg = {0};
static scd41_context_t g_scd41_1_ctx = {0};
g_scd41_1_cfg.i2c_handle = &hi2c1;
g_scd41_1_cfg.i2c_mutex  = MutexSCD41Handle; /* mutex chung */

/* Instance 2: I2C2 */
static scd41_config_t  g_scd41_2_cfg = {0};
static scd41_context_t g_scd41_2_ctx = {0};
g_scd41_2_cfg.i2c_handle = &hi2c2;
g_scd41_2_cfg.i2c_mutex  = MutexSCD41Handle; /* cùng mutex */

scd4x_runtime_init(&g_scd41_1_cfg, &g_scd41_1_ctx);
scd4x_runtime_init(&g_scd41_2_cfg, &g_scd41_2_ctx);
scd4x_runtime_start_periodic_measurement(&g_scd41_1_cfg, &g_scd41_1_ctx);
scd4x_runtime_start_periodic_measurement(&g_scd41_2_cfg, &g_scd41_2_ctx);
```

Khi cả 2 instance dùng cùng 1 mutex → chuỗi `lock → set_handle → I2C → unlock` luôn là
atomic, không bao giờ bị xen giữa bởi instance kia.

### Poll 2 instance trong cùng 1 task (cách đơn giản nhất)

Nếu chỉ có **1 task** gọi cả 2 instance tuần tự thì race condition không xảy ra.
Mutex chung vẫn nên dùng để an toàn nếu sau này tách task.

```c
/* QueueSCD41_1 và QueueSCD41_2 là 2 queue riêng biệt */
for (;;)
{
    Scd41Api_Service(&g_scd41_1_cfg, &g_scd41_1_ctx,
                     QueueSCD41_1Handle,
                     scd4x_runtime_default_itm_event_handler);

    Scd41Api_Service(&g_scd41_2_cfg, &g_scd41_2_ctx,
                     QueueSCD41_2Handle,
                     scd4x_runtime_default_itm_event_handler);

    osDelay(500);
}
```

### Tóm tắt quy tắc dùng nhiều instance

| Tình huống | Mutex | An toàn? |
|---|---|---|
| 2 instance, 1 task, tuần tự | 2 mutex riêng hoặc 1 chung | ✅ |
| 2 instance, 2 task riêng | **1 mutex chung** | ✅ |
| 2 instance, 2 task riêng | 2 mutex riêng | ❌ Race condition |

---

## 6. Lưu ý

- Queue phải khai báo kích thước item là `sizeof(scd41_queue_item_t)`, **không phải** `sizeof(uint16_t)`.
  Sai kích thước sẽ làm nhiệt độ/độ ẩm đọc ra rác.
- Sensor SCD41 cập nhật dữ liệu mỗi **5 giây** — poll mỗi 500ms là đủ, không cần poll liên tục.
- `config.restart_after_failures = 3` nghĩa là sau 3 lỗi I2C liên tiếp, thư viện tự gửi lại
  `start_periodic_measurement`. Đặt `0` để tắt tính năng này.
- `sensirion_i2c_hal_recover_bus()` được gọi tự động khi HAL trả lỗi —
  nó DeInit rồi Init lại peripheral I2C để xử lý bus treo.