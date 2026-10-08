#include "ppi_fork.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <nrfx_gpiote.h>
#include <nrfx_timer.h>
#include <helpers/nrfx_gppi.h>
#include <hal/nrf_gpio.h>

LOG_MODULE_REGISTER(ppi_fork, LOG_LEVEL_INF);

/*
 *   		    Port    Role
 *   -------   ----    ----
 *   D8        P1.10   primary  (PPI channel TEP)
 *   D9        P1.11   fork     (PPI channel FORK.TEP)
 *
 * The two pins are given opposite initial states so the scope shows a
 * complementary pair (which also makes it obvious that both edges land on
 * the same clock rather than one lagging the other when we're testing)
 */
#define PIN_PRIMARY NRF_GPIO_PIN_MAP(1, 10)
#define PIN_FORK    NRF_GPIO_PIN_MAP(1, 11)

/* TIMER2: TIMER0 belongs to the Bluetooth controller if one is ever linked in,
 * and TIMER1 is a common default for other subsystems. (use timer 2 to be safe)
 */
static nrfx_timer_t timer = NRFX_TIMER_INSTANCE(NRF_TIMER2);

/* The nRF52840 has a single GPIOTE instance. CONFIG_GPIO is off so nothing
 * else is allocating out of it.
 */
static nrfx_gpiote_t gpiote = NRFX_GPIOTE_INSTANCE(NRF_GPIOTE);

static nrfx_gppi_handle_t gppi_h;
static uint8_t ch_primary;
static uint8_t ch_fork;
static bool running;

static int out_pin_configure(nrfx_gpiote_pin_t pin, uint8_t channel,
			     nrf_gpiote_outinit_t init_val)
{
	static const nrfx_gpiote_output_config_t output_config = {
		/* H0H1 keeps the edges clean at MHz rates into a scope probe. */
		// higher current for a cleaner edge (if there is overshoot change this)
		.drive = NRF_GPIO_PIN_H0H1,
		// the GPIOs are output only
		.input_connect = NRF_GPIO_PIN_INPUT_DISCONNECT,
		// already driven
		.pull = NRF_GPIO_PIN_NOPULL,
	};

	const nrfx_gpiote_task_config_t task_config = {
		// select channel from the 8 available
		.task_ch = channel,
		// determines what TASKS_OUT does
		.polarity = NRF_GPIOTE_POLARITY_TOGGLE,
		// initial gpio value
		.init_val = init_val,
	};

	// writes one register per pin allowing GPIOTE to drive
	return nrfx_gpiote_output_configure(&gpiote, pin, &output_config, &task_config);
}

