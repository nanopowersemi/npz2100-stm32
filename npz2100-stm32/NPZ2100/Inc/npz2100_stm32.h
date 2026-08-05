/**
 * @file npz2100_stm32.h
 * @brief nPZ2100 driver port for STM32L053 / STM32CubeIDE 1.18.1 / HAL.
 *
 * Power architecture
 * ------------------
 * The nPZ2100 controls the STM32L053 power supply via its SW_HP host switch.
 * When idle, the nPZ2100 cuts power to the STM32 completely — there is no
 * always-on state, no RTOS, no interrupt handler waiting.
 *
 * Every STM32 boot is a fresh start caused by the nPZ2100 re-asserting SW_HP
 * in response to a configured trigger (sensor threshold, ADC limit, alarm…).
 *
 * Required boot sequence — call from main(), before any application logic,
 * after HAL_Init() and SystemClock_Config() and MX_I2C1_Init():
 *
 *   NPZ2100_Handle_t npz;
 *   NPZ2100_Init(&npz, &hi2c1);
 *
 *   NPZ2100_WakeReason_t reason;
 *   NPZ2100_BootStatus(&npz, &reason);   // read STA1-3, kick watchdog
 *
 *   NPZ2100_Readback(&npz);              // sync shadow from device
 *   NPZ2100_ApplyRegmap(&npz, regmap, sizeof(regmap)); // write only changes
 *
 *   // ... application logic ...
 *   NPZ2100_ShadowFlush(&npz);           // push runtime config changes
 *   NPZ2100_EnterIdle(&npz);             // STM32 power cut — does not return
 *
 * Integration into STM32CubeIDE
 * ------------------------------
 * 1. Add NPZ2100/Src/ .c files to the project build (Project -> Properties ->
 *    C/C++ Build -> Settings -> Source Location).
 *    Add NPZ2100/Inc to compiler include paths.
 * 2. Call NPZ2100_Init() after MX_I2C1_Init() in main.c.
 * 3. CubeMX will not overwrite NPZ2100/ — it only touches Core/ and Drivers/.
 *
 * I²C pins: PC4 = SDA, PC5 = SCL  (I2C1, configured in CubeMX)
 * I²C speed: 100 kHz (Standard Mode — matches nPZ2100 max spec)
 * I²C address: 0x6F (7-bit, factory default)
 *
 * @version 0.7
 * @date    2026-05-06
 * @author  Nanopower Semiconductor AS
 */

#ifndef NPZ2100_STM32_H_
#define NPZ2100_STM32_H_

/* STM32CubeL0 HAL — included first so uint8_t etc. are resolved. */
#include "stm32l0xx_hal.h"

/* Platform-agnostic driver layers. */
#include "npz2100_mid.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Constants
 * ======================================================================= */

/** Default 7-bit I²C address of the nPZ2100. */
#define NPZ2100_I2C_ADDR        (0x3Cu)

/** HAL I²C timeout for each transaction (ms). */
#define NPZ2100_I2C_TIMEOUT_MS  (10u)

/* =========================================================================
 * Handle
 * ======================================================================= */

/**
 * @brief nPZ2100 driver handle.
 *
 * Declare one of these per nPZ2100 device, typically as a local variable
 * in main() or a file-scope static.  Initialise with NPZ2100_Init() before
 * calling any other function.
 *
 * The handle is not thread-safe — if using an RTOS, protect with a mutex.
 */
typedef struct {
    npz2100_hal_t    hal;      /**< Platform-agnostic HAL callbacks + address. */
    npz2100_config_t shadow;   /**< Register shadow — mirrors last known device state. */
    I2C_HandleTypeDef *hi2c;   /**< Pointer to the CubeMX-generated I²C handle. */
} NPZ2100_Handle_t;

/* =========================================================================
 * Wake-up reason struct
 * ======================================================================= */

/**
 * @brief Decoded nPZ2100 wake-up reason, populated by NPZ2100_BootStatus().
 *
 * Multiple flags may be set simultaneously.
 * Reading STA1/STA2 via NPZ2100_BootStatus() also resets the watchdog timer.
 */
typedef struct {
    /* Reset sources (STA1) */
    uint8_t rst_src;        /**< System reset source — use NPZ2100_RST_SRC_* */
    uint8_t srst_src;       /**< Soft reset source   — use NPZ2100_SRST_SRC_* */

    /* Peripheral trigger flags (STA2 bits 5:0) */
    uint8_t periph_mask;    /**< Bitmask: bit N-1 set if peripheral N triggered */

    /* ADC and timing flags (STA1) */
    uint8_t adc1     : 1;   /**< ADC channel 1 threshold crossed */
    uint8_t adc2     : 1;   /**< ADC channel 2 threshold crossed */
    uint8_t adc3     : 1;   /**< ADC channel 3 (battery) threshold crossed */
    uint8_t timeout  : 1;   /**< Periodic time-out (no other source first) */

    /* Event flags (STA2) */
    uint8_t alarm    : 1;   /**< Global time counter alarm fired */
    uint8_t log_full : 1;   /**< SRAM logging area full */

    /* Event counter and power-aware (STA3) */
    uint8_t counter  : 1;   /**< Event counter reached trigger value */
    uint8_t pa_active: 1;   /**< Power-aware mode is active */

    /* NAK flags (STA3 bits 5:0) */
    uint8_t nak_mask;       /**< Bitmask: bit N-1 set if peripheral N NAK'd */
} NPZ2100_WakeReason_t;

