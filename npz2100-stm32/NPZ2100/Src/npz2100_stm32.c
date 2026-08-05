/**
 * @file npz2100_stm32.c
 * @brief nPZ2100 STM32 HAL port implementation.
 *
 * This is the ONLY file that includes STM32 HAL headers.
 * npz2100.c and npz2100_mid.c remain strictly platform-agnostic.
 *
 * Printf / UART logging
 * ---------------------
 * Logging is controlled by NPZ2100_LOG_ENABLE (default: enabled).
 * To disable all printf output, define NPZ2100_LOG_ENABLE=0 in your
 * project preprocessor symbols:
 *   Project -> Properties -> C/C++ Build -> Settings ->
 *   MCU GCC Compiler -> Preprocessor -> Defined symbols:
 *   NPZ2100_LOG_ENABLE=0
 *
 * printf is retargeted to UART via NPZ2100_UartInit() which must be
 * called once before any driver function, typically right after
 * MX_USARTx_UART_Init() in main():
 *
 *   MX_USART2_UART_Init();      // CubeMX-generated
 *   NPZ2100_UartInit(&huart2);  // retarget printf to USART2
 *   NPZ2100_Init(&hnpz, &hi2c1);
 *
 * The __io_putchar() weak override routes every putchar() call
 * (used internally by printf) through the selected UART handle.
 * This is the standard STM32CubeIDE UART printf retarget pattern.
 *
 * STM32L053 I2C specifics
 * -----------------------
 * STM32 HAL expects the 7-bit I2C address left-shifted by 1.
 * NPZ2100_I2C_ADDR (0x6F) becomes 0xDE for HAL calls.
 *
 * HAL_I2C_Mem_Read() issues the register-pointer write + data read
 * in one call with a repeated START between them, which is exactly
 * what the nPZ2100 expects for register reads.
 *
 * @version 0.8
 * @date    2026-05-06
 * @author  Nanopower Semiconductor AS
 */

#include "npz2100_stm32.h"
#include <stdio.h>    /* printf — retargeted to UART via __io_putchar */
#include <string.h>   /* memset */

/* =========================================================================
 * Logging control
 * ======================================================================= */

#ifndef NPZ2100_LOG_ENABLE
  #define NPZ2100_LOG_ENABLE  1   /**< Set to 0 to strip all printf output. */
#endif

#if NPZ2100_LOG_ENABLE
  #define NPZ_LOG(fmt, ...)  printf("[NPZ2100] " fmt "\r\n", ##__VA_ARGS__)
#else
  #define NPZ_LOG(fmt, ...)  do { } while (0)
#endif

/* =========================================================================
 * UART printf retarget
 * ======================================================================= */

/** Handle to the UART used for printf output.
 *  Set by NPZ2100_UartInit(). NULL = printf disabled. */
static UART_HandleTypeDef *s_huart = NULL;

/**
 * @brief Point printf output at a UART peripheral.
 *
 * Call once after MX_USARTx_UART_Init(), before any driver function:
 * @code
 *   NPZ2100_UartInit(&huart2);
 * @endcode
 *
 * Pass NULL to disable printf output at runtime.
 *
 * @param huart  Pointer to an initialised UART_HandleTypeDef.
 */
void NPZ2100_UartInit(UART_HandleTypeDef *huart)
{
    s_huart = huart;
}

/**
 * @brief Override the weak __io_putchar() to route printf through UART.
 *
 * The STM32CubeIDE newlib-nano runtime calls __io_putchar() for every
 * character emitted by printf/puts.  This override sends each character
 * via HAL_UART_Transmit() in blocking mode (1 ms timeout per byte).
 *
 * Note: this override is global — it affects ALL printf calls in the
 * application, not just those inside this driver.  If your project
 * already has a __io_putchar() override, remove this one and ensure
 * your version is compatible.
 */
int __io_putchar(int ch)
{
    if (s_huart != NULL) {
        uint8_t byte = (uint8_t)ch;
        HAL_UART_Transmit(s_huart, &byte, 1u, 1u);
    }
    return ch;
}

