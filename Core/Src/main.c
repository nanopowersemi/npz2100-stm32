/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : nPZ2100 reference application — STM32L053C8Ux
  *
  * INTEGRATION INTO A CUBEMX-GENERATED PROJECT
  * ---------------------------------------------
  * This file is designed to REPLACE the CubeMX-generated main.c.
  *
  * Also required in Core/Src/:
  *   npz2100_all.c  — compiles the nPZ2100 driver sources automatically.
  *                    No Source Location change in Project Properties needed.
  *
  * Also required in compiler include paths (Project Properties →
  * C/C++ Build → Settings → MCU GCC Compiler → Include paths):
  *   ../NPZ2100/Inc
  *
  * hi2c1 is declared extern here — it is defined by CubeMX in i2c.c.
  * If your project does NOT have i2c.c (standalone use), change the
  * extern declaration back to: I2C_HandleTypeDef hi2c1;
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "i2c.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "npz2100_stm32.h"
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

/* USER CODE BEGIN PV */

/* nPZ2100 driver handle — lives for the duration of one boot cycle. */
static NPZ2100_Handle_t hnpz;

/* Tool-generated register map (byte-stream format).
 * Replace with the actual output from the Nanopower configuration tool.
 *
 * Format: [length][start_addr][data_0]...[data_(length-2)]
 * where length = 1 (start_addr) + N (data bytes).
 *
 * The diff inside NPZ2100_ApplyRegmap() ensures only registers that changed
 * since the last boot are written — zero I²C transactions on a warm boot
 * where configuration is unchanged.
 */
static const uint8_t npz2100_regmap[] = {
    /* ---- Global configuration: IOCFG1 @ 0x05 through TOUT_H @ 0x0D ---- */
    10, 0x05,
    0x00,       /* IOCFG1:  host PSW = power-switch mode                    */
    0x00,       /* IOCFG2:  peripheral PSW = power-switch with rise detect  */
    0xFF,       /* IOCFG3:  all INT pull-ups enabled at ~100 kΩ            */
    0x00,       /* IOCFG4:  all INT pins = input active-high               */
    0x1D,       /* IOCFG5:  SPI_AUTO=1, I2C pull-ups auto, PSW_SR=1        */
    0x03,       /* SYSCFG1: peripheral 1 and 2 as wake-up sources           */
    0x04,       /* SYSCFG2: ADC3 (battery) as wake-up source               */
    0xFF,       /* TOUT_L:  maximum time-out (LSB)                          */
    0x01,       /* TOUT_H:  maximum time-out (MSB)                          */

    /* ---- Peripheral 1: I²C sensor @ 0x48, enabled, periodic poll ------- */
    15, 0x1F,
    0x00,       /* P_BANK:  slot 0                                          */
    0x01,       /* CFGP1:   periodic power-on, poll+read+compare            */
    0x00,       /* IOP1:    SW_LP1, INT1, CSN1                             */
    0x00,       /* MODP1:   16-bit unsigned, default threshold trigger      */
    0x00, 0x01, /* PERP1:   polling period = 256 system clocks              */
    0x01,       /* NCMDP1:  1 init command in SRAM                          */
    0x48,       /* ADDRP1:  sensor I²C address                              */
    0x00,       /* RREGP1:  read from register 0x00                         */
    0x20, 0x1C, /* THROVP1: over-threshold                                  */
    0x00, 0x00, /* THRUNP1: under-threshold = 0 (disabled)                  */
    0x08,       /* TWTP1:   post-init wait                                  */

    /* ---- Peripheral 2–6: disabled (default) ----------------------------- */
    15, 0x1F,
    0x01, 0x00, 0x15, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,

    15, 0x1F,
    0x02, 0x00, 0x2A, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,

    15, 0x1F,
    0x03, 0x00, 0x3F, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,

    15, 0x1F,
    0x04, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,

    15, 0x1F,
    0x05, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
};

/* Sensor initialisation commands stored in nPZ2100 SRAM.
 * The nPZ2100 sends these to the sensor during autonomous polling.
 * Each pair = (register_address, value). */
static const uint8_t sensor_init_cmds[] = {
    0x01, 0x00,   /* Example: config register, continuous conversion mode */
};

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void handle_peripheral_wake(uint8_t slot);
static void handle_battery_low(NPZ2100_Handle_t *hnpz);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
 * @brief Handle a peripheral threshold crossing.
 * Reads the last sampled value from the nPZ2100 and processes it.
 */
static void handle_peripheral_wake(uint8_t slot)
{
    uint16_t raw_value;
    NPZ2100_Status_t ret = NPZ2100_PeriphReadValue(&hnpz, slot, &raw_value);
    if (ret != NPZ2100_OK) {
        return;
    }

    /*
     * Example: TMP117 temperature sensor
     * Raw = signed 16-bit, 1 LSB = 0.0078125 °C
     * Convert to integer millidegrees: raw × 78 / 10 ≈ raw × 0.0078125 × 10000
     */
    int32_t temp_mdeg = (int32_t)(int16_t)raw_value * 78;
    (void)temp_mdeg; /* Use temp_mdeg: upload via UART, store to flash, etc. */
}

/**
 * @brief Battery low — increase polling period to conserve energy.
 * Modifies the shadow directly and relies on NPZ2100_ShadowFlush()
 * in main() to push the change before re-entering idle.
 */