/* =========================================================================
 * Return type
 * ======================================================================= */

/**
 * @brief Return type for all NPZ2100 STM32 API functions.
 *
 * Aliased from npz2100_err_t (defined in npz2100_hal.h) so application code
 * uses the familiar STM32 PascalCase style without redefining the enumerators.
 *
 * Values:
 *   NPZ2100_OK          =  0   Success.
 *   NPZ2100_ERR_IO      = -1   I2C bus error (HAL_ERROR or HAL_TIMEOUT).
 *   NPZ2100_ERR_ARG     = -2   Invalid argument.
 *   NPZ2100_ERR_DEV     = -3   Device not found / ID mismatch.
 *   NPZ2100_ERR_TIMEOUT = -4   Operation timed out.
 *   NPZ2100_ERR_STATE   = -5   Device in wrong state for this operation.
 */
typedef npz2100_err_t NPZ2100_Status_t;

/* =========================================================================
 * Boot-time API
 * ======================================================================= */

/**
 * @brief Initialise the nPZ2100 driver handle.
 *
 * Populates the HAL callbacks with STM32 HAL I²C wrappers, seeds the
 * shadow with power-on reset defaults, and verifies the device is present
 * by reading the ID register (expected value: 0x74).
 *
 * Call after HAL_Init(), SystemClock_Config(), and MX_I2C1_Init().
 *
 * @param[out] hnpz   Pointer to an uninitialised NPZ2100_Handle_t.
 * @param[in]  hi2c   Pointer to the CubeMX I²C handle (e.g. &hi2c1).
 * @return NPZ2100_OK, NPZ2100_ERR_IO on bus error, NPZ2100_ERR_DEV if
 *         ID register does not read 0x74.
 */
NPZ2100_Status_t NPZ2100_Init(NPZ2100_Handle_t  *hnpz,
                               I2C_HandleTypeDef *hi2c);

/**
 * @brief Read wake-up reason from STA1–STA3.  CALL FIRST ON EVERY BOOT.
 *
 * This must be the first nPZ2100 I²C operation after the STM32 boots.
 * Reading STA1/STA2 simultaneously resets the nPZ2100 watchdog timer.
 * All three registers are read in a single burst (one I²C transaction).
 *
 * @param[in]  hnpz    Pointer to initialised NPZ2100_Handle_t.
 * @param[out] reason  Decoded wake-up flags.  Pass NULL to only kick
 *                     the watchdog without decoding the reason.
 * @return NPZ2100_OK or NPZ2100_ERR_IO.
 */
NPZ2100_Status_t NPZ2100_BootStatus(NPZ2100_Handle_t    *hnpz,
                                     NPZ2100_WakeReason_t *reason);

/**
 * @brief Sync the driver shadow from the device's current register state.
 *
 * The nPZ2100 retains all register values while the STM32 is powered off.
 * Call this after NPZ2100_BootStatus() so the shadow reflects actual device
 * state before NPZ2100_ApplyRegmap() computes its diff.
 *
 * @param[in] hnpz  Pointer to initialised NPZ2100_Handle_t.
 * @return NPZ2100_OK or NPZ2100_ERR_IO.
 */
NPZ2100_Status_t NPZ2100_Readback(NPZ2100_Handle_t *hnpz);

/* =========================================================================
 * Configuration API
 * ======================================================================= */

/**
 * @brief Apply a tool-generated byte-stream register map to the device.
 *
 * Parses the flat `[length][start_addr][data...]` stream and writes only
 * registers that differ from the shadow.  On a typical warm boot where
 * nothing changed, zero I²C transactions are issued.
 *
 * @param[in] hnpz     Pointer to initialised NPZ2100_Handle_t.
 * @param[in] map      Byte-stream from the Nanopower configuration tool.
 * @param[in] map_len  Total length in bytes — use sizeof() for compile-time arrays.
 * @return NPZ2100_OK, NPZ2100_ERR_ARG on malformed stream, NPZ2100_ERR_IO
 *         on I²C error.
 */
NPZ2100_Status_t NPZ2100_ApplyRegmap(NPZ2100_Handle_t *hnpz,
                                      const uint8_t    *map,
                                      size_t            map_len);

