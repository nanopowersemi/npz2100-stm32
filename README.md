# nPZ2100 — STM32L053C8Ux Driver Package

**Target:** STM32L053C8Ux · **IDE:** STM32CubeIDE **2.2.0** · **CubeMX:** standalone 6.18.0 · **HAL:** STM32CubeL0

---

## What is in this package

```
npz2100_stm32/
├── README.md                   ← This file — follow it step by step
│
├── NPZ2100/                    ← Driver — copy this folder into your project
│   ├── Inc/
│   │   ├── npz2100_stm32.h     ← Public API  (include in main.c)
│   │   ├── npz2100_hal.h       ← Platform-agnostic HAL interface
│   │   ├── npz2100_mid.h       ← Mid-level API (regmap, shadow, typed helpers)
│   │   ├── npz2100_regs_system.h
│   │   ├── npz2100_regs_io.h
│   │   ├── npz2100_regs_periph.h
│   │   └── npz2100_regs_adc_log.h
│   └── Src/
│       ├── npz2100_stm32.c     ← STM32 HAL port
│       ├── npz2100.c           ← Platform-agnostic HAL primitives
│       └── npz2100_mid.c       ← Platform-agnostic mid-level API
│
└── Core/                       ← Reference files to merge into your project
    ├── Inc/main.h
    └── Src/main.c              ← Full boot-sequence reference application
```

> **No `.ioc` file is included.** STM32CubeMX validates `.ioc` files with a
> strict internal parser — a hand-authored file always fails. Follow the
> CubeMX configuration steps in **Step 2** below instead; it takes under
> 2 minutes.

---

## Power architecture

```
  Battery/Supply ──► nPZ2100 ──SW_HP──► STM32L053 VDD
                        │
                    I²C1 (PC4=SDA / PC5=SCL), 100 kHz
                        │
                    STM32L053
```

The nPZ2100 controls the STM32L053 power supply via **SW_HP**.
When idle, the nPZ2100 cuts power to the STM32 completely.
Every STM32 boot was caused by the nPZ2100 re-asserting SW_HP.

---

## Prerequisites

