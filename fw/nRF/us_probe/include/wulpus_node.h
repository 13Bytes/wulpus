#ifndef WULPUS_NODE_H
#define WULPUS_NODE_H

#include <stdint.h>

void wulpus_node_id_set_fallback(uint16_t node_id);
uint16_t wulpus_node_id_get(void);

uint32_t wulpus_timestamp_ms_get(void);

#endif // WULPUS_NODE_H