/* =========================================================================
 * STM32 HAL I2C callbacks
 * ======================================================================= */

/**
 * @brief HAL write callback — wraps HAL_I2C_Master_Transmit().
 *
 * buf[0] = register address prepended by the core driver.
 * buf[1..len-1] = data payload.
 * Issues one START/STOP for the whole buffer — single I2C transaction.
 */
static npz2100_err_t stm32_i2c_write(uint8_t        i2c_addr,
                                      const uint8_t *buf,
                                      size_t         len,
                                      void          *ctx)
{
    I2C_HandleTypeDef *hi2c = (I2C_HandleTypeDef *)ctx;

    HAL_StatusTypeDef ret = HAL_I2C_Master_Transmit(
        hi2c,
        (uint16_t)(i2c_addr << 1u),
        (uint8_t *)buf,    /* HAL prototype is non-const; buffer not modified */
        (uint16_t)len,
        NPZ2100_I2C_TIMEOUT_MS);

    if (ret != HAL_OK) {
        NPZ_LOG("I2C WRITE FAIL  reg=0x%02X len=%u HAL=%d",
                buf[0], (unsigned)len, (int)ret);
        return NPZ2100_ERR_IO;
    }

#if NPZ2100_LOG_ENABLE
//    /* Log the register and each data byte written. */
//    printf("[NPZ2100] WR reg=0x%02X data=", buf[0]);
//    for (size_t i = 1u; i < len; i++) {
//        printf("0x%02X", buf[i]);
//        if (i < len - 1u) { printf(" "); }
//    }
//    printf("\r\n");
#endif

    return NPZ2100_OK;
}

/**
 * @brief HAL read callback — wraps HAL_I2C_Mem_Read().
 *
 * Issues: START addr+W, reg, rSTART, addr+R, buf[0..len-1], STOP.
 */
static npz2100_err_t stm32_i2c_read(uint8_t  i2c_addr,
                                     uint8_t  reg,
                                     uint8_t *buf,
                                     size_t   len,
                                     void    *ctx)
{
    I2C_HandleTypeDef *hi2c = (I2C_HandleTypeDef *)ctx;

    HAL_StatusTypeDef ret = HAL_I2C_Mem_Read(
        hi2c,
        (uint16_t)(i2c_addr << 1u),
        (uint16_t)reg,
        I2C_MEMADD_SIZE_8BIT,
        buf,
        (uint16_t)len,
        NPZ2100_I2C_TIMEOUT_MS);

    if (ret != HAL_OK) {
        NPZ_LOG("I2C READ  FAIL  reg=0x%02X len=%u HAL=%d",
                reg, (unsigned)len, (int)ret);
        return NPZ2100_ERR_IO;
    }

#if NPZ2100_LOG_ENABLE
//    printf("[NPZ2100] RD reg=0x%02X data=", reg);
//    for (size_t i = 0u; i < len; i++) {
//        printf("0x%02X", buf[i]);
//        if (i < len - 1u) { printf(" "); }
//    }
//    printf("\r\n");
#endif

    return NPZ2100_OK;
}

/* =========================================================================
 * Internal helpers
 * ======================================================================= */

static inline NPZ2100_Status_t mid_to_status(npz2100_err_t e)
{
    return (NPZ2100_Status_t)e;
}

/* =========================================================================
 * NPZ2100_Init
 * ======================================================================= */

