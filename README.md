# SAME54-FREQUENCY-COUNTER

SAME54 Xplained Pro frequency counter using TC0 as a 1ms gate timer and TC2 as a hardware pulse counter via GCLKIN.

An external frequency measurement project for the SAM E54 Xplained Pro Board using MPLAB Harmony 3 (MCC).

## Objective

This repository demonstrates how to measure an external signal frequency using the SAME54's hardware timer/counter peripherals without CPU involvement in the counting process.

TC0 acts as a precise 1ms gate timer, TC2 counts incoming pulses from an external signal routed through GCLKIN → GCLK3 → PCHCTRL[26], and the result is printed over UART via SERCOM2. TC4 generates the test signal used as the input source.

> **Key insight:** GCLK3 and TC2 must be initialized **after** TC4 starts, because GCLK3 is sourced from GCLKIN (an external pin). Initializing GCLK3 before the clock source is present causes the synchronization busy-loop to hang indefinitely.

## Related Documentation

* [SAM E54 Xplained Pro Evaluation Kit](https://www.microchip.com/en-us/development-tool/ATSAME54-XPRO)
* [ATSAME54P20A Product Page](https://www.microchip.com/en-us/product/ATSAME54P20A)
* [SAM D5x/E5x Family Data Sheet](https://www.microchip.com/en-us/product/ATSAME54P20A#Documentation)
* [MPLAB Harmony 3](https://www.microchip.com/en-us/tools-resources/configure/mplab-harmony)
* [Microchip 32-bit Examples](https://github.com/microchip-pic-avr-examples)

## How It Works

```
TC4 (Compare/PWM output)
  │
  └─► GCLKIN pin ──► GCLK3 (SRC=2, DIV=1) ──► PCHCTRL[26] ──► TC2 (counts pulses)
                                                                       │
TC0 (1ms gate timer) ──► overflow interrupt ──────────────────────────┘
                                │
                                └─► read TC2 count → print frequency over UART
```

**Frequency formula:**
```
Frequency (MHz) = TC2_count / 1000
```
Since TC2 counts pulses over 1ms, dividing by 1000 converts counts/ms to MHz.

**Maximum measurable frequency** (16-bit TC2):
```
65,535 counts / 1ms gate = 65.535 MHz maximum
```

## Hardware Requirements

* SAM E54 Xplained Pro development board

## MCC / Harmony 3 Project Graph

The following peripherals are configured in the MCC Project Graph:

| Peripheral | Type | Role |
|---|---|---|
| TC0 | Timer | 1ms gate — triggers callback on overflow |
| TC2 | Timer | Pulse counter — clocked by GCLKIN via GCLK3 |
| TC4 | Compare | Signal generator — drives GCLKIN pin |
| SERCOM2 | UART | Printf output via STDIO |
| EVSYS | Event System | Peripheral routing |
| NVMCTRL | Memory | Flash wait states |

## Software Requirements

* **MPLAB X IDE** (v6.20 or later)
* **XC32 Compiler**
* **MPLAB Harmony 3 / MCC**

## MCC Configuration

### TC0 — Gate Timer
- Mode: Timer
- Period: 1ms
- Interrupt: Enabled (overflow callback)

### TC2 — Pulse Counter
- Mode: Timer (counter input)
- Clock Source: GCLK3 (GCLKIN — external pin)
- Size: 16-bit
- **Note:** Do NOT initialize in `SYS_Initialize()` — must be called manually after GCLK3 is live

### TC4 — Signal Generator
- Mode: Compare (waveform output)
- Output: Connected to GCLKIN pin
- Started before GCLK3 initialization

### GCLK3 — Custom Initialization
GCLK3 is **not** initialized by MCC. It is configured manually in `GCLK3_Initialize_Custom()`:

```c
// Source = GCLKIN (SRC=2), DIV=1, Generator enabled
GCLK_REGS->GCLK_GENCTRL[3] = GCLK_GENCTRL_DIV(1U)
                             | GCLK_GENCTRL_SRC(2U)
                             | GCLK_GENCTRL_GENEN_Msk;

// Connect to TC2/TC3 peripheral channel (PCHCTRL index 26)
GCLK_REGS->GCLK_PCHCTRL[26] = GCLK_PCHCTRL_GEN(0x3U)
                              | GCLK_PCHCTRL_CHEN_Msk;
```

> **Why PCHCTRL[26]?** Per the SAM D5x/E5x datasheet, index 26 maps to `GCLK_TC2_GCLK_TC3` — the shared clock channel for TC2 and TC3.

### SERCOM2 — UART / STDIO
- Mode: UART
- Connected to STDIO for `printf()`

## Initialization Order (Critical)

The order of initialization matters because of the GCLKIN dependency:

```c
SYS_Initialize(NULL);               // Standard Harmony init (TC2 commented out here)
TC0_TimerCallbackRegister(...);     // Register 1ms gate callback
TC4_CompareStart();                 // ← MUST come first: starts GCLKIN signal
GCLK3_Initialize_Custom();          // ← Safe now: clock source is present
TC2_TimerInitialize();              // ← Safe now: GCLK3 is running
```

If `GCLK3_Initialize_Custom()` is called before TC4 starts, the sync busy-loop will hang forever because the GCLKIN pin has no signal.

## Key Functions

| Function | Description |
|---|---|
| `TC0_Callback()` | ISR — stops both timers, captures TC2 count, sets flag |
| `GCLK3_Initialize_Custom()` | Configures GCLK3 with GCLKIN source and routes to TC2 |
| `TC2_Timer16bitCounterGet()` | Reads the raw pulse count from TC2 |
| `TC4_CompareStart()` | Starts the test signal generator |

## Main Loop

```c
while (true)
{
    SYS_Tasks();

    s_gateComplete = false;
    TC0_TimerStart();               // Start 1ms gate
    TC2_TimerStart();               // Start counting pulses

    while (!s_gateComplete);        // Wait for gate to close (TC0 overflow ISR)

    printf("Measured Frequency: %.3f MHz\r\n", (float)s_pulseCount / 1000.0f);
}
```

## TC0 Callback

```c
static void TC0_Callback(TC_TIMER_STATUS status, uintptr_t context)
{
    (void)status;   // Unused — only overflow fires this callback in this config
    (void)context;  // Unused — no per-channel state needed (single channel, globals used)

    TC0_TimerStop();
    s_pulseCount   = TC2_Timer16bitCounterGet();
    TC2_TimerStop();
    s_gateComplete = true;
}
```

## Measurement Limitations

| Parameter | Value |
|---|---|
| Gate window | 1 ms |
| Counter size | 16-bit |
| Max measurable frequency | **65.535 MHz** |
| Frequency resolution | 1 kHz (0.001 MHz) |

To extend the range, options include:
- **Shorter gate window** (e.g. 0.5ms → up to ~131 MHz, adjust divisor)
- **GCLK3 prescaler** (e.g. DIV=4 → multiply result by 4, up to ~262 MHz)
- **32-bit mode** (chain TC2+TC3 → effectively no overflow concern)

## Troubleshooting

### Lockup on startup
- **Cause:** GCLK3 initialized before TC4 is running
- **Fix:** Ensure `TC4_CompareStart()` is called before `GCLK3_Initialize_Custom()`

### Always reads 0 MHz
- Verify TC2 is initialized after GCLK3 (`TC2_TimerInitialize()` called manually, not via `SYS_Initialize()`)
- Check that GCLKIN pin is correctly mapped to TC4's output in MCC pin manager

### Reading seems too low (e.g. half expected value)
- Check `GCLK_GENCTRL_DIV` value — if set to 2 or higher, multiply result accordingly

### No UART output
- Verify SERCOM2 is connected to STDIO in Project Graph
- Check baud rate matches your terminal

## License

This project is open source. Feel free to use and modify for your projects.

## Author

Created for SAME54 frequency measurement applications using MPLAB Harmony 3.
