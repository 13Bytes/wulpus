#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

void rand_sender_fixed_frequency_thread(void);
void send_random_data(uint8_t tx_rx_id, uint16_t meas_frame_nr,
                      struct k_msgq *ble_tx_msgq);

void mock_sender_thread(void);
void set_mesh_job_state(bool active);

extern uint32_t mesh_rand_sender_period_ms;