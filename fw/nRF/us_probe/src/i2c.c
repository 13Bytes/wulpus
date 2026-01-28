#include "i2c.h"

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <errno.h>

LOG_MODULE_REGISTER(npm2100_i2c, LOG_LEVEL_INF);

#define NPM2100_NODE DT_ALIAS(pmic)

#if !DT_NODE_HAS_STATUS(NPM2100_NODE, okay)
#error "Devicetree alias 'pmic' is not defined or not okay. Add aliases { pmic = &npm2100; } in your overlay."
#endif

static const struct i2c_dt_spec pmic = I2C_DT_SPEC_GET(NPM2100_NODE);

static int npm2100_write_u8(uint8_t reg, uint8_t val)
{
    int ret = i2c_reg_write_byte_dt(&pmic, reg, val);
    if (ret) {
        LOG_ERR("nPM2100 write failed: reg 0x%02X = 0x%02X (ret %d)", reg, val, ret);
    }
    return ret;
}

int npm2100_init_sequence(void)
{
    if (!device_is_ready(pmic.bus)) {
        LOG_ERR("I2C bus not ready");
        return -ENODEV;
    }

    /* Requested writes */
    int ret;

    ret = npm2100_write_u8(0x6A, 0x00);
    if (ret) return ret;

    ret = npm2100_write_u8(0x68, 52u);   /* 52 decimal = 0x34 */
    if (ret) return ret;

    ret = npm2100_write_u8(0x69, 0x01);
    if (ret) return ret;

    LOG_INF("nPM2100 init sequence done");
    return 0;
}
