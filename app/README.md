# Capstone: QSPI pattern generator (nRF52840 DK)

## QSPI as a maximum-speed digital output

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

## Pin map

| Arduino | Port  | Signal          | Block  |
|---------|-------|-----------------|--------|
| D2      | P1.03 | QSPI SCK        | QSPI   |
| D3      | P1.04 | QSPI CSN        | QSPI   |
| D4      | P1.05 | QSPI IO0        | QSPI   |
| D5      | P1.06 | QSPI IO1        | QSPI   |
| D6      | P1.07 | QSPI IO2        | QSPI   |
| D7      | P1.08 | QSPI IO3        | QSPI   |

Nothing else in the board devicetree claims any of these.

## Build and flash

```sh
cd ~/zephyrproject/Backscatter-Enabled-Radio/app
west build -b nrf52840dk/nrf52840 -p always -d build .
west flash -d build
```

Console is UART0 over the DK's J-Link CDC at 115200 8N1.

## What you should see

**P1.03 / P1.05..P1.08** — a burst every 10 ms. Each `qspi_pattern_square()`
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

- **Hardware status.** `nrfx_qspi_activate()` confirmed returning 0 with no
  memory on the bus, so the bring-up path works. Scoped at DIV8 (2 MHz on the
  data lines) and DIV1 (16 MHz on the data lines, read with cursors -- the
  scope's auto-measure aliases to 8 MHz when zoomed out). Nibble order and
  signal quality at 32 MHz are still open.

- **HFXO is not automatic.** On reset HFCLK runs from HFINT, an internal RC
  oscillator good to only a few percent, and nothing in this application
  starts the crystal on its own (Zephyr's clock is LFCLK/RTC-based and
  `CONFIG_UART_NRFX_UARTE_HFXO_ON_ACTIVE` is unset). This was found the hard
  way: a 1 MHz timer-driven output measured 970 kHz -- exactly the RC error.
  `qspi_pattern_init()` calls `hfxo_request()` (`src/hfxo.c`). Any future
  module whose output frequency matters must call it too.
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