| Tool | Version | Where to get it |
|------|---------|----------------|
| STM32CubeIDE | **2.2.0** | [st.com/stm32cubeide](https://www.st.com/stm32cubeide) |
| STM32CubeMX | **6.18.0 (standalone)** | [st.com/stm32cubemx](https://www.st.com/stm32cubemx) |
| STM32CubeL0 firmware | **V1.12.2** | CubeMX → Help → Manage embedded software packages |

> Starting with STM32CubeIDE 2.0.0, **STM32CubeMX is a separate download**.
> Install both tools independently before proceeding.

### What's new in CubeMX 6.18.0 relevant to this project

Nothing changes for the STM32L053 / I²C workflow. The additions in 6.18.0 are:
- "Start from Board" support for NUCLEO-F446RE, NUCLEO-L432KC, NUCLEO-L476RG (not used here)
- OpenSTLinux 6.2.1 support for MP1x/MP2x (not relevant for STM32L053)
- User manual and release notes now available online with improved navigation
- CAD content opens in the default browser instead of the embedded viewer

The I²C configuration UI, pin assignment workflow, STM32L053 support, and
generated HAL code structure are **identical** to previous 6.x versions.
The steps in this README are fully valid for 6.18.0.

---

## Integration — 6 steps

### Step 1 — Create a new project in STM32CubeMX

1. Open **STM32CubeMX** (standalone app).
2. Click **ACCESS TO MCU SELECTOR**.
3. In the search box type `STM32L053C8U` → select **STM32L053C8Ux** → click **Start Project**.

---

### Step 2 — Configure peripherals

#### Clock (RCC tab)

| Setting | Value |
|---------|-------|
| High Speed Clock (HSE) | Disable |
| Low Speed Clock (LSE) | Disable |

Go to **Clock Configuration** tab:
- Set **SYSCLK** source to **HSI** → 16 MHz
- Leave AHB, APB1, APB2 prescalers at `/1`
- Set **I2C1 Clock Mux** → **HSI**

#### I²C1 (Connectivity → I2C1)

| Setting | Value |
|---------|-------|
| Mode | **I2C** |
| Speed Mode | Standard Mode |
| Speed Frequency (kHz) | **100** |
| Rise Time (ns) | 1000 |
| Fall Time (ns) | 300 |

> **STM32L0 note:** The STM32L0 I²C peripheral uses a 32-bit `Timing`
> register, not `ClockSpeed`/`DutyCycle` (those are STM32F1/F4 fields).
> CubeMX calculates the `Timing` value automatically from the speed and
> rise/fall times you enter. The generated value for HSI 16 MHz / 100 kHz
> / 1000 ns rise / 300 ns fall is `0x00707CBB` — this is already in the
> provided `main.c`.

After enabling I2C1, CubeMX will try to assign the default I²C1 pins
(PB6/PB7). **Remap them to PC4/PC5:**

In the **Pinout view**:
1. Click pin **PB6** → select `Reset_State` (deassign).
2. Click pin **PB7** → select `Reset_State` (deassign).
3. Click pin **PC4** → select `I2C1_SDA`.
4. Click pin **PC5** → select `I2C1_SCL`.

Verify in the **Configuration** panel that I2C1 shows SDA=PC4, SCL=PC5.

#### SWD (for debugging)

Should already be assigned:
- PA13 → SYS_SWDIO
- PA14 → SYS_SWCLK

If not: **System Core → SYS → Debug → Serial Wire**.

---

### Step 3 — Configure the project settings

Go to the **Project Manager** tab:

| Field | Value |
|-------|-------|
| Project Name | `npz2100_sample` (or your choice) |
| Project Location | Your workspace folder |
| Toolchain / IDE | **STM32CubeIDE** |
| Minimum Heap Size | `0x200` |
| Minimum Stack Size | `0x400` |

Under **Code Generator**:
- ☑ Copy only the necessary library files
- ☑ Generate peripheral initialization as a pair of `.c/.h` files per peripheral

Click **GENERATE CODE** → click **Open Project** (or open in CubeIDE manually).

---

### Step 4 — Add the NPZ2100 driver to the generated project

1. **Copy** the `NPZ2100/` folder from this package into the generated project
   root (next to `Core/` and `Drivers/`).

2. In **STM32CubeIDE 2.2.0**, right-click the project in Project Explorer
   → **Refresh** (or press `F5`).

3. Add the **include path**:
   `Project → Properties → C/C++ Build → Settings →`
   `MCU GCC Compiler → Include paths → ➕ Add`:
   ```
   ../NPZ2100/Inc
   ```

4. Add the **source folder**:
   `Project → Properties → C/C++ Build → Settings →`
   `MCU GCC Compiler → Source Location → ➕ Add Folder`:
   Select `NPZ2100/Src`.

5. Click **Apply and Close** → **Build** (`Ctrl+B`) to confirm no errors.

> The `NPZ2100/` folder is **never touched** when you regenerate code in
> CubeMX. Include path and source location settings in CubeIDE are also
> preserved across regenerations.

---

### Step 5 — Integrate the reference main.c

The provided `Core/Src/main.c` contains the full nPZ2100 boot sequence
inside CubeMX `/* USER CODE BEGIN/END */` sections so it survives
future code regenerations.

**Option A — Replace** (quickest for a fresh project):
Copy `Core/Src/main.c` from this package over the CubeMX-generated one.

**Option B — Merge** (if you already have application code):
Copy these blocks from the reference `main.c` into your own file:

```
USER CODE BEGIN Includes     →  #include "npz2100_stm32.h"
USER CODE BEGIN PV           →  handle, regmap[], sensor_init_cmds[]
USER CODE BEGIN 2            →  NPZ2100_Init → BootStatus → Readback → ApplyRegmap
USER CODE BEGIN WHILE        →  wake-reason handler → ShadowFlush → EnterIdle
```

---

### Step 6 — Wire the hardware and test

**Hardware connections:**

| nPZ2100 pin | STM32L053 pin | Notes |
|-------------|--------------|-------|
| SDA | PC4 | I²C1 SDA |
| SCL | PC5 | I²C1 SCL |
| VBAT | 3.0–3.3 V | STM32 supply rail |
| VSS | GND | |
| SW_HP | STM32L053 VDD | Power control — see note below |

**Required bypass capacitors** (C0G dielectric, placed close to nPZ2100):

| Ref | Value | nPZ2100 pin |
|-----|-------|-------------|
| C1 | 100 nF | VBAT |
| C2 | 10 nF | VDD1V2 |
| C3 | 10 nF | VDDD |

**Required external I²C pull-up resistors:**
STM32L053 internal pull-ups are insufficient for 100 kHz — external resistors
are required per ST application note AN10441.

| Ref | Value | Net |
|-----|-------|-----|
| R1 | 4.7 kΩ | SDA (PC4) → VBAT |
| R2 | 4.7 kΩ | SCL (PC5) → VBAT |

**SW_HP → STM32L053 VDD:**
SW_HP controls the STM32 power supply. Connect via a P-channel MOSFET or
load switch rated for the STM32's peak current. SW_HP can source 10 mA
directly; use an external switch for higher currents.

> **Bench evaluation without power control:** power the STM32 directly from
> the ST-LINK USB. Leave SW_HP disconnected. I²C communication works
> normally — `NPZ2100_EnterIdle()` will write the idle command but the STM32
> stays powered since its supply is independent of SW_HP. This is enough to
> verify I²C communication and the full boot sequence.

Build (`Ctrl+B`) → Flash (`F11`) → observe via SWV ITM Console or UART.

**Expected first boot output** (add `printf` via SWV or UART as needed):
- `NPZ2100_Init` returns `NPZ2100_OK` → device found
- `reason.rst_src == 0x00` → `NPZ2100_RST_SRC_POR` → cold boot
- `NPZ2100_ApplyRegmap` writes ~18 registers (first boot, shadow = defaults)
- Execution reaches `NPZ2100_EnterIdle` → STM32 power cut (if SW_HP wired)

**Subsequent warm boots** after a trigger:
- `NPZ2100_ApplyRegmap` writes 0 registers (device already in sync)
- `reason.periph_mask` → non-zero → peripheral triggered

---

## Boot sequence (every STM32 boot)

```c
#include "npz2100_stm32.h"

NPZ2100_Handle_t hnpz;

// After HAL_Init(), SystemClock_Config(), MX_I2C1_Init():

// 1. Init — probe device (ID must = 0x74), seed shadow
NPZ2100_Init(&hnpz, &hi2c1);

// 2. Read wake reason — ALWAYS first I²C op; also kicks watchdog
NPZ2100_WakeReason_t reason;
NPZ2100_BootStatus(&hnpz, &reason);

// 3. Sync shadow — nPZ2100 retains registers while STM32 is off
NPZ2100_Readback(&hnpz);

// 4. Apply desired config — only changed registers written
NPZ2100_ApplyRegmap(&hnpz, regmap, sizeof(regmap));

// 5. Handle wake reason
if (reason.rst_src == NPZ2100_RST_SRC_POR) { /* cold boot — write SRAM */ }
if (reason.periph_mask & 0x01)             { /* peripheral 1 triggered */ }
if (reason.adc3)                           { /* battery threshold      */ }
if (reason.timeout)                        { /* periodic time-out      */ }

// 6. Push any runtime config changes made via typed helpers
NPZ2100_ShadowFlush(&hnpz);

// 7. Re-enter idle — power cut — does NOT return
NPZ2100_EnterIdle(&hnpz);
```

---

## API reference

| Function | Description |
|----------|-------------|
| `NPZ2100_Init(h, hi2c)` | Init, probe (ID=0x74), seed shadow |
| `NPZ2100_BootStatus(h, &r)` | Read STA1–3, decode reason, kick watchdog |
| `NPZ2100_Readback(h)` | Sync shadow from device |
| `NPZ2100_ApplyRegmap(h, map, len)` | Diff-apply byte-stream regmap |
| `NPZ2100_GetShadow(h)` | Pointer to shadow for typed helpers |
| `NPZ2100_ShadowFlush(h)` | Push shadow changes to device |
| `NPZ2100_SramWrite(h, addr, data, len)` | Write sensor init cmds to SRAM |
| `NPZ2100_SramRead(h, addr, data, len)` | Read from SRAM |
| `NPZ2100_PeriphReadValue(h, slot, &v)` | Read last peripheral sample |
| `NPZ2100_EnterIdle(h)` | Idle command — power cut — does not return |
| `NPZ2100_SoftReset(h)` | Soft reset (config + SRAM preserved) |

All functions return `NPZ2100_OK` (0) on success or a negative `NPZ2100_Status_t`.

---

## Wake reason flags (`NPZ2100_WakeReason_t`)

| Field | Description |
|-------|-------------|
| `rst_src` | `NPZ2100_RST_SRC_POR`=cold boot, `_EXT`=NRST pin, `_BOR`=brown-out |
| `periph_mask` | Bitmask: bit N-1 set if peripheral N+1 triggered |
| `adc1`, `adc2`, `adc3` | ADC threshold crossed |
| `timeout` | Periodic time-out (no other source first) |
| `alarm` | Global time counter alarm |
| `counter` | Event counter reached trigger value |
| `nak_mask` | Bitmask: peripheral N+1 NAK'd I²C |

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| Hangs in `while(1)` after `NPZ2100_Init` | Device not found | Check PC4/PC5 wiring; verify 4.7 kΩ pull-ups on SDA/SCL; confirm I²C address 0x6F |
| `NPZ2100_ERR_IO` immediately | I²C not initialised | Ensure `MX_I2C1_Init()` runs before `NPZ2100_Init()` |
| `NPZ2100_ERR_IO` on first write only | Clock mismatch | Verify I2C1 clock source is HSI in CubeMX Clock Config tab |
| Build error: `npz2100_stm32.h: No such file` | Include path missing | Add `../NPZ2100/Inc` in Project → Properties → GCC Compiler → Include paths |
| Build error: `undefined reference to NPZ2100_Init` | Source folder missing | Add `NPZ2100/Src` in Project → Properties → GCC Compiler → Source Location |
| Build error: `no member named 'ClockSpeed'` | Wrong I2C init fields | STM32L0 uses `hi2c1.Init.Timing` — never `ClockSpeed`/`DutyCycle`. Use the provided `main.c`. |
| Build error: `redeclaration of enumerator 'NPZ2100_OK'` | Duplicate enum | `NPZ2100_Status_t` must be a typedef alias of `npz2100_err_t`, not a new enum. Use the provided `npz2100_stm32.h`. |
| `NPZ2100_ERR_ARG` from `ApplyRegmap` | Malformed regmap | Check segment length bytes: `length = 1 (start_addr) + N (data bytes)` |
| Regenerating code breaks main.c | Code outside USER CODE blocks | Keep all nPZ2100 code inside `/* USER CODE BEGIN/END */` fences |

---

## Contact

Nanopower Semiconductor AS — www.nanopowersemi.com — info@nanopowersemi.com
