/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "itm.h"
#include "rtrecd.h"
#include "lcd.h"
#include "scd4x_i2c.h"
#include "ds18b20_app.h"
#include "app_menu.h"
#include "app_settings.h"
#include "output.h"
#include "output_control.h"
#include "fan.h"
#include <string.h>

#undef Error_Handler
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;
I2C_HandleTypeDef hi2c2;

RTC_HandleTypeDef hrtc;

TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim3;

UART_HandleTypeDef huart1;

/* Definitions for TaskInput */
osThreadId_t TaskInputHandle;
const osThreadAttr_t TaskInput_attributes = {
  .name = "TaskInput",
  .stack_size = 500 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for TaskUI */
osThreadId_t TaskUIHandle;
const osThreadAttr_t TaskUI_attributes = {
  .name = "TaskUI",
  .stack_size = 500 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for TaskLCD */
osThreadId_t TaskLCDHandle;
const osThreadAttr_t TaskLCD_attributes = {
  .name = "TaskLCD",
  .stack_size = 300 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for TaskDS18B20 */
osThreadId_t TaskDS18B20Handle;
const osThreadAttr_t TaskDS18B20_attributes = {
  .name = "TaskDS18B20",
  .stack_size = 500 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for TaskOutput */
osThreadId_t TaskOutputHandle;
const osThreadAttr_t TaskOutput_attributes = {
  .name = "TaskOutput",
  .stack_size = 200 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for TaskFanLearn */
osThreadId_t TaskFanLearnHandle;
const osThreadAttr_t TaskFanLearn_attributes = {
  .name = "TaskFanLearn",
  .stack_size = 300 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for QueueEC11 */
osMessageQueueId_t QueueEC11Handle;
const osMessageQueueAttr_t QueueEC11_attributes = {
  .name = "QueueEC11"
};
/* Definitions for QueueSCD41 */
osMessageQueueId_t QueueSCD41Handle;
const osMessageQueueAttr_t QueueSCD41_attributes = {
  .name = "QueueSCD41"
};
/* Definitions for QueueDS18B20 */
osMessageQueueId_t QueueDS18B20Handle;
const osMessageQueueAttr_t QueueDS18B20_attributes = {
  .name = "QueueDS18B20"
};
/* Definitions for QueueOutput */
osMessageQueueId_t QueueOutputHandle;
const osMessageQueueAttr_t QueueOutput_attributes = {
  .name = "QueueOutput"
};
/* Definitions for MutexI2C2 */
osMutexId_t MutexI2C2Handle;
const osMutexAttr_t MutexI2C2_attributes = {
  .name = "MutexI2C2"
};
/* Definitions for MutexSCD41 */
osMutexId_t MutexSCD41Handle;
const osMutexAttr_t MutexSCD41_attributes = {
  .name = "MutexSCD41"
};
/* Definitions for MutexMenu */
osMutexId_t MutexMenuHandle;
const osMutexAttr_t MutexMenu_attributes = {
  .name = "MutexMenu"
};
/* USER CODE BEGIN PV */

static rtrecd_t g_rtrecd = {
  .pin_a = {GPIOB, GPIO_PIN_12},
  .pin_b = {GPIOB, GPIO_PIN_13},
  .pin_sw = {GPIOB, GPIO_PIN_14}
};

/* Menu context - shared between TaskUI (writer) and TaskLCD (reader/renderer) */
static app_menu_ctx_t g_menu_ctx;
static output_t       g_output;
static fan_ctx_t      g_fan_ctx;

/* Bộ đếm xung TACH (PB4 / TIM3_CH1): tăng trong HAL_TIM_IC_CaptureCallback */
static volatile uint32_t g_tach_pulse_count = 0U;

uint32_t ramduinput, ramduui, ramdulcd, ramduds18b20, ramduoutput, ramdufanlearn;
uint32_t free_heap __attribute__((unused));
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C2_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_RTC_Init(void);
static void MX_TIM1_Init(void);
static void MX_TIM2_Init(void);
static void MX_TIM3_Init(void);
void StartTaskInput(void *argument);
void StartTaskUI(void *argument);
void StartTaskLCD(void *argument);
void StartTaskDS18B20(void *argument);
void StartTaskOutput(void *argument);
void StartTaskFanLearn(void *argument);

/* USER CODE BEGIN PFP */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
 * @brief Cập nhật GPIO báo động PB5 theo mọi nguồn lỗi.
 *        Gọi từ StartTaskFanLearn mỗi vòng lặp.
 */
static void alarm_update(void)
{
  bool scd41_fault    = false;
  uint8_t ds18b20_mask = 0U;

  osMutexAcquire(MutexMenuHandle, osWaitForever);
  scd41_fault   = g_menu_ctx.scd41_fault;
  ds18b20_mask  = g_menu_ctx.ds18b20_fault_mask;
  osMutexRelease(MutexMenuHandle);

  fan_alarm_update(&g_fan_ctx, scd41_fault, ds18b20_mask);
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
	itm_set_library_enabled(ITM_LIB_RTRECD,  false);
	itm_set_library_enabled(ITM_LIB_DS18B20, false);
	itm_set_library_enabled(ITM_LIB_GLOBAL,  true);
	itm_set_library_enabled(ITM_LIB_SCD41,   false);
	itm_set_library_enabled(ITM_LIB_FAN,     true);


  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_I2C2_Init();
  MX_I2C1_Init();
  MX_USART1_UART_Init();
  MX_RTC_Init();
  MX_TIM1_Init();
  MX_TIM2_Init();
  MX_TIM3_Init();
  /* USER CODE BEGIN 2 */

  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();
  /* Create the mutex(es) */
  /* creation of MutexI2C2 */
  MutexI2C2Handle = osMutexNew(&MutexI2C2_attributes);

  /* creation of MutexSCD41 */
  MutexSCD41Handle = osMutexNew(&MutexSCD41_attributes);

  /* creation of MutexMenu */
  MutexMenuHandle = osMutexNew(&MutexMenu_attributes);

  /* USER CODE BEGIN RTOS_MUTEX */
  /* MutexMenu đã được tạo bởi CubeMX ở trên */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  /* creation of QueueEC11 */
  QueueEC11Handle = osMessageQueueNew (16, sizeof(rtrecd_queue_item_t), &QueueEC11_attributes);

  /* creation of QueueSCD41 */
  QueueSCD41Handle = osMessageQueueNew (16, sizeof(scd41_queue_item_t), &QueueSCD41_attributes);

  /* creation of QueueDS18B20 */
  QueueDS18B20Handle = osMessageQueueNew (16, sizeof(Ds18b20QueueItem), &QueueDS18B20_attributes);

  /* creation of QueueOutput */
  QueueOutputHandle = osMessageQueueNew (16, sizeof(uint16_t), &QueueOutput_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of TaskInput */
  TaskInputHandle = osThreadNew(StartTaskInput, NULL, &TaskInput_attributes);

  /* creation of TaskUI */
  TaskUIHandle = osThreadNew(StartTaskUI, NULL, &TaskUI_attributes);

  /* creation of TaskLCD */
  TaskLCDHandle = osThreadNew(StartTaskLCD, NULL, &TaskLCD_attributes);

  /* creation of TaskDS18B20 */
  TaskDS18B20Handle = osThreadNew(StartTaskDS18B20, NULL, &TaskDS18B20_attributes);

  /* creation of TaskOutput */
  TaskOutputHandle = osThreadNew(StartTaskOutput, NULL, &TaskOutput_attributes);

  /* creation of TaskFanLearn */
  TaskFanLearnHandle = osThreadNew(StartTaskFanLearn, NULL, &TaskFanLearn_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  free_heap = xPortGetFreeHeapSize();

  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE|RCC_OSCILLATORTYPE_LSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV4;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_RTC;
  PeriphClkInit.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief I2C2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C2_Init(void)
{

  /* USER CODE BEGIN I2C2_Init 0 */

  /* USER CODE END I2C2_Init 0 */

  /* USER CODE BEGIN I2C2_Init 1 */

  /* USER CODE END I2C2_Init 1 */
  hi2c2.Instance = I2C2;
  hi2c2.Init.ClockSpeed = 100000;
  hi2c2.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c2.Init.OwnAddress1 = 0;
  hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c2.Init.OwnAddress2 = 0;
  hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c2.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C2_Init 2 */

  /* USER CODE END I2C2_Init 2 */

}

/**
  * @brief RTC Initialization Function
  * @param None
  * @retval None
  */
static void MX_RTC_Init(void)
{

  /* USER CODE BEGIN RTC_Init 0 */

  /* USER CODE END RTC_Init 0 */

  RTC_TimeTypeDef sTime = {0};
  RTC_DateTypeDef DateToUpdate = {0};

  /* USER CODE BEGIN RTC_Init 1 */

  /* USER CODE END RTC_Init 1 */

  /** Initialize RTC Only
  */
  hrtc.Instance = RTC;
  hrtc.Init.AsynchPrediv = RTC_AUTO_1_SECOND;
  hrtc.Init.OutPut = RTC_OUTPUTSOURCE_ALARM;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN Check_RTC_BKUP */
  /*
   * Kiểm tra backup register DR1:
   * - Nếu = 0xA5A5 → RTC đã được set từ trước (VBAT còn), bỏ qua SetTime/SetDate.
   * - Nếu khác    → Lần đầu khởi động hoặc VBAT hết, cần set lại thời gian.
   */
  if (HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR1) == 0xA5A5U)
  {
      /*
       * Khôi phục ngày từ BKP DR2/DR3 (lưu bởi app_menu_write_time_to_rtc)
       * để phòng trường hợp debugger/reset xoá BKP ngày của HAL.
       * DR2 [15:9]=year-2000  [8:5]=month  [4:0]=day
       * DR3 = 0x5A5A (magic)
       */
      if (HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR3) == 0x5A5AU)
      {
          uint16_t d = (uint16_t)HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR2);
          RTC_DateTypeDef sDateRestore = {0};
          sDateRestore.Year    = (uint8_t)((d >> 9U) & 0x7FU);
          sDateRestore.Month   = (uint8_t)((d >> 5U) & 0x0FU);
          sDateRestore.Date    = (uint8_t)(d & 0x1FU);
          sDateRestore.WeekDay = RTC_WEEKDAY_MONDAY;
          HAL_RTC_SetDate(&hrtc, &sDateRestore, RTC_FORMAT_BIN);
      }
      /*
       * Bộ đếm RTC_CNT vẫn chạy (VBAT). Nếu đã qua 0h kể từ lần lưu DR2,
       * GetDate (gọi GetTime bên trong) sẽ cộng thêm ngày cho DateToUpdate.
       */
      {
          RTC_DateTypeDef sDateSync = {0};
          (void)HAL_RTC_GetDate(&hrtc, &sDateSync, RTC_FORMAT_BIN);
      }
      return; /* Thời gian RTC vẫn còn, không cần reset */
  }
  /* USER CODE END Check_RTC_BKUP */

  /** Initialize RTC and set the Time and Date
  */
  sTime.Hours = 0x0;
  sTime.Minutes = 0x0;
  sTime.Seconds = 0x0;

  if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BCD) != HAL_OK)
  {
    Error_Handler();
  }
  DateToUpdate.WeekDay = RTC_WEEKDAY_MONDAY;
  DateToUpdate.Month = RTC_MONTH_JANUARY;
  DateToUpdate.Date = 0x1;
  DateToUpdate.Year = 0x0;

  if (HAL_RTC_SetDate(&hrtc, &DateToUpdate, RTC_FORMAT_BCD) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RTC_Init 2 */
  /* Ghi magic number vào BKP DR1 để lần sau biết RTC đã được set */
  HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR1, 0xA5A5U);
  /* USER CODE END RTC_Init 2 */

}

/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 3;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 19999;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim1, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_4) != HAL_OK)
  {
    Error_Handler();
  }
  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = 0;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim1, &sBreakDeadTimeConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */
  HAL_TIM_MspPostInit(&htim1);

}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 0;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 159;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_LOW;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */
  HAL_TIM_MspPostInit(&htim2);

}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_IC_InitTypeDef sConfigIC = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 3;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 65535;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_IC_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigIC.ICPolarity = TIM_INPUTCHANNELPOLARITY_FALLING;
  sConfigIC.ICSelection = TIM_ICSELECTION_DIRECTTI;
  sConfigIC.ICPrescaler = TIM_ICPSC_DIV1;
  sConfigIC.ICFilter = 8;
  if (HAL_TIM_IC_ConfigChannel(&htim3, &sConfigIC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_HalfDuplex_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_15|GPIO_PIN_5, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8|GPIO_PIN_12, GPIO_PIN_RESET);

  /*Configure GPIO pins : PB12 PB13 PB14 */
  GPIO_InitStruct.Pin = GPIO_PIN_12|GPIO_PIN_13|GPIO_PIN_14;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : PB15 PB5 */
  GPIO_InitStruct.Pin = GPIO_PIN_15|GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : PA8 PA12 */
  GPIO_InitStruct.Pin = GPIO_PIN_8|GPIO_PIN_12;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

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
  else if (GPIO_Pin == GPIO_PIN_14)
  {
    rtrecd_isr_sw(&g_rtrecd);
  }
}