NPZ2100_Status_t NPZ2100_Init(NPZ2100_Handle_t  *hnpz,
                               I2C_HandleTypeDef *hi2c)
{
    if (hnpz == NULL || hi2c == NULL) {
        NPZ_LOG("Init ERROR: NULL handle");
        return NPZ2100_ERR_ARG;
    }

    hnpz->hi2c         = hi2c;
    hnpz->hal.write    = stm32_i2c_write;
    hnpz->hal.read     = stm32_i2c_read;
    hnpz->hal.i2c_addr = NPZ2100_I2C_ADDR;
    hnpz->hal.ctx      = (void *)hi2c;

    /* Seed shadow with power-on reset defaults — no I2C transaction. */
    npz2100_err_t err = npz2100_config_init_defaults(&hnpz->shadow);
    if (err != NPZ2100_OK) {
        NPZ_LOG("Init ERROR: shadow init failed (%d)", (int)err);
        return mid_to_status(err);
    }

    /* Probe device — ID register must equal 0x74. */
    NPZ_LOG("Init: probing I2C addr=0x%02X ...", NPZ2100_I2C_ADDR);
    err = npz2100_probe_ll(&hnpz->hal);
    if (err != NPZ2100_OK) {
        NPZ_LOG("Init ERROR: device not found (err=%d) - check wiring", (int)err);
        return mid_to_status(err);
    }

    NPZ_LOG("Init OK: nPZ2100 found at I2C 0x%02X", NPZ2100_I2C_ADDR);
    return NPZ2100_OK;
}

/* =========================================================================
 * NPZ2100_BootStatus
 * ======================================================================= */

NPZ2100_Status_t NPZ2100_BootStatus(NPZ2100_Handle_t    *hnpz,
                                     NPZ2100_WakeReason_t *reason)
{
    if (hnpz == NULL) {
        return NPZ2100_ERR_ARG;
    }

    uint8_t sta1, sta2, sta3;

    /* Single burst read — STA1/STA2 read also resets the watchdog. */
    npz2100_err_t err = npz2100_status_read(&hnpz->hal, &sta1, &sta2, &sta3);
    if (err != NPZ2100_OK) {
        NPZ_LOG("BootStatus ERROR: STA read failed (%d)", (int)err);
        return mid_to_status(err);
    }

    NPZ_LOG("BootStatus: STA1=0x%02X STA2=0x%02X STA3=0x%02X", sta1, sta2, sta3);

    if (reason == NULL) {
        return NPZ2100_OK;
    }

    memset(reason, 0, sizeof(*reason));

    reason->rst_src     = NPZ2100_STA1_RST_SRC_GET(sta1);
    reason->srst_src    = NPZ2100_STA1_SRST_SRC_GET(sta1);
    reason->adc1        = (sta1 & NPZ2100_STA1_FADC1_MSK) ? 1u : 0u;
    reason->adc2        = (sta1 & NPZ2100_STA1_FADC2_MSK) ? 1u : 0u;
    reason->adc3        = (sta1 & NPZ2100_STA1_FADC3_MSK) ? 1u : 0u;
    reason->timeout     = (sta1 & NPZ2100_STA1_FTOUT_MSK) ? 1u : 0u;
    reason->periph_mask = sta2 & 0x3Fu;
    reason->alarm       = (sta2 & NPZ2100_STA2_FALM_MSK) ? 1u : 0u;
    reason->log_full    = (sta2 & NPZ2100_STA2_FLOG_MSK) ? 1u : 0u;
    reason->counter     = (sta3 & NPZ2100_STA3_FCNT_MSK) ? 1u : 0u;
    reason->pa_active   = (sta3 & NPZ2100_STA3_FPA_MSK)  ? 1u : 0u;
    reason->nak_mask    = sta3 & 0x3Fu;

    /* Decode and print reset source. */
    static const char * const rst_names[] = {
        "Power-on reset", "External NRST pin", "I2C command", "Brown-out reset"
    };
    NPZ_LOG("BootStatus: reset_src=%s (0x%02X)",
            rst_names[reason->rst_src & 0x03u], reason->rst_src);

    /* Print every active wake flag. */
    if (reason->periph_mask) {
        for (int i = 0; i < 6; i++) {
            if (reason->periph_mask & (1u << i)) {
                NPZ_LOG("BootStatus: WAKE peripheral %d threshold crossed", i + 1);
            }
        }
    }
    if (reason->adc1)      { NPZ_LOG("BootStatus: WAKE ADC1 threshold crossed"); }
    if (reason->adc2)      { NPZ_LOG("BootStatus: WAKE ADC2 threshold crossed"); }
    if (reason->adc3)      { NPZ_LOG("BootStatus: WAKE ADC3 (battery) threshold crossed"); }
    if (reason->timeout)   { NPZ_LOG("BootStatus: WAKE periodic time-out"); }
    if (reason->alarm)     { NPZ_LOG("BootStatus: WAKE global time counter alarm"); }
    if (reason->log_full)  { NPZ_LOG("BootStatus: WAKE SRAM log area full"); }
    if (reason->counter)   { NPZ_LOG("BootStatus: WAKE event counter triggered"); }
    if (reason->pa_active) { NPZ_LOG("BootStatus: power-aware mode is active"); }
    if (reason->nak_mask) {
        for (int i = 0; i < 6; i++) {
            if (reason->nak_mask & (1u << i)) {
                NPZ_LOG("BootStatus: peripheral %d NAK'd I2C", i + 1);
            }
        }
    }

    return NPZ2100_OK;
}

