/*
 * One TIMER event -> PPI channel + its fork -> two GPIOTE tasks.
 *
 * A PPI channel on the nRF52 series has three endpoint registers: EEP (the
 * event), TEP (the task) and FORK.TEP (a second task). Both task endpoints
 * fire from the same event in the same clock cycle, so the two pins transition
 * together with no software in the path at all -- no interrupt, no jitter, and
 * it keeps running while the CPU is busy or asleep.
 */

#ifndef PPI_FORK_H_
#define PPI_FORK_H_

#include <stdint.h>

/**
 * Start toggling both pins at @p toggle_hz.
 *
 * The TIMER runs at 16 MHz and each compare event toggles the pins, so one
 * output period costs two events and the ceiling is 4 MHz (ticks >= 2).
 *
 * @return 0 on success, negative errno otherwise.
 */
int ppi_fork_start(uint32_t toggle_hz);

/** Stop the timer and release the PPI channel and both GPIOTE channels. */
void ppi_fork_stop(void);

#endif /* PPI_FORK_H_ */
