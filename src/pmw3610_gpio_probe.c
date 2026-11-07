
    
  
/*
 * Temporary P0.17 (PMW3610 SDIO) voltage diagnostic for nice!nano v2.
 *
 * Replaces src/pmw3610_gpio_probe.c in the diagnostic driver fork.
 * Required existing diagnostic configuration:
 *   CONFIG_PMW3610_GPIO_PROBE=y
 *   CONFIG_PMW3610_ALT=n
 *   CONFIG_SPI=n
 *   &spi0 { status = "disabled"; };
 *
 * P0.20 (NCS) is held HIGH and P0.08 (SCLK) LOW. Only P0.17 toggles.
 * Start with the nice!nano OUTSIDE the shield. Before trying the assembled
 * board, establish that NCS and SDIO are no longer shorted, unpowered.
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
#define HOLD_MS 6000

static const struct device *const port0 = DEVICE_DT_GET(DT_NODELABEL(gpio0));

static void probe_thread(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);

    /* Give USB and ZMK a chance to start; the cycle continues indefinitely. */
    k_msleep(1800);

    if (!device_is_ready(port0)) {
        LOG_ERR("GPIO0 is not ready");
        return;
    }

    int rc = gpio_pin_configure(port0, NCS_PIN, GPIO_OUTPUT_HIGH);
    if (rc < 0) {
        LOG_ERR("Cannot set P0.20 (NCS) HIGH: %d", rc);
        return;
    }

    rc = gpio_pin_configure(port0, SCLK_PIN, GPIO_OUTPUT_LOW);
    if (rc < 0) {
        LOG_ERR("Cannot set P0.08 (SCLK) LOW: %d", rc);
        return;
    }

    /* Start with SDIO released; only drive it after NCS is HIGH. */
    rc = gpio_pin_configure(port0, SDIO_PIN, GPIO_INPUT);
    if (rc < 0) {
        LOG_ERR("Cannot release P0.17 (SDIO): %d", rc);
        return;
    }

    LOG_INF("P0.17 ONLY: HIGH for %d s, LOW for %d s, repeat forever",
            HOLD_MS / 1000, HOLD_MS / 1000);
    LOG_INF("P0.20 (NCS) held HIGH, P0.08 (SCLK) held LOW; no sensor SPI reads");

    unsigned int cycle = 0;
    while (true) {
        cycle++;

        rc = gpio_pin_configure(port0, SDIO_PIN, GPIO_OUTPUT_HIGH);
        if (rc < 0) {
            LOG_ERR("Cannot drive P0.17 HIGH: %d", rc);
            break;
        }
        LOG_INF("Cycle %u: TEST 48/50 HIGH NOW - P0.17 expected ~3.3 V (%d s)",
                cycle, HOLD_MS / 1000);
        k_msleep(HOLD_MS);

        rc = gpio_pin_set(port0, SDIO_PIN, 0);
        if (rc < 0) {
            LOG_ERR("Cannot drive P0.17 LOW: %d", rc);
            break;
        }
        LOG_INF("Cycle %u: TEST 49 LOW NOW - P0.17 expected ~0 V (%d s)",
                cycle, HOLD_MS / 1000);
        k_msleep(HOLD_MS);
    }

    /* On error, stop driving SDIO; keep sensor deselected. */
    (void)gpio_pin_configure(port0, SDIO_PIN, GPIO_INPUT);
    (void)gpio_pin_set(port0, NCS_PIN, 1);
    LOG_ERR("P0.17 voltage cycle stopped because of GPIO error");
}

K_THREAD_DEFINE(pmw3610_gpio_probe_tid, 1536, probe_thread,
                NULL, NULL, NULL, 7, 0, 0);


