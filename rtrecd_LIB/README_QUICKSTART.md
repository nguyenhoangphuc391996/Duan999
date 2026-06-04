# rtrecd Quick Start

Hướng dẫn ngắn gọn để dùng encoder `rtrecd` theo cách mới.

---

## 1) Cấu hình CubeMX

- `PB12` → Encoder A
- `PB13` → Encoder B
- `PB14` → SW

- `PB12`, `PB13`: `GPIO_EXTI`, `Rising/Falling`, `Pull-up`
- `PB14`: `GPIO_Input`, `Pull-up`
- Bật `EXTI line[15:10] interrupts`

---

## 2) Thêm thư viện

1. Copy thư mục `rtrecd_LIB` vào project.
2. Thêm include path tới `rtrecd_LIB`.
3. Thêm source path tơi `rtrecd_LIB`.

---

## 3) Khai báo trong `main.c`

```c
#include "itm.h"
#include "rtrecd.h"

static rtrecd_t g_rtrecd = {
  .pin_a = {GPIOB, GPIO_PIN_12},
  .pin_b = {GPIOB, GPIO_PIN_13},
  .pin_sw = {GPIOB, GPIO_PIN_14}
};
```

---

## 4) Xử lý ngắt

```c
/* USER CODE BEGIN 4 */

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == GPIO_PIN_12)
  {
    rtrecd_isr_a(&g_rtrecd);
  }
  else if (GPIO_Pin == GPIO_PIN_13)
  {
    rtrecd_isr_b(&g_rtrecd);
  }
}

/* USER CODE END 4 */
```

---

## 5) Init trong `TaskInput`

```c
void StartTaskInput(void *argument)
{
  /* USER CODE BEGIN 5 */

	if (rtrecd_init(&g_rtrecd) == false)
	  {
	    Error_Handler();
	  }
	  rtrecd_event_t input_data;
  /* Infinite loop */
  for(;;)
  {
	 input_data = rtrecd_process(&g_rtrecd);
	 if (input_data != RTRECD_EVENT_NONE)
	 {
		 (void)osMessageQueuePut(QueueInputHandle, &input_data, 0U, 0U);
	 }

    osDelay(2);
  }
  /* USER CODE END 5 */
}
```

---

## 6) Task lấy event

```c
void StartTaskUI(void *argument)
{
  /* USER CODE BEGIN StartTaskUI */
	rtrecd_event_t ev;
  /* Infinite loop */
  for(;;)
  {

    /* Block until an event is available */
    if (osMessageQueueGet(QueueInputHandle, &ev, NULL, osWaitForever) == osOK)
    {
      const char *label;
      switch (ev)
      {
        case RTRECD_EVENT_ROTATE_CW:
          label = "RIGHT";
          break;
        case RTRECD_EVENT_ROTATE_CCW:
          label = "LEFT";
          break;
        case RTRECD_EVENT_BUTTON_SHORT:
          label = "PRESS";
          break;
        case RTRECD_EVENT_BUTTON_LONG:
          label = "LONG_PRESS";
          break;
        case RTRECD_EVENT_NONE:
        default:
          label = "NONE";
          break;
      }

      /* Use single itm_print API to output the message (split into parts). */
      itm_print("QueueInput event: ");
      itm_print(label);
      itm_print("\r\n");
    }
    else
    {
      osDelay(1);
    }
  }
  /* USER CODE END StartTaskUI */
}

```

---

## 7) Kết quả

- Quay phải → `RIGHT`
- Quay trái → `LEFT`
- Nhấn ngắn → `PRESS`
- Nhấn lâu → `LONG_PRESS`

---

## 8) Nhớ nhanh

1. CubeMX tạo EXTI/NVIC
2. `main.c` chỉ khai báo `static rtrecd_t`
3. `TaskUI` gọi `rtrecd_init()`
4. `TaskInput` gọi `rtrecd_process()`
