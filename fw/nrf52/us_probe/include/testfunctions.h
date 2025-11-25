#include <zephyr/sys/atomic.h>
#include <zephyr/kernel.h>

void rand_sender_thread(void *msgq_ptr, void *unused1, void *unused2);
void send_random_data(uint8_t tx_rx_id, uint16_t meas_frame_nr, struct k_msgq *ble_tx_msgq);