/* =========================================================================
 * NPZ2100_Readback
 * ======================================================================= */

NPZ2100_Status_t NPZ2100_Readback(NPZ2100_Handle_t *hnpz)
{
    if (hnpz == NULL) {
        return NPZ2100_ERR_ARG;
    }

    NPZ_LOG("Readback: syncing shadow from device ...");
    npz2100_err_t err = npz2100_map_readback(&hnpz->hal, &hnpz->shadow);
    if (err != NPZ2100_OK) {
        NPZ_LOG("Readback ERROR: failed (%d)", (int)err);
    } else {
        NPZ_LOG("Readback OK: shadow synced");
    }
    return mid_to_status(err);
}

/* =========================================================================
 * NPZ2100_ApplyRegmap
 * ======================================================================= */

NPZ2100_Status_t NPZ2100_ApplyRegmap(NPZ2100_Handle_t *hnpz,
                                      const uint8_t    *map,
                                      size_t            map_len)
{
    if (hnpz == NULL || map == NULL || map_len == 0u) {
        NPZ_LOG("ApplyRegmap ERROR: invalid arguments");
        return NPZ2100_ERR_ARG;
    }

    if (npz2100_map_validate(map, map_len) != NPZ2100_OK) {
        NPZ_LOG("ApplyRegmap ERROR: malformed byte stream (bad length field)");
        return NPZ2100_ERR_ARG;
    }

    /* Count how many registers differ before writing. */
    uint8_t ndiff = npz2100_map_diff_count(&hnpz->shadow, map, map_len);
    NPZ_LOG("ApplyRegmap: %u register(s) differ from shadow", (unsigned)ndiff);

    if (ndiff == 0u) {
        NPZ_LOG("ApplyRegmap: device already in sync - no writes issued");
        return NPZ2100_OK;
    }

    npz2100_err_t err = npz2100_map_apply(&hnpz->hal, &hnpz->shadow, map, map_len);
    if (err != NPZ2100_OK) {
        NPZ_LOG("ApplyRegmap ERROR: apply failed (%d)", (int)err);
    } else {
        NPZ_LOG("ApplyRegmap OK: %u register(s) written", (unsigned)ndiff);
    }
    return mid_to_status(err);
}

/* =========================================================================
 * NPZ2100_GetShadow
 * ======================================================================= */

npz2100_config_t *NPZ2100_GetShadow(NPZ2100_Handle_t *hnpz)
{
    if (hnpz == NULL) {
        return NULL;
    }
    return &hnpz->shadow;
}

/* =========================================================================
 * NPZ2100_ShadowFlush
 * ======================================================================= */

