#include "wulpus_node.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#if IS_ENABLED(CONFIG_BT_MESH)
#include "mesh.h"
#endif

static uint16_t fallback_node_id = 1;

void wulpus_node_id_set_fallback(uint16_t node_id)
{
    fallback_node_id = (node_id == 0) ? 1 : node_id;
}

uint16_t wulpus_node_id_get(void)
{
#if IS_ENABLED(CONFIG_BT_MESH)
    uint16_t const mesh_addr = bt_mesh_primary_addr();
    if (mesh_addr != BT_MESH_ADDR_UNASSIGNED && mesh_addr != 0)
    {
        return mesh_addr;
    }
#endif

    return fallback_node_id;
}

uint32_t wulpus_timestamp_ms_get(void)
{
#if IS_ENABLED(CONFIG_BT_MESH)
    return mesh_get_network_timestamp();
#else
    return k_uptime_get_32();
#endif
}
