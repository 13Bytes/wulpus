#ifndef APP_I2C_H_
#define APP_I2C_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Perform the required nPM2100 configuration writes over I2C.
 *
 * Writes:
 *   0x6A <- 0x00
 *   0x68 <- 52 (decimal, i.e. 0x34)
 *   0x69 <- 0x01
 *
 * @return 0 on success, negative errno on failure.
 */
int npm2100_init_sequence(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_I2C_H_ */