void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM3)
  {
    g_tach_pulse_count++;
  }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  owReadHandler(huart);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  owErrorHandler(huart);
}
/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartTaskInput */
/**
  * @brief  Function implementing the TaskInput thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartTaskInput */
void StartTaskInput(void *argument)
{
  /* USER CODE BEGIN 5 */
	if (rtrecd_init(&g_rtrecd) == false)
	{
		Error_Handler();
	}

	scd41_config_t scd41_config = {0};
	scd41_context_t scd41_context = {0};

	scd41_config.i2c_handle = &hi2c1;
	scd41_config.i2c_mutex = MutexSCD41Handle;

	scd4x_runtime_init(&scd41_config, &scd41_context);
	scd4x_runtime_start_periodic_measurement(&scd41_config, &scd41_context);

	uint32_t scd41_last_tick = osKernelGetTickCount();

  /* Infinite loop */
  for(;;)
  {
	  /* Đọc encoder trước/sau service cảm biến để giảm mất bước khi I2C bận. */
	  (void)rtrecd_service(&g_rtrecd, QueueEC11Handle);

	  if ((osKernelGetTickCount() - scd41_last_tick) >= 500U)
	  {
		  scd41_last_tick = osKernelGetTickCount();
		  Scd41Api_Service(&scd41_config,
		                   &scd41_context,
		                   QueueSCD41Handle,
		                   scd4x_runtime_default_itm_fault_event_handler);
	  }

	  (void)rtrecd_service(&g_rtrecd, QueueEC11Handle);

	  ramduinput = uxTaskGetStackHighWaterMark(NULL);
	  osDelay(1);
  }
  /* USER CODE END 5 */
}

