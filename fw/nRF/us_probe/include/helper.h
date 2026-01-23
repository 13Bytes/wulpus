#ifndef HELPER_H
#define HELPER_H

#include <bluetooth/mesh/models.h>
#include <stdint.h>

bool own_message(const struct bt_mesh_model *model,
				 const struct bt_mesh_msg_ctx *ctx);
void apply_config(const uint8_t *data, uint16_t len);

#endif // HELPER_H