/*
 * Read-only PMW3610 GPIO diagnostic for nice!nano v2 + pmw3610_test.
 * Pins are the USER-VERIFIED test wiring, not generic PMW3610 defaults:
 * P0.08=SCLK, P0.17=SDIO, P0.20=NCS.
 *
 * The normal PMW3610 driver and SPI0 MUST be disabled in this build.
 * This tests register reads only; it never initializes motion reporting.
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/devicetree.h>
#include <stdint.h>

LOG_MODULE_REGISTER(pmw3610_gpio_probe, LOG_LEVEL_INF);

#define SCLK_PIN 8U
#define SDIO_PIN 17U
#define NCS_PIN  20U
#define BIT_DELAY_US 3U
#define TURNAROUND_US 10U

static const struct device *const port0 = DEVICE_DT_GET(DT_NODELABEL(gpio0));

/* PMW3610 uses SPI mode 3: SCLK idle HIGH; sensor samples on rising
 * edges and presents each data bit after the preceding falling edge.
 * Values are intentionally clocked slowly to make timing non-critical.
 */
static void send_address(uint8_t addr)
{
    for (int bit = 7; bit >= 0; bit--) {
        gpio_pin_set(port0, SCLK_PIN, 0);
        gpio_pin_set(port0, SDIO_PIN, (addr >> bit) & 1U);
        k_busy_wait(BIT_DELAY_US);
        gpio_pin_set(port0, SCLK_PIN, 1);
        k_busy_wait(BIT_DELAY_US);
    }
}

static int gpio_read_register(uint8_t reg)
{
    int rc = gpio_pin_configure(port0, SDIO_PIN, GPIO_OUTPUT_LOW);
    if (rc < 0) {
        return rc;
    }

    gpio_pin_set(port0, SCLK_PIN, 1); /* idle high */
    gpio_pin_set(port0, NCS_PIN, 0);  /* select sensor */
    k_busy_wait(5);

    /* MSB=0 means read. */
    send_address(reg & 0x7FU);

    /* The sensor now drives the shared SDIO line. */
    rc = gpio_pin_configure(port0, SDIO_PIN, GPIO_INPUT);
    if (rc < 0) {
        gpio_pin_set(port0, NCS_PIN, 1);
        return rc;
    }
    k_busy_wait(TURNAROUND_US); /* PMW3610 tSRAD >= 4 us */

    uint8_t result = 0;
    for (int bit = 7; bit >= 0; bit--) {
        gpio_pin_set(port0, SCLK_PIN, 0);
        k_busy_wait(BIT_DELAY_US); /* allow sensor to drive bit */
        rc = gpio_pin_get(port0, SDIO_PIN);
        if (rc < 0) {
            gpio_pin_set(port0, SCLK_PIN, 1);
            gpio_pin_set(port0, NCS_PIN, 1);
            return rc;
        }
        result |= (uint8_t)((rc & 1) << bit);
        gpio_pin_set(port0, SCLK_PIN, 1);
        k_busy_wait(BIT_DELAY_US);
    }

    gpio_pin_set(port0, NCS_PIN, 1); /* release sensor */
    k_busy_wait(10);
    return (int)result;
}

static void pmw3610_probe_thread(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);

    k_msleep(1800); /* allow 3.3V and 1.8V supplies to settle */
    if (!device_is_ready(port0)) {
        LOG_ERR("GPIO0 device unavailable");
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

    LOG_INF("GPIO 3-wire probe: SCLK=P0.08 SDIO=P0.17 NCS=P0.20; SPI0 disabled");
    for (int attempt = 1; attempt <= 4; attempt++) {
        int product = gpio_read_register(0x00);
        int reverse = gpio_read_register(0x3F);
        int revision = gpio_read_register(0x01);
        int not_revision = gpio_read_register(0x3E);
        if (product < 0 || reverse < 0 || revision < 0 || not_revision < 0) {
            LOG_ERR("Attempt %d: GPIO read failed: prod=%d inv=%d rev=%d inv_rev=%d",
                    attempt, product, reverse, revision, not_revision);
        } else {
            LOG_INF("Attempt %d: PROD_ID=0x%02x (want 0x3e), NOT_PROD_ID=0x%02x (want 0xc1)",
                    attempt, product, reverse);
            LOG_INF("Attempt %d: REV_ID=0x%02x, NOT_REV_ID=0x%02x (should be complements)",
                    attempt, revision, not_revision);
        }
        k_msleep(200);
    }
    gpio_pin_set(port0, NCS_PIN, 1);
    gpio_pin_set(port0, SCLK_PIN, 1);
    (void)gpio_pin_configure(port0, SDIO_PIN, GPIO_INPUT);
    LOG_INF("GPIO probe complete; no sensor configuration or motion output was attempted");
}

K_THREAD_DEFINE(pmw3610_gpio_probe_tid, 1536, pmw3610_probe_thread,
                NULL, NULL, NULL, 7, 0, 0);