/* USER CODE BEGIN Header_StartTaskUI */
/**
* @brief Function implementing the TaskUI thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTaskUI */
void StartTaskUI(void *argument)
{
  /* USER CODE BEGIN StartTaskUI */
	rtrecd_queue_item_t ev;
	scd41_queue_item_t scd41_data;

	bool got_event;

	/* Đọc RTC mỗi 1 giây */
	uint32_t rtc_last_tick = osKernelGetTickCount();

	/* Đồng bộ ngay lần đầu (trước đó app_menu_init chỉ set mặc định) */
	osMutexAcquire(MutexMenuHandle, osWaitForever);
	app_menu_update_time_from_rtc(&g_menu_ctx, &hrtc);
	osMutexRelease(MutexMenuHandle);

  /* Infinite loop */
  for(;;)
  {
	  got_event = false;

	  /* EC11: xử lý hết event trong queue (tránh tồn đọng khi MutexMenu bận). */
	  while (osMessageQueueGet(QueueEC11Handle, &ev, NULL, 0U) == osOK)
	  {
		  osMutexAcquire(MutexMenuHandle, osWaitForever);
		  app_menu_handle_event(&g_menu_ctx, ev);
		  if (g_menu_ctx.time_rtc_dirty)
		  {
			  app_menu_write_time_to_rtc(&g_menu_ctx, &hrtc);
		  }
		  osMutexRelease(MutexMenuHandle);
		  got_event = true;
	  }

	  /* SCD41 data -> menu sensor update + ITM log */
	  if (osMessageQueueGet(QueueSCD41Handle, &scd41_data, NULL, 0U) == osOK)
	  {
		  scd41_print_scd41_measurement(scd41_data.co2,
				  scd41_data.temperature_m_deg_c,
				  scd41_data.humidity_m_percent_rh);

		  osMutexAcquire(MutexMenuHandle, osWaitForever);
		  app_menu_update_scd41(&g_menu_ctx, &scd41_data);
		  /* Khi nhận được data hợp lệ, xóa fault trên LCD */
		  app_menu_set_scd41_fault(&g_menu_ctx, false);
		  osMutexRelease(MutexMenuHandle);

		  got_event = true;
	  }

	  /* Cập nhật trạng thái lỗi SCD41 lên LCD (đồng bộ từ thư viện scd41) */
	  {
		  bool fault = scd4x_runtime_fault_flag_active();
		  osMutexAcquire(MutexMenuHandle, osWaitForever);
		  app_menu_set_scd41_fault(&g_menu_ctx, fault);
		  osMutexRelease(MutexMenuHandle);
	  }

	  /* Đọc RTC mỗi 1 giây -> cập nhật time_cfg và dirty LCD */
	  if ((osKernelGetTickCount() - rtc_last_tick) >= 1000U)
	  {
		  rtc_last_tick = osKernelGetTickCount();
		  osMutexAcquire(MutexMenuHandle, osWaitForever);
		  app_menu_update_time_from_rtc(&g_menu_ctx, &hrtc);
		  osMutexRelease(MutexMenuHandle);
		  got_event = true;
	  }

	  if(got_event)
	  {
		  continue;
	  }
	  osDelay(10);

	  ramduui = uxTaskGetStackHighWaterMark(NULL);

  }
  /* USER CODE END StartTaskUI */
}

