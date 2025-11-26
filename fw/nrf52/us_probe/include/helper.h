#include <bluetooth/mesh/models.h>

bool address_is_local(const struct bt_mesh_elem *elem, uint16_t addr);
void apply_config(uint8_t *const data, uint16_t len);