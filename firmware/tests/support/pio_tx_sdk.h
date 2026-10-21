/**
 * @file pio_tx_sdk.h
 * @brief Isolated SDK declarations for host tests including the real PIO driver.
 *
 * Only TX calls have implementations in the test. Unused hardware paths are
 * discarded by section garbage collection, not simulated or executed.
 */
#ifndef PIO_TX_TEST_SDK_H
#define PIO_TX_TEST_SDK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "uart/dma/progress_math.h"

typedef unsigned int uint;
typedef int64_t absolute_time_t;
/** @brief Register fields accessed while compiling the real PIO driver for host TX tests. */
typedef struct {
    volatile uint32_t txf[4]; /**< TX FIFO register identities used to check the DMA destination. */
    volatile uint32_t rxf[4]; /**< RX register declarations for unused driver paths. */
    volatile uint32_t fdebug; /**< TXSTALL observations; hardware W1C/reassertion is modeled explicitly by tests. */
} pio_hw_t;
typedef pio_hw_t *PIO;
/** @brief Placeholder for unexecuted state-machine configuration calls. */
typedef struct { uint32_t unused; /**< Opaque mock storage. */ } pio_sm_config;
/** @brief Placeholder for unexecuted PIO program loading calls. */
struct pio_program { uint length; /**< Program size declaration. */ };
/** @brief TX settings observed in real configuration calls. */
typedef struct {
    bool enabled; /**< Mock default EN state, retained across ordinary completions. */
    bool read_increment; /**< Source-address increment setting. */
    bool write_increment; /**< Destination-address increment setting. */
    uint data_size; /**< Transfer width setting. */
    uint dreq; /**< Peripheral pacing selection. */
} dma_channel_config;
/** @brief DMA register declarations for unused RX/abort paths. */
typedef struct {
    struct {
        volatile uint32_t al1_ctrl; /**< Non-triggering control alias. */
        volatile uint32_t transfer_count; /**< Countdown register. */
    } ch[12]; /**< Mock DMA channel registers. */
} pio_test_dma_hw_t;

extern pio_hw_t pio_tx_test_pio0;
extern pio_hw_t pio_tx_test_pio1;
extern pio_test_dma_hw_t *dma_hw;
#define pio0 (&pio_tx_test_pio0)
#define pio1 (&pio_tx_test_pio1)
#define __isr
#define NUM_DMA_CHANNELS 12u
#define DMA_IRQ_1 1u
#define DMA_SIZE_8 0u
#define DMA_CH0_CTRL_TRIG_EN_BITS 1u
#define PIO_FDEBUG_TXSTALL_LSB 24u
#define PIO_FIFO_JOIN_TX 1u
#define PIO_FIFO_JOIN_RX 2u
#define GPIO_FUNC_SIO 0u
#define GPIO_OUT true
#define GPIO_SLEW_RATE_FAST 1u
#define GPIO_DRIVE_STRENGTH_12MA 3u
#define PICO_OK 0
#define PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY 0u
#define clk_sys 0u
#define UART_DMA_PROGRESS_H

static inline void __dmb(void) {}

uint32_t clock_get_hz(uint clock);
uint32_t save_and_disable_interrupts(void);
void restore_interrupts(uint32_t state);
void hw_clear_bits(volatile uint32_t *address, uint32_t mask);
void hw_set_bits(volatile uint32_t *address, uint32_t mask);
void irq_add_shared_handler(uint irq, void (*handler)(void), uint priority);
void irq_set_enabled(uint irq, bool enabled);
void gpio_deinit(uint pin);
bool gpio_get(uint pin);
void gpio_pull_down(uint pin);
void gpio_pull_up(uint pin);
void gpio_put(uint pin, bool value);
void gpio_set_dir(uint pin, bool output);
void gpio_set_drive_strength(uint pin, uint strength);
void gpio_set_function(uint pin, uint function);
void gpio_set_slew_rate(uint pin, uint rate);