/**
 * @brief Return a pointer to the driver shadow for use with typed helpers.
 *
 * Use with the mid-level typed helpers (npz2100_sys_set, npz2100_periph_set…)
 * to modify configuration, then call NPZ2100_ShadowFlush() to push to device.
 *
 * @param[in] hnpz  Pointer to initialised NPZ2100_Handle_t.
 * @return Pointer to the internal npz2100_config_t shadow.
 */
npz2100_config_t *NPZ2100_GetShadow(NPZ2100_Handle_t *hnpz);

/**
 * @brief Push shadow changes to the device — write only changed registers.
 *
 * @param[in] hnpz  Pointer to initialised NPZ2100_Handle_t.
 * @return NPZ2100_OK or NPZ2100_ERR_IO.
 */
NPZ2100_Status_t NPZ2100_ShadowFlush(NPZ2100_Handle_t *hnpz);

/* =========================================================================
 * SRAM access
 * ======================================================================= */

/**
 * @brief Write sensor initialisation commands into the nPZ2100 SRAM.
 *
 * The nPZ2100 sends these autonomously to sensors during polling while
 * the STM32 is powered off.  Handles the 128-byte bank boundary automatically.
 *
 * @param[in] hnpz       Pointer to initialised NPZ2100_Handle_t.
 * @param[in] sram_addr  SRAM byte offset (0x00–0xFF).
 * @param[in] data       Source data.
 * @param[in] len        Number of bytes to write.
 * @return NPZ2100_OK, NPZ2100_ERR_ARG on out-of-bounds, NPZ2100_ERR_IO.
 */
NPZ2100_Status_t NPZ2100_SramWrite(NPZ2100_Handle_t *hnpz,
                                    uint8_t           sram_addr,
                                    const uint8_t    *data,
                                    size_t            len);

/**
 * @brief Read from the nPZ2100 SRAM.
 *
 * @param[in]  hnpz       Pointer to initialised NPZ2100_Handle_t.
 * @param[in]  sram_addr  SRAM byte offset (0x00–0xFF).
 * @param[out] data       Destination buffer.
 * @param[in]  len        Number of bytes to read.
 * @return NPZ2100_OK, NPZ2100_ERR_ARG, or NPZ2100_ERR_IO.
 */
NPZ2100_Status_t NPZ2100_SramRead(NPZ2100_Handle_t *hnpz,
                                   uint8_t           sram_addr,
                                   uint8_t          *data,
                                   size_t            len);

/**
 * @brief Read the last sampled value for a peripheral slot.
 *
 * @param[in]  hnpz   Pointer to initialised NPZ2100_Handle_t.
 * @param[in]  slot   Peripheral index 0–5.
 * @param[out] value  Raw value (8 or 16-bit per DTYPE setting).
 * @return NPZ2100_OK, NPZ2100_ERR_ARG, or NPZ2100_ERR_IO.
 */
NPZ2100_Status_t NPZ2100_PeriphReadValue(NPZ2100_Handle_t *hnpz,
                                          uint8_t           slot,
                                          uint16_t         *value);

/* =========================================================================
 * Control
 * ======================================================================= */

/**
 * @brief Hand control to the nPZ2100.  STM32 power will be cut.
 *
 * Writes 0xFF to IDLE_RST.  The nPZ2100 will de-assert SW_HP, cutting
 * power to the STM32 completely.  This function does not return in normal
 * operation — the STM32 loses power immediately after the I²C write.
 *
 * @param[in] hnpz  Pointer to initialised NPZ2100_Handle_t.
 * @return NPZ2100_ERR_IO if the I²C write itself failed (rare).
 *         Does not return on success.
 */
NPZ2100_Status_t NPZ2100_EnterIdle(NPZ2100_Handle_t *hnpz);

/**
 * @brief Issue a soft reset (SRAM and config preserved).
 *
 * @param[in] hnpz  Pointer to initialised NPZ2100_Handle_t.
 * @return NPZ2100_OK or NPZ2100_ERR_IO.
 */
NPZ2100_Status_t NPZ2100_SoftReset(NPZ2100_Handle_t *hnpz);

/* =========================================================================
 * Logging / UART printf retarget
 * ======================================================================= */

/**
 * @brief Retarget printf to a UART peripheral.
 *
 * Call once after MX_USARTx_UART_Init() and before NPZ2100_Init():
 * @code
 *   NPZ2100_UartInit(&huart2);
 *   NPZ2100_Init(&hnpz, &hi2c1);
 * @endcode
 *
 * All printf output from the driver (and from the rest of the application)
 * is routed through this UART via a __io_putchar() override.
 *
 * Pass NULL to disable output at runtime.
 *
 * To disable logging at compile time (zero code size overhead), define
 * NPZ2100_LOG_ENABLE=0 in Project Properties -> GCC Compiler -> Defined symbols.
 *
 * @param huart  Pointer to an initialised UART_HandleTypeDef (e.g. &huart2).
 */
void NPZ2100_UartInit(UART_HandleTypeDef *huart);

#ifdef __cplusplus
}
#endif

#endif /* NPZ2100_STM32_H_ */
