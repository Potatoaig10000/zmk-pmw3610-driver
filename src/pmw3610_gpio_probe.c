/*
 * Temporary PMW3610 sensor-pad GPIO level test for nice!nano v2 / ZMK.
 *
 * Purpose: make known, long-lasting logic levels that can be measured
 * with a DC multimeter at the breakout's SENSOR FOOTPRINT pads.
 *
 * Pins verified for this project:
 *   P0.08 -> SCLK, P0.17 -> SDIO, P0.20 -> NCS.
 *
 * Preconditions (keep the existing temporary GPIO probe configuration):
 *   CONFIG_PMW3610_GPIO_PROBE=y
 *   CONFIG_PMW3610_ALT=n
 *   CONFIG_SPI=n
 *   &spi0 { status = "disabled"; };
 *
 * GPIO outputs are changed only while NCS is high (except NCS itself).
 * SDIO is NEVER driven when NCS is low, avoiding contention with the IC.
 * This test does not read registers, issue sensor reset commands, or
 * enable motion output. Restore the normal driver after diagnostics.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(pmw3610_gpio_probe, LOG_LEVEL_INF);

#define SCLK_PIN 8U
#define SDIO_PIN 17U
#define NCS_PIN 20U
#define TEST_DURATION_MS 35000

static const struct device *const port0 = DEVICE_DT_GET(DT_NODELABEL(gpio0));

static int setup_idle(void)
{
    int rc;
    rc = gpio_pin_configure(port0, NCS_PIN, GPIO_OUTPUT_HIGH);
    if (rc < 0) { return rc; }
    rc = gpio_pin_configure(port0, SCLK_PIN, GPIO_OUTPUT_HIGH);
    if (rc < 0) { return rc; }
    return gpio_pin_configure(port0, SDIO_PIN, GPIO_INPUT);
}

static void rest_measurement(void)
{
    k_msleep(TEST_DURATION_MS);
}

static void probe_thread(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);

    k_msleep(1800);
    if (!device_is_ready(port0)) {
        LOG_ERR("GPIO0 is not ready");
        return;
    }

    int rc = setup_idle();
    if (rc < 0) {
        LOG_ERR("Cannot configure GPIO outputs: %d", rc);
        return;
    }

    LOG_INF("Pad-level test uses P0.20=NCS, P0.08=SCLK, P0.17=SDIO");
    LOG_INF("Each stage holds for 35 seconds. DC volts, black probe at breakout GND.");

    /* Test 29: NCS must become LOW at sensor pad 5. SDIO is an input. */
    rc = gpio_pin_set(port0, NCS_PIN, 0);
    if (rc < 0) { goto gpio_failure; }
    LOG_INF("TEST 29 NOW (35s): measure SENSOR PAD 5 NCS, expected ~0 V");
    rest_measurement();

    /* Return to deselected state before changing other signals. */
    rc = gpio_pin_set(port0, NCS_PIN, 1);
    if (rc < 0) { goto gpio_failure; }
    k_busy_wait(100);

    /* Test 30: SCLK LOW at sensor pad 3, with sensor deselected. */
    rc = gpio_pin_set(port0, SCLK_PIN, 0);
    if (rc < 0) { goto gpio_failure; }
    LOG_INF("TEST 30 NOW (35s): measure SENSOR PAD 3 SCLK, expected ~0 V");
    rest_measurement();

    /* Test 31: SCLK HIGH at sensor pad 3, with sensor deselected. */
    rc = gpio_pin_set(port0, SCLK_PIN, 1);
    if (rc < 0) { goto gpio_failure; }
    LOG_INF("TEST 31 NOW (35s): measure SENSOR PAD 3 SCLK, expected ~3.3 V");
    rest_measurement();

    /* Test 32: SDIO HIGH with NCS HIGH; sensor must be deselected. */
    rc = gpio_pin_configure(port0, SDIO_PIN, GPIO_OUTPUT_HIGH);
    if (rc < 0) { goto gpio_failure; }
    LOG_INF("TEST 32 NOW (35s): measure SENSOR PAD 2 SDIO, expected ~3.3 V");
    rest_measurement();

    /* Test 33: SDIO LOW with NCS HIGH. */
    rc = gpio_pin_set(port0, SDIO_PIN, 0);
    if (rc < 0) { goto gpio_failure; }
    LOG_INF("TEST 33 NOW (35s): measure SENSOR PAD 2 SDIO, expected ~0 V");
    rest_measurement();

    LOG_INF("Tests 29-33 complete; restoring idle pins");
    rc = setup_idle();
    if (rc < 0) {
        LOG_ERR("Could not restore idle pins: %d", rc);
    }
    return;

gpio_failure:
    LOG_ERR("GPIO operation failed: %d; restoring idle pins", rc);
    (void)setup_idle();
}

K_THREAD_DEFINE(pmw3610_gpio_probe_tid, 1536, probe_thread,
                NULL, NULL, NULL, 7, 0, 0);