bool pio_can_add_program(PIO pio, const struct pio_program *program);
uint pio_add_program(PIO pio, const struct pio_program *program);
void pio_clkdiv_restart_sm_mask(PIO pio, uint mask);
uint pio_get_dreq(PIO pio, uint sm, bool tx);
void pio_gpio_init(PIO pio, uint pin);
void pio_interrupt_clear(PIO pio, uint irq);
bool pio_interrupt_get(PIO pio, uint irq);
void pio_set_input_sync_bypass_with_mask(PIO pio, uint32_t value, uint32_t mask);
void pio_sm_clear_fifos(PIO pio, uint sm);
uint pio_sm_get_pc(PIO pio, uint sm);
uint pio_sm_get_tx_fifo_level(PIO pio, uint sm);
int pio_sm_init(PIO pio, uint sm, uint offset, const pio_sm_config *config);
bool pio_sm_is_rx_fifo_empty(PIO pio, uint sm);
bool pio_sm_is_tx_fifo_empty(PIO pio, uint sm);
void pio_sm_put(PIO pio, uint sm, uint32_t value);
void pio_sm_restart(PIO pio, uint sm);
void pio_sm_set_clkdiv(PIO pio, uint sm, float divider);
void pio_sm_set_consecutive_pindirs(PIO pio, uint sm, uint pin, uint count, bool output);
void pio_sm_set_enabled(PIO pio, uint sm, bool enabled);
void pio_sm_set_pins_with_mask(PIO pio, uint sm, uint32_t value, uint32_t mask);
void pio_sm_unclaim(PIO pio, uint sm);
void sm_config_set_clkdiv(pio_sm_config *config, float divider);
void sm_config_set_fifo_join(pio_sm_config *config, uint join);
void sm_config_set_in_pins(pio_sm_config *config, uint pin);
void sm_config_set_in_shift(pio_sm_config *config, bool right, bool autopush, uint threshold);
void sm_config_set_jmp_pin(pio_sm_config *config, uint pin);
void sm_config_set_out_pins(pio_sm_config *config, uint pin, uint count);
void sm_config_set_out_shift(pio_sm_config *config, bool right, bool autopull, uint threshold);
void sm_config_set_sideset_pins(pio_sm_config *config, uint pin);
extern const struct pio_program pio_uart_tx_program;
extern const struct pio_program pio_uart_tx_cts_program;
extern const struct pio_program pio_uart_rx_program;
pio_sm_config pio_uart_tx_program_get_default_config(uint offset);
pio_sm_config pio_uart_tx_cts_program_get_default_config(uint offset);
pio_sm_config pio_uart_rx_program_get_default_config(uint offset);

dma_channel_config dma_channel_get_default_config(uint channel);
void channel_config_set_transfer_data_size(dma_channel_config *config, uint size);
void channel_config_set_read_increment(dma_channel_config *config, bool increment);
void channel_config_set_write_increment(dma_channel_config *config, bool increment);
void channel_config_set_dreq(dma_channel_config *config, uint dreq);
void channel_config_set_ring(dma_channel_config *config, bool write, uint bits);
void channel_config_set_irq_quiet(dma_channel_config *config, bool quiet);
void dma_channel_configure(uint channel, const dma_channel_config *config, volatile void *write_address,
                           const volatile void *read_address, uint32_t count, bool trigger);
void dma_channel_set_read_addr(uint channel, const volatile void *address, bool trigger);
void dma_channel_set_trans_count(uint channel, uint32_t count, bool trigger);
bool dma_channel_is_busy(uint channel);
void dma_channel_abort(uint channel);
void dma_channel_unclaim(uint channel);
void dma_irqn_acknowledge_channel(uint irq, uint channel);
bool dma_irqn_get_channel_status(uint irq, uint channel);
void dma_irqn_set_channel_enabled(uint irq, uint channel, bool enabled);
uint32_t uart_dma_rx_progress(uint channel);
uint32_t uart_dma_rx_transfer_count_max(void);
uint32_t uart_dma_rx_transfer_count_encoded(void);
uint32_t uart_dma_rx_transfer_count_remaining(uint channel);
bool uart_dma_rx_wait_paused_progress_stable(uint channel);

#endif