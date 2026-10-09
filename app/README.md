# Capstone: PPI fork (nRF52840 DK)

## One event, two pins, no CPU

A PPI channel on nRF52 has three endpoint registers: `EEP` (event), `TEP`
(task) and `FORK.TEP` (a second task). One TIMER2 compare event drives both
GPIOTE toggle tasks through the same channel, so the two pins transition on the
same clock edge. No interrupt is enabled anywhere in the path — it keeps
running while the CPU is busy or asleep.

## Pin map

| Arduino | Port  | Signal          | Block  |
|---------|-------|-----------------|--------|
| D8      | P1.10 | toggle, primary | GPIOTE |
| D9      | P1.11 | toggle, fork    | GPIOTE |

Nothing else in the board devicetree claims any of these.

## Build and flash

```sh
cd ~/zephyrproject/Backscatter-Enabled-Radio/app
west build -b nrf52840dk/nrf52840 -p always -d build .
west flash -d build
```

Console is UART0 over the DK's J-Link CDC at 115200 8N1.

## What you should see

**P1.10 / P1.11** — a complementary 4 MHz square-wave pair, continuous, edges
aligned to within a single 16 MHz timer tick. 4 MHz is the ceiling (16 MHz
timer, two compare events per output period); lower `PPI_TOGGLE_HZ` in
`src/main.c` for a slower pair.

## Knobs

| Where | What |
|-------|------|
| `main.c: PPI_TOGGLE_HZ` | fork output frequency, up to 4 MHz |

## Notes and caveats

- **Hardware status.** Confirmed producing a stable square wave on D8,
  free-running while `main()` sleeps. Complementarity, edge skew and the
  debugger-halt test are still open.

- **HFXO is not automatic.** On reset HFCLK runs from HFINT, an internal RC
  oscillator good to only a few percent, and nothing in this application
  starts the crystal on its own (Zephyr's clock is LFCLK/RTC-based and
  `CONFIG_UART_NRFX_UARTE_HFXO_ON_ACTIVE` is unset). This was found the hard
  way: the 1 MHz PPI output measured 970 kHz -- exactly the RC error.
  `ppi_fork_start()` calls `hfxo_request()` (`src/hfxo.c`). Any future module
  whose output frequency matters must call it too.
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
