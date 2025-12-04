#include <bluetooth/mesh/models.h>

bool own_message(const struct bt_mesh_model *model, const struct bt_mesh_msg_ctx *ctx);
void apply_config(uint8_t *const data, uint16_t len);