/* USER CODE BEGIN Header_StartTaskLCD */
/**
* @brief Function implementing the TaskLCD thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTaskLCD */
void StartTaskLCD(void *argument)
{
  /* USER CODE BEGIN StartTaskLCD */
	lcd_i2c_config_t lcd_cfg = {
	    .hi2c = &hi2c2,
	    .i2c_addr = 0x27 << 1,
	    .mutex = MutexI2C2Handle,
	};

    itm_print("[LCD] init...\r\n");
    lcd_init(&lcd_cfg);
    itm_print("[LCD] init OK\r\n");

    /* Khởi tạo menu sau khi LCD đã sẵn sàng */
    itm_print("[FLASH] app_menu_init -> load settings\r\n");
    osMutexAcquire(MutexMenuHandle, osWaitForever);
    app_menu_init(&g_menu_ctx);
    osMutexRelease(MutexMenuHandle);
    itm_print("[FLASH] app_menu_init done\r\n");

  /* Infinite loop */
  for(;;)
  {
	  osMutexAcquire(MutexMenuHandle, osWaitForever);
	  app_menu_render(&g_menu_ctx);
	  osMutexRelease(MutexMenuHandle);

	  ramdulcd = uxTaskGetStackHighWaterMark(NULL);
	  osDelay(50);
  }
  /* USER CODE END StartTaskLCD */
}