NPZ2100_Status_t NPZ2100_ShadowFlush(NPZ2100_Handle_t *hnpz)
{
    if (hnpz == NULL) {
        return NPZ2100_ERR_ARG;
    }

    NPZ_LOG("ShadowFlush: pushing shadow changes to device ...");

    npz2100_err_t err = NPZ2100_OK;

    struct { uint8_t addr; uint8_t *field; } regs[] = {
        { NPZ2100_REG_IOCFG1,    &hnpz->shadow.iocfg1     },
        { NPZ2100_REG_IOCFG2,    &hnpz->shadow.iocfg2     },
        { NPZ2100_REG_IOCFG3,    &hnpz->shadow.iocfg3     },
        { NPZ2100_REG_IOCFG4,    &hnpz->shadow.iocfg4     },
        { NPZ2100_REG_IOCFG5,    &hnpz->shadow.iocfg5     },
        { NPZ2100_REG_SYSCFG1,   &hnpz->shadow.syscfg1    },
        { NPZ2100_REG_SYSCFG2,   &hnpz->shadow.syscfg2    },
        { NPZ2100_REG_TOUT_L,    &hnpz->shadow.tout_l     },
        { NPZ2100_REG_TOUT_H,    &hnpz->shadow.tout_h     },
        { NPZ2100_REG_GCT_MS,    &hnpz->shadow.gct_ms     },
        { NPZ2100_REG_GCT_0,     &hnpz->shadow.gct_0      },
        { NPZ2100_REG_GCT_1,     &hnpz->shadow.gct_1      },
        { NPZ2100_REG_GCT_2,     &hnpz->shadow.gct_2      },
        { NPZ2100_REG_GCT_3,     &hnpz->shadow.gct_3      },
        { NPZ2100_REG_GCT_ALM_0, &hnpz->shadow.gct_alm_0  },
        { NPZ2100_REG_GCT_ALM_1, &hnpz->shadow.gct_alm_1  },
        { NPZ2100_REG_GCT_ALM_2, &hnpz->shadow.gct_alm_2  },
        { NPZ2100_REG_GCT_ALM_3, &hnpz->shadow.gct_alm_3  },
        { NPZ2100_REG_WDOG_L,    &hnpz->shadow.wdog_l     },
        { NPZ2100_REG_WDOG_H,    &hnpz->shadow.wdog_h     },
        { NPZ2100_REG_GTC_CFG,   &hnpz->shadow.gtc_cfg    },
        { NPZ2100_REG_PA_CFG,    &hnpz->shadow.pa_cfg     },
        { NPZ2100_REG_ADCCFG,    &hnpz->shadow.adccfg     },
        { NPZ2100_REG_THROVA1,   &hnpz->shadow.throva1    },
        { NPZ2100_REG_THRUNA1,   &hnpz->shadow.thruna1    },
        { NPZ2100_REG_THROVA2,   &hnpz->shadow.throva2    },
        { NPZ2100_REG_THRUNA2,   &hnpz->shadow.thruna2    },
        { NPZ2100_REG_THROVA3,   &hnpz->shadow.throva3    },
        { NPZ2100_REG_THRUNA3,   &hnpz->shadow.thruna3    },
        { NPZ2100_REG_LOGCFG,    &hnpz->shadow.logcfg     },
        { NPZ2100_REG_LOGSADDR,  &hnpz->shadow.logsaddr   },
        { NPZ2100_REG_CNTCFG,    &hnpz->shadow.cntcfg     },
        { NPZ2100_REG_CNT_TRIG_0,&hnpz->shadow.cnt_trig_0 },
        { NPZ2100_REG_CNT_TRIG_1,&hnpz->shadow.cnt_trig_1 },
        { NPZ2100_REG_CNT_TRIG_2,&hnpz->shadow.cnt_trig_2 },
        { NPZ2100_REG_CNT_TRIG_3,&hnpz->shadow.cnt_trig_3 },
        { NPZ2100_REG_SRAM_BANK, &hnpz->shadow.sram_bank  },
    };

    for (size_t i = 0u; i < sizeof(regs)/sizeof(regs[0]) && err == NPZ2100_OK; i++) {
        err = npz2100_shadow_write_reg(&hnpz->hal, &hnpz->shadow,
                                       regs[i].addr, *regs[i].field);
    }

    for (uint8_t slot = 0u;
         slot <= NPZ2100_P_BANK_MAX && err == NPZ2100_OK;
         slot++) {
        err = npz2100_periph_apply(&hnpz->hal, &hnpz->shadow, slot);
    }

    if (err != NPZ2100_OK) {
        NPZ_LOG("ShadowFlush ERROR: failed (%d)", (int)err);
    } else {
        NPZ_LOG("ShadowFlush OK");
    }
    return mid_to_status(err);
}

