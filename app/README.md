# Capstone: QSPI pattern generator + PPI fork (nRF52840 DK)

Two independent experiments in one image.

## 1. QSPI as a maximum-speed digital output

The nRF52840's QSPI block is clocked from HFCLK128M and can drive SCK at
**32 MHz** — faster than SPIM (8 MHz), faster than PWM, and faster than the
CPU can reach writing `OUTSET`/`OUTCLR`. In quad mode it shifts four bits per
SCK clock across IO0..IO3, so a RAM buffer becomes an arbitrary 4-bit-wide
waveform at 32 Mbit/s per line (128 Mbit/s aggregate).

The peripheral is taken away from Zephyr entirely: `&qspi` and `&mx25r64` are
disabled in the overlay so `nrfx_qspi` owns it, and the signals are PSEL'd onto
the Arduino header rather than the onboard MX25R6435F.

That matters because QSPI is a *flash controller*, not a generic bus: `writeoc`
is a literal JEDEC opcode, the peripheral auto-inserts `WREN`, and it polls the
memory's WIP bit after every operation. Left on the stock pins, each burst is a
real quad page program into the MX25R64 — it wears the part, it stalls for the
program time before the next burst can start, and page-wrap plus
can-only-clear-bits means the chip doesn't even end up holding what you sent.
The raw-opcode paths (`nrfx_qspi_cinstr_xfer()`, long-frame mode) are sharper
still, since there you choose the command byte and `0xC7` is a chip erase.
Header pins have none of that, and can be probed.

## 2. One event, two pins, no CPU

A PPI channel on nRF52 has three endpoint registers: `EEP` (event), `TEP`
(task) and `FORK.TEP` (a second task). One TIMER2 compare event drives both
GPIOTE toggle tasks through the same channel, so the two pins transition on the
same clock edge. No interrupt is enabled anywhere in the path — it keeps
running while the CPU is busy or asleep.

## Pin map

| Arduino | Port  | Signal          | Block  |
|---------|-------|-----------------|--------|
| D2      | P1.03 | QSPI SCK        | QSPI   |
| D3      | P1.04 | QSPI CSN        | QSPI   |
| D4      | P1.05 | QSPI IO0        | QSPI   |
| D5      | P1.06 | QSPI IO1        | QSPI   |
| D6      | P1.07 | QSPI IO2        | QSPI   |
| D7      | P1.08 | QSPI IO3        | QSPI   |
| D8      | P1.10 | toggle, primary | GPIOTE |
| D9      | P1.11 | toggle, fork    | GPIOTE |

Nothing else in the board devicetree claims any of these.

## Build and flash

```sh
cd ~/zephyrproject/capstone/app
west build -b nrf52840dk/nrf52840 -p always -d build .
west flash -d build
```

Console is UART0 over the DK's J-Link CDC at 115200 8N1.

## What you should see

**P1.10 / P1.11** — a complementary 1 MHz square-wave pair, continuous, edges
aligned to within a single 16 MHz timer tick. Raise `PPI_TOGGLE_HZ` in
`src/main.c` up to 4 MHz (the ceiling: 16 MHz timer, two compare events per
output period).

**P1.03 / P1.05..P1.08** — a burst every 500 ms. Each `qspi_pattern_square()`
call puts *two* CSN frames on the wire, because the QSPI peripheral
automatically prepends a `WREN` (0x06) before every page program. The second
frame is the interesting one:

```
CSN  ‾‾|________________________________|‾‾
SCK    |‾|_|‾|_ ... 32 MHz ... _|‾|_|‾|_|
IO0..3  <- 0x38 opcode (8 clk, IO0 only)
        <- 24-bit address (6 clk, quad)
        <- 512 payload nibbles (0xF0 fill)
```

With a `0xF0` fill every IO line goes high for one clock and low for the next,
so all four carry a **16 MHz square wave** (SCK/2) for the 16 µs of the burst.

## Knobs

| Where | What |
|-------|------|
| `main.c: PPI_TOGGLE_HZ` | fork output frequency, up to 4 MHz |
| `main.c: QSPI_BURST_BYTES` | burst length, up to `QSPI_PATTERN_MAX_BURST` (1024) |
| `qspi_pattern_init(NRF_QSPI_FREQ_DIVn)` | SCK = 32 MHz / (n+1) |
| `qspi_pattern_burst(buf, len)` | emit an arbitrary pattern instead of the square wave |

Useful fill bytes in quad mode (high nibble first, each nibble drives
IO3..IO0 for one clock):

- `0xF0` — all four lines toggle together at SCK/2
- `0x5A` — IO0/IO2 and IO1/IO3 in antiphase
- `0xFF` — all lines held high for the whole payload
- a `0x00,0x11,0x22,...` ramp — a slow counter visible on a logic analyser

## Notes and caveats

- **Hardware status.** PPI: confirmed producing a stable square wave on D8,
  free-running while `main()` sleeps. QSPI: `nrfx_qspi_activate()` confirmed
  returning 0 with no memory on the bus, so the bring-up path works; the
  waveform itself is not yet scoped. Complementarity, edge skew, the
  debugger-halt test and all QSPI signal measurements are still open.

- **HFXO is not automatic.** On reset HFCLK runs from HFINT, an internal RC
  oscillator good to only a few percent, and nothing in this application
  starts the crystal on its own (Zephyr's clock is LFCLK/RTC-based and
  `CONFIG_UART_NRFX_UARTE_HFXO_ON_ACTIVE` is unset). This was found the hard
  way: with QSPI disabled, the 1 MHz PPI output measured 970 kHz -- exactly
  the RC error. Both modules now call `hfxo_request()` (`src/hfxo.c`);
  requests are reference counted. Any future module whose output frequency
  matters must call it too.
- **IO1 pull-down.** `TASKS_ACTIVATE` makes the peripheral read the memory's
  status register and wait for the WIP bit to clear. With no memory on the bus
  a floating IO1 can read WIP=1 forever, so `qspi_pattern_init()` applies a
  pull-down to P1.06 after `nrfx_qspi_init()` (which resets `PIN_CNF` to
  `NOPULL`). In practice activation succeeds on a bare header with no jumper.
  It is still unknown whether the software pull-down is load-bearing or
  whether ACTIVATE simply does not poll on this part -- to find out, comment
  out the `qspi_idle_pulldown_apply()` call and see if activation still
  returns 0. If it ever does return `-ETIMEDOUT`, strap P1.06 to GND.
- **Nibble order.** MSB-nibble-first is the standard SPI convention and is what
  the fill-byte table above assumes; worth confirming on the analyser before
  relying on it for a specific pattern.
- **Sharing GPIOTE with Zephyr.** `CONFIG_GPIO=y`: Zephyr's `gpio_nrfx` driver
  and `ppi_fork.c` both use nrfx_gpiote on the single GPIOTE instance, which is
  safe because nrfx's channel allocator is a module-global atomic mask keyed by
  instance index. The two drivers do keep separate *per-pin* bookkeeping, so
  don't drive one pad through both APIs. Budget is 8 GPIOTE channels total: two
  are taken here, and each Zephyr GPIO **edge interrupt** may claim one more
  (level/`SENSE`-based interrupts use the shared PORT event instead and cost no
  channel). Plain reads, writes, I2C, SPI and ADC use no GPIOTE at all.
- **TIMER2** rather than TIMER0, which the Bluetooth controller claims if one
  is ever linked in.
