#include "helper.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/logging/log.h>

/**
 * Returns true if the specified address is an address of the local element.
 */
bool address_is_local(const struct bt_mesh_elem *elem, uint16_t addr)
{
  return elem->rt->addr == addr;
}