/* USER CODE BEGIN Header_StartTaskDS18B20 */
/**
* @brief Function implementing the TaskDS18B20 thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTaskDS18B20 */
void StartTaskDS18B20(void *argument)
{
  /* USER CODE BEGIN StartTaskDS18B20 */
	  OneWire_Config  owCfg1;
	  OneWire_Context owCtx1;

	  owCfg1.huart      = &huart1;
	  owCfg1.maxDevices = 4U;   /* Tạm thời; vòng lặp đầu sẽ đồng bộ từ ds18b20_target_count */

	  Ds18b20Api_Init(&owCfg1, &owCtx1);
	  Ds18b20Api_BindMenuCtx(&owCfg1, &g_menu_ctx);

  /* Infinite loop */
  for(;;)
  {
	  /* ---- Đồng bộ số cảm biến từ settings (bao gồm sau khi load Flash) ---- */
	  if (g_menu_ctx.ds18b20_target_count > 0U && !g_menu_ctx.relearn_req)
	  {
		  owCfg1.maxDevices = g_menu_ctx.ds18b20_target_count;
	  }

	  /* ---- Xử lý yêu cầu học lại vị trí từ menu ---- */
	  if (g_menu_ctx.relearn_req)
	  {
		  /* Cập nhật số cảm biến theo cài đặt người dùng */
		  uint8_t target = g_menu_ctx.ds18b20_target_count;
		  if (target < 1U) target = 1U;
		  if (target > (uint8_t)MENU_DS18B20_MAX) target = (uint8_t)MENU_DS18B20_MAX;
		  owCfg1.maxDevices = target;

		  Ds18b20Api_RequestRelearn(&owCfg1, &owCtx1);

		  g_menu_ctx.relearn_req         = 0U;
		  g_menu_ctx.relearn_retry_count = 0U;
		  g_menu_ctx.relearn_phase       = DS18B20_LEARN_SEARCHING;
	  }

	  /* ---- Service bình thường ---- */
	  HAL_StatusTypeDef res = Ds18b20Api_Service(&owCfg1, &owCtx1,
			  	  	  QueueDS18B20Handle,
                      Ds18b20Api_GetFaultCb());

	  /* ---- Đọc kết quả từ queue và cập nhật menu ---- */
	  {
		  Ds18b20QueueItem ds18b20_data;
		  while (osMessageQueueGet(QueueDS18B20Handle, &ds18b20_data, NULL, 0U) == osOK)
		  {
			  Ds18b20Api_PrintItem(&ds18b20_data);
			  osMutexAcquire(MutexMenuHandle, osWaitForever);
			  app_menu_update_ds18b20(&g_menu_ctx, &ds18b20_data);
			  osMutexRelease(MutexMenuHandle);
		  }
	  }

	  /* ---- Cập nhật kết quả học vị trí ---- */
	  if (g_menu_ctx.relearn_phase == DS18B20_LEARN_SEARCHING)
	  {
		  if (res == HAL_OK)
		  {
			  /* Service OK = EnsureReady thành công + đọc nhiệt độ OK */
			  g_menu_ctx.relearn_phase = DS18B20_LEARN_DONE;
		  }
		  else
		  {
			  g_menu_ctx.relearn_retry_count++;
			  /* Timeout ~60s: 12 lần × 5s */
			  if (g_menu_ctx.relearn_retry_count > 12U)
			  {
				  g_menu_ctx.relearn_phase = DS18B20_LEARN_ERROR;
			  }
		  }
	  }

	  ramduds18b20 = uxTaskGetStackHighWaterMark(NULL);
      osDelay(500);
  }
  /* USER CODE END StartTaskDS18B20 */
}

/* USER CODE BEGIN Header_StartTaskOutput */
/**
* @brief Function implementing the TaskOutput thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTaskOutput */
void StartTaskOutput(void *argument)
{
  /* USER CODE BEGIN StartTaskOutput */
  output_cmd_t cmd;
  output_ctrl_snapshot_t snap;
  output_ctrl_state_t ctrl;
  uint32_t control_last_tick = osKernelGetTickCount();

  output_ctrl_state_init(&ctrl);
  output_defaults(&g_output);
  g_output.fan.htim = &htim2;
  g_output.servo1.htim = &htim1;
  g_output.servo2.htim = &htim1;
  if (output_init(&g_output) == false)
  {
    Error_Handler();
  }

  for (;;)
  {
    if (osMessageQueueGet(QueueOutputHandle, &cmd, NULL, 20U) == osOK)
    {
      (void)output_apply_cmd(&g_output, &cmd);
    }

    if ((osKernelGetTickCount() - control_last_tick) >= 200U)
    {
      control_last_tick = osKernelGetTickCount();
      output_ctrl_snapshot_take(&snap, &g_menu_ctx, MutexMenuHandle);

      (void)output_ctrl_apply(&g_output, &ctrl, &snap, osKernelGetTickCount());
    }
    ramduoutput = uxTaskGetStackHighWaterMark(NULL);
  }
  /* USER CODE END StartTaskOutput */
}

/* USER CODE BEGIN Header_StartTaskFanLearn */
/**
* @brief Function implementing the TaskFanLearn thread.
*        - Học tốc độ quạt: tắt quạt, chờ dừng hẳn, rồi tăng PWM 0→100%.
*        - Giám sát RPM đã học: lệch >±25% (2 lần ×5 s) → tắt quạt + QER + còi.
*        - Phục hồi lệch học: chờ quạt dừng hẳn (hết quán tính), quay tay >= 100 RPM mới bật lại;
*          trong ±15% mới tắt còi.
*        - Còi PB5 cũng kêu khi lỗi SCD41 hoặc DS18B20.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTaskFanLearn */
void StartTaskFanLearn(void *argument)
{
  /* USER CODE BEGIN StartTaskFanLearn */

  /* Khởi động TIM3 Input Capture để đếm xung TACH trên PB4 */
  HAL_NVIC_SetPriority(TIM3_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(TIM3_IRQn);
  HAL_TIM_IC_Start_IT(&htim3, TIM_CHANNEL_1);

  /* Khởi tạo fan context: PB5 = GPIO báo động */
  fan_ctx_init(&g_fan_ctx,
               &g_menu_ctx,
               &g_output,
               &g_tach_pulse_count,
               MutexMenuHandle,
               GPIOB,
               GPIO_PIN_5);

  for (;;)
  {
    fan_task_body(&g_fan_ctx);
    alarm_update();

    ramdufanlearn = uxTaskGetStackHighWaterMark(NULL);
    osDelay(100U);
  }
  /* USER CODE END StartTaskFanLearn */
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM4 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM4)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