static void handle_battery_low(NPZ2100_Handle_t *h)
{
    npz2100_config_t *shadow = NPZ2100_GetShadow(h);
    if (shadow == NULL) { return; }

    /* Double peripheral 1's polling period. */
    npz2100_periph_shadow_t *p = &shadow->periph[0];
    uint16_t period = (uint16_t)p->perp_l | ((uint16_t)p->perp_h << 8u);
    period = (period == 0u) ? 2u : (uint16_t)(period * 2u);
    p->perp_l = (uint8_t)(period & 0xFFu);
    p->perp_h = (uint8_t)(period >> 8u);
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
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
  MX_I2C1_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */

    /* ------------------------------------------------------------------ */
    /* Retarget printf to UART before any driver call.                     */
    /*                                                                     */
    /* Replace huart2 with whichever UART your CubeMX project initialises. */
    /* If your project has MX_USART2_UART_Init() generated, huart2 is     */
    /* declared in usart.c — add  extern UART_HandleTypeDef huart2;  here  */
    /* or declare it in main.h.                                            */
    /*                                                                     */
    /* If you do not want UART printf, comment out the two lines below.    */
    /* ------------------------------------------------------------------ */
    extern UART_HandleTypeDef huart2;   /* defined in Core/Src/usart.c    */
    NPZ2100_UartInit(&huart2);

    /* ---------------------------------------------------------------------- */
    /* 1. Initialise nPZ2100 driver.                                           */
    /*    Seeds shadow with reset defaults, probes device (ID = 0x74).        */
    /* ---------------------------------------------------------------------- */
    NPZ2100_Status_t ret = NPZ2100_Init(&hnpz, &hi2c1);
    if (ret != NPZ2100_OK) {
        /*
         * Device not found or I²C fault.
         * Spin here — the nPZ2100 watchdog will power-cycle the STM32
         * after its configured timeout, giving us a chance to retry.
         */
        while (1) { HAL_Delay(1000); }
    }

    /* ---------------------------------------------------------------------- */
    /* 2. Read wake-up reason.  MUST be first nPZ2100 operation.              */
    /*    Also resets the nPZ2100 watchdog timer.                              */
    /* ---------------------------------------------------------------------- */
    NPZ2100_WakeReason_t reason;
    NPZ2100_BootStatus(&hnpz, &reason);

    /* ---------------------------------------------------------------------- */
    /* 3. Sync shadow from device.                                             */
    /*    nPZ2100 retains all registers while STM32 is off.                   */
    /*    Readback before applying regmap gives an accurate diff.              */
    /* ---------------------------------------------------------------------- */
    NPZ2100_Readback(&hnpz);

    /* ---------------------------------------------------------------------- */
    /* 4. Apply register map — write only what changed.                        */
    /*    On a warm boot with no config change: zero I²C transactions.        */
    /* ---------------------------------------------------------------------- */
    NPZ2100_ApplyRegmap(&hnpz, npz2100_regmap, sizeof(npz2100_regmap));

    /* ---------------------------------------------------------------------- */
    /* 5. On cold boot (power-on reset): write sensor init commands to SRAM.  */
    /*    nPZ2100 sends these autonomously while STM32 is off.                */
    /* ---------------------------------------------------------------------- */
    if (reason.rst_src == NPZ2100_RST_SRC_POR) {
        NPZ2100_SramWrite(&hnpz, 0x00u,
                          sensor_init_cmds, sizeof(sensor_init_cmds));
    }

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */

    /* ---------------------------------------------------------------------- */
    /* 6. Handle wake reason.                                                  */
    /* ---------------------------------------------------------------------- */
    uint8_t config_changed = 0u;

    /* Peripheral threshold crossings */
    for (uint8_t i = 0u; i < 6u; i++) {
        if (reason.periph_mask & (1u << i)) {
            handle_peripheral_wake(i);
        }
    }

    /* Battery low (ADC3 threshold) */
    if (reason.adc3) {
        handle_battery_low(&hnpz);
        config_changed = 1u;
    }

    /* Periodic time-out — nothing to do except re-enter idle */
    if (reason.timeout) {
        /* Heartbeat: log status, update RTC, etc. */
        (void)reason.timeout;
    }

    /* Global time counter alarm */
    if (reason.alarm) {
        /* Perform scheduled task here. */
        (void)reason.alarm;
    }

    /* ---------------------------------------------------------------------- */
    /* 7. Push runtime config changes (if any handler modified the shadow).   */
    /* ---------------------------------------------------------------------- */
    if (config_changed) {
        NPZ2100_ShadowFlush(&hnpz);
    }

    /* ---------------------------------------------------------------------- */
    /* 8. Re-enter idle.  nPZ2100 will cut STM32 power.                       */
    /*    This is the last instruction that executes.                          */
    /* ---------------------------------------------------------------------- */
    NPZ2100_EnterIdle(&hnpz);

    /*
     * Reaching here means the I²C write to IDLE_RST failed.
     * Spin and wait for the nPZ2100 watchdog to power-cycle the system.
     */
    while (1) {
        HAL_Delay(1000);
    }

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
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

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USART2|RCC_PERIPHCLK_I2C1;
  PeriphClkInit.Usart2ClockSelection = RCC_USART2CLKSOURCE_HSI;
  PeriphClkInit.I2c1ClockSelection = RCC_I2C1CLKSOURCE_HSI;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
    __disable_irq();
    while (1) { }
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
    (void)file; (void)line;
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