int ppi_fork_start(uint32_t toggle_hz)
{
	uint32_t base_frequency;
	uint32_t ticks;
	int err;

	if (running) {
		return -EALREADY;
	}

	if (toggle_hz == 0U) {
		return -EINVAL;
	}

	if (!nrfx_gpiote_init_check(&gpiote)) {
		err = nrfx_gpiote_init(&gpiote, NRFX_GPIOTE_DEFAULT_CONFIG_IRQ_PRIORITY);
		if (err != 0) {
			LOG_ERR("nrfx_gpiote_init failed (%d)", err);
			return err;
		}
	}

	// claim the channels
	err = nrfx_gpiote_channel_alloc(&gpiote, &ch_primary);
	if (err != 0) {
		LOG_ERR("no GPIOTE channel for the primary pin (%d)", err);
		return err;
	}

	// claim the channels
	err = nrfx_gpiote_channel_alloc(&gpiote, &ch_fork);
	if (err != 0) {
		LOG_ERR("no GPIOTE channel for the fork pin (%d)", err);
		goto free_primary_ch;
	}

	// bind pins
	err = out_pin_configure(PIN_PRIMARY, ch_primary, NRF_GPIOTE_INITIAL_VALUE_HIGH);
	if (err != 0) {
		LOG_ERR("primary pin configure failed (%d)", err);
		goto free_fork_ch;
	}

	err = out_pin_configure(PIN_FORK, ch_fork, NRF_GPIOTE_INITIAL_VALUE_LOW);
	if (err != 0) {
		LOG_ERR("fork pin configure failed (%d)", err);
		goto free_fork_ch;
	}

	// arms the tasks so TASKS_OUT[ch] @ 0x40006000 + 4ch becomes live 
	// writing 1 here toggles the pin
	nrfx_gpiote_out_task_enable(&gpiote, PIN_PRIMARY);
	nrfx_gpiote_out_task_enable(&gpiote, PIN_FORK);


	// set up the timer
	// MODE      @ 0x4000A504  = Timer     (count PCLK16M, not external pulses)
	// BITMODE   @ 0x4000A508  = 32-bit
	// PRESCALER @ 0x4000A510  = 0         (16 MHz / 2^0)
	// CC[0]     @ 0x4000A540  = ticks
	// SHORTS    @ 0x4000A200  = COMPARE0_CLEAR
	base_frequency = NRF_TIMER_BASE_FREQUENCY_GET(timer.p_reg);  //16 MHz

	// prescaler is 0 so full 16 MHz no division
	nrfx_timer_config_t timer_config = NRFX_TIMER_DEFAULT_CONFIG(base_frequency);

	timer_config.bit_width = NRF_TIMER_BIT_WIDTH_32;

	/* NULL handler: the timer never raises an interrupt. The whole point of
	 * the exercise is that the CPU is not involved once this is armed.
	 */
	err = nrfx_timer_init(&timer, &timer_config, NULL);
	if (err != 0) {
		LOG_ERR("nrfx_timer_init failed (%d)", err);
		goto disable_tasks;
	}

	nrfx_timer_clear(&timer);

	/* Two compare events per output period, since each one toggles. */
	// one output period costs two events
	ticks = base_frequency / (2U * toggle_hz);
	if (ticks < 2U) {
		ticks = 2U;
	}

	/* COMPARE0_CLEAR turns CC0 into a free-running period; enable_int is
	 * false so no NVIC line is ever asserted. (INTENSET is never written)
	 */
	nrfx_timer_extended_compare(&timer, NRF_TIMER_CC_CHANNEL0, ticks,
				    NRF_TIMER_SHORT_COMPARE0_CLEAR_MASK, false);

	/* EEP = TIMER2 COMPARE[0], TEP = GPIOTE OUT task for the primary pin. */
	err = nrfx_gppi_conn_alloc(
		nrfx_timer_compare_event_address_get(&timer, NRF_TIMER_CC_CHANNEL0),
		nrfx_gpiote_out_task_address_get(&gpiote, PIN_PRIMARY),
		&gppi_h);
	if (err != 0) {
		LOG_ERR("no PPI channel available (%d)", err);
		goto uninit_timer;
	}

	/* Second task endpoint on the same channel. On PPI hardware nrfx puts
	 * this in FORK.TEP because TEP is already taken; on DPPI parts the same
	 * call just subscribes another task to the channel. That is the whole
	 * reason for going through nrfx_gppi rather than nrf_ppi directly.
	 */
	err = nrfx_gppi_ep_attach(nrfx_gpiote_out_task_address_get(&gpiote, PIN_FORK), gppi_h);
	if (err != 0) {
		LOG_ERR("fork endpoint attach failed (%d)", err);
		goto free_conn;
	}

	nrfx_gppi_conn_enable(gppi_h);
	nrfx_timer_enable(&timer);

	running = true;

	LOG_INF("PPI fork running: P1.10 and P1.11 toggling at %u Hz "
		"(TIMER2 %u Hz, CC0 = %u ticks), CPU idle",
		base_frequency / (2U * ticks), base_frequency, ticks);

	return 0;

free_conn:
	nrfx_gppi_conn_free(
		nrfx_timer_compare_event_address_get(&timer, NRF_TIMER_CC_CHANNEL0),
		nrfx_gpiote_out_task_address_get(&gpiote, PIN_PRIMARY),
		gppi_h);
uninit_timer:
	nrfx_timer_uninit(&timer);
disable_tasks:
	nrfx_gpiote_out_task_disable(&gpiote, PIN_PRIMARY);
	nrfx_gpiote_out_task_disable(&gpiote, PIN_FORK);
	(void)nrfx_gpiote_pin_uninit(&gpiote, PIN_PRIMARY);
	(void)nrfx_gpiote_pin_uninit(&gpiote, PIN_FORK);
free_fork_ch:
	(void)nrfx_gpiote_channel_free(&gpiote, ch_fork);
free_primary_ch:
	(void)nrfx_gpiote_channel_free(&gpiote, ch_primary);

	return err;
}

void ppi_fork_stop(void)
{
	if (!running) {
		return;
	}

	nrfx_timer_disable(&timer);
	nrfx_gppi_conn_disable(gppi_h);
	nrfx_gppi_conn_free(
		nrfx_timer_compare_event_address_get(&timer, NRF_TIMER_CC_CHANNEL0),
		nrfx_gpiote_out_task_address_get(&gpiote, PIN_PRIMARY),
		gppi_h);
	nrfx_timer_uninit(&timer);

	nrfx_gpiote_out_task_disable(&gpiote, PIN_PRIMARY);
	nrfx_gpiote_out_task_disable(&gpiote, PIN_FORK);
	(void)nrfx_gpiote_pin_uninit(&gpiote, PIN_PRIMARY);
	(void)nrfx_gpiote_pin_uninit(&gpiote, PIN_FORK);
	(void)nrfx_gpiote_channel_free(&gpiote, ch_fork);
	(void)nrfx_gpiote_channel_free(&gpiote, ch_primary);

	running = false;
}
