#ifndef HELPER_H
#define HELPER_H

#include <stdint.h>

#include <zephyr/sys/util.h>

#if IS_ENABLED(CONFIG_BT_MESH)
#include <bluetooth/mesh/models.h>
#endif

#if IS_ENABLED(CONFIG_BT_MESH)
bool own_message(const struct bt_mesh_model *model,
				 const struct bt_mesh_msg_ctx *ctx);
#endif
void apply_config(const uint8_t *data, uint16_t len);

#endif // HELPER_H