/*
 * Temporary PMW3610 SDIO input-bias diagnostic for nice!nano v2.
 * Uses project-verified GPIOs: SCLK=P0.08, SDIO=P0.17, NCS=P0.20.
 *
 * Prerequisites:
 *   CONFIG_PMW3610_GPIO_PROBE=y
 *   CONFIG_PMW3610_ALT=n
 *   CONFIG_SPI=n and &spi0 { status = "disabled"; };
 *
 * A normal sensor driver must NOT operate concurrently with this test.
 * Reads registers only; does not enable motion or alter sensor settings.
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <stdint.h>

LOG_MODULE_REGISTER(pmw3610_gpio_probe, LOG_LEVEL_INF);

#define SCLK_PIN        8U
#define SDIO_PIN        17U
#define NCS_PIN         20U
#define BIT_DELAY_US    3U
#define TURNAROUND_US  20U

static const struct device *const port0 = DEVICE_DT_GET(DT_NODELABEL(gpio0));

/* Mode 3: idle clock high, address shifted most-significant bit first. */
static void send_address(uint8_t address)
{
    for (int bit = 7; bit >= 0; bit--) {
        gpio_pin_set(port0, SCLK_PIN, 0);
        gpio_pin_set(port0, SDIO_PIN, (address >> bit) & 1U);
        k_busy_wait(BIT_DELAY_US);
        gpio_pin_set(port0, SCLK_PIN, 1);
        k_busy_wait(BIT_DELAY_US);
    }
}

/* bias must be 0, GPIO_PULL_UP, or GPIO_PULL_DOWN. */
static int read_register_with_bias(uint8_t reg, gpio_flags_t bias)
{
    uint8_t value = 0;
    int rc = gpio_pin_configure(port0, SDIO_PIN, GPIO_OUTPUT_LOW);
    if (rc < 0) {
        return rc;
    }

    gpio_pin_set(port0, SCLK_PIN, 1);
    gpio_pin_set(port0, NCS_PIN, 0);
    k_busy_wait(5);
    send_address(reg & 0x7FU);

    /* Release SDIO before the sensor's response. The weak pull is only
     * enabled during the input phase, never while driving an address.
     */
    rc = gpio_pin_configure(port0, SDIO_PIN, GPIO_INPUT | bias);
    if (rc < 0) {
        goto cleanup;
    }
    k_busy_wait(TURNAROUND_US);

    for (int bit = 7; bit >= 0; bit--) {
        gpio_pin_set(port0, SCLK_PIN, 0);
        k_busy_wait(BIT_DELAY_US);
        rc = gpio_pin_get_raw(port0, SDIO_PIN);
        if (rc < 0) {
            goto cleanup;
        }
        value |= (uint8_t)((rc & 1) << bit);
        gpio_pin_set(port0, SCLK_PIN, 1);
        k_busy_wait(BIT_DELAY_US);
    }
    rc = (int)value;

cleanup:
    gpio_pin_set(port0, SCLK_PIN, 1);
    gpio_pin_set(port0, NCS_PIN, 1);
    (void)gpio_pin_configure(port0, SDIO_PIN, GPIO_INPUT);
    k_busy_wait(20);
    return rc;
}

static void probe_thread(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);

    k_msleep(1800);
    if (!device_is_ready(port0)) {
        LOG_ERR("GPIO0 not ready");
        return;
    }

    int rc = gpio_pin_configure(port0, NCS_PIN, GPIO_OUTPUT_HIGH);
    if (rc == 0) {
        rc = gpio_pin_configure(port0, SCLK_PIN, GPIO_OUTPUT_HIGH);
    }
    if (rc == 0) {
        rc = gpio_pin_configure(port0, SDIO_PIN, GPIO_INPUT);
    }
    if (rc < 0) {
        LOG_ERR("GPIO setup failed: %d", rc);
        return;
    }

    LOG_INF("SDIO bias test: SCLK=P0.08 SDIO=P0.17 NCS=P0.20 (SPI0 disabled)");
    LOG_INF("Expected: PRODUCT_ID=0x3e, inverse PRODUCT_ID=0xc1");

    const struct {
        const char *name;
        gpio_flags_t flags;
    } modes[] = {
        { "FLOAT", 0 },
        { "PULL_UP", GPIO_PULL_UP },
        { "PULL_DOWN", GPIO_PULL_DOWN },
    };

    for (int attempt = 1; attempt <= 2; attempt++) {
        for (size_t mode = 0; mode < ARRAY_SIZE(modes); mode++) {
            int product = read_register_with_bias(0x00, modes[mode].flags);
            int inverse = read_register_with_bias(0x3F, modes[mode].flags);
            if (product < 0 || inverse < 0) {
                LOG_ERR("Attempt %d %s: GPIO error: product=%d inverse=%d",
                        attempt, modes[mode].name, product, inverse);
            } else {
                LOG_INF("Attempt %d %s: PRODUCT=0x%02x INVERSE=0x%02x",
                        attempt, modes[mode].name, product, inverse);
            }
            k_msleep(50);
        }
    }

    gpio_pin_set(port0, NCS_PIN, 1);
    gpio_pin_set(port0, SCLK_PIN, 1);
    (void)gpio_pin_configure(port0, SDIO_PIN, GPIO_INPUT);
    LOG_INF("SDIO bias test complete; pins left idle");
}

K_THREAD_DEFINE(pmw3610_gpio_probe_tid, 1536, probe_thread,
                NULL, NULL, NULL, 7, 0, 0);