/* =========================================================================
 * SRAM access
 * ======================================================================= */

NPZ2100_Status_t NPZ2100_SramWrite(NPZ2100_Handle_t *hnpz,
                                    uint8_t           sram_addr,
                                    const uint8_t    *data,
                                    size_t            len)
{
    if (hnpz == NULL) {
        return NPZ2100_ERR_ARG;
    }
    NPZ_LOG("SramWrite: addr=0x%02X len=%u", sram_addr, (unsigned)len);
    npz2100_err_t err = npz2100_sram_write_ll(&hnpz->hal, &hnpz->shadow,
                                               sram_addr, data, len);
    if (err != NPZ2100_OK) {
        NPZ_LOG("SramWrite ERROR (%d)", (int)err);
    }
    return mid_to_status(err);
}

NPZ2100_Status_t NPZ2100_SramRead(NPZ2100_Handle_t *hnpz,
                                   uint8_t           sram_addr,
                                   uint8_t          *data,
                                   size_t            len)
{
    if (hnpz == NULL) {
        return NPZ2100_ERR_ARG;
    }
    NPZ_LOG("SramRead: addr=0x%02X len=%u", sram_addr, (unsigned)len);
    npz2100_err_t err = npz2100_sram_read_ll(&hnpz->hal, &hnpz->shadow,
                                              sram_addr, data, len);
    if (err != NPZ2100_OK) {
        NPZ_LOG("SramRead ERROR (%d)", (int)err);
    }
    return mid_to_status(err);
}

NPZ2100_Status_t NPZ2100_PeriphReadValue(NPZ2100_Handle_t *hnpz,
                                          uint8_t           slot,
                                          uint16_t         *value)
{
    if (hnpz == NULL || value == NULL || slot > NPZ2100_P_BANK_MAX) {
        return NPZ2100_ERR_ARG;
    }
    npz2100_err_t err = npz2100_periph_read_value_ll(&hnpz->hal, &hnpz->shadow,
                                                      slot, value);
    if (err != NPZ2100_OK) {
        NPZ_LOG("PeriphReadValue ERROR: slot=%u err=%d", (unsigned)slot, (int)err);
    } else {
        NPZ_LOG("PeriphReadValue: slot=%u value=0x%04X (%u)",
                (unsigned)slot, *value, *value);
    }
    return mid_to_status(err);
}

/* =========================================================================
 * Control
 * ======================================================================= */

NPZ2100_Status_t NPZ2100_EnterIdle(NPZ2100_Handle_t *hnpz)
{
    if (hnpz == NULL) {
        return NPZ2100_ERR_ARG;
    }
    NPZ_LOG("EnterIdle: handing control to nPZ2100 - STM32 power will be cut");
    /*
     * After this write the nPZ2100 de-asserts SW_HP and cuts STM32 power.
     * This function does not return in normal operation.
     */
    return mid_to_status(npz2100_enter_idle_ll(&hnpz->hal));
}

NPZ2100_Status_t NPZ2100_SoftReset(NPZ2100_Handle_t *hnpz)
{
    if (hnpz == NULL) {
        return NPZ2100_ERR_ARG;
    }
    NPZ_LOG("SoftReset: issuing soft reset (config and SRAM preserved)");
    return mid_to_status(npz2100_soft_reset_ll(&hnpz->hal));
}
