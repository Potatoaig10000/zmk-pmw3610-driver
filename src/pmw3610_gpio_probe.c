
/*
 * PMW3610 GPIO 3-wire SPI diagnostic.
 * SCLK: P0.08
 * SDIO: P0.17
 * NCS:  P0.20
 *
 * Normal SPIM and PMW3610 driver are disabled.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <stdint.h>

LOG_MODULE_REGISTER(pmw3610_gpio_probe, LOG_LEVEL_INF);

#define SCLK_PIN 8U
#define SDIO_PIN 17U
#define NCS_PIN 20U

#define BIT_DELAY_US 3U
#define TURNAROUND_US 20U

static const struct device *const port0 =
    DEVICE_DT_GET(DT_NODELABEL(gpio0));

/* Clock idle HIGH. Send address MSB first. */
static void send_address(uint8_t address)
{
    for (int bit = 7; bit >= 0; bit--) {
        gpio_pin_set(port0, SCLK_PIN, 0);
        gpio_pin_set(
            port0, SDIO_PIN,
            (address >> bit) & 1U
        );

        k_busy_wait(BIT_DELAY_US);

        gpio_pin_set(port0, SCLK_PIN, 1);
        k_busy_wait(BIT_DELAY_US);
    }
}

static int read_register_with_bias(
    uint8_t reg,
    gpio_flags_t bias
)
{
    uint8_t value = 0;

    int rc = gpio_pin_configure(
        port0, SDIO_PIN, GPIO_OUTPUT_LOW
    );

    if (rc < 0) {
        return rc;
    }

    gpio_pin_set(port0, SCLK_PIN, 1);
    gpio_pin_set(port0, NCS_PIN, 0);

    k_busy_wait(5);
    send_address(reg & 0x7FU);

    /* Release bidirectional SDIO for reading. */
    rc = gpio_pin_configure(
        port0, SDIO_PIN, GPIO_INPUT | bias
    );

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

    (void)gpio_pin_configure(
        port0, SDIO_PIN, GPIO_INPUT
    );

    k_busy_wait(20);

    return rc;
}

static void probe_thread(
    void *a,
    void *b,
    void *c
)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);

    k_msleep(1800);

    if (!device_is_ready(port0)) {
        LOG_ERR("GPIO0 not ready");
        return;
    }

    int rc = gpio_pin_configure(
        port0, NCS_PIN, GPIO_OUTPUT_HIGH
    );

    if (rc == 0) {
        rc = gpio_pin_configure(
            port0, SCLK_PIN, GPIO_OUTPUT_HIGH
        );
    }

    if (rc == 0) {
        rc = gpio_pin_configure(
            port0, SDIO_PIN, GPIO_INPUT
        );
    }

    if (rc < 0) {
        LOG_ERR("GPIO setup failed: %d", rc);
        return;
    }

    LOG_INF(
        "SDIO bias test: SCLK=P0.08 SDIO=P0.17 NCS=P0.20"
    );
    LOG_INF(
        "Expected PRODUCT_ID=0x3e, INVERSE=0xc1"
    );

    const struct {
        const char *name;
        gpio_flags_t flags;
    } modes[] = {
        { "FLOAT", 0 },
        { "PULL_UP", GPIO_PULL_UP },
        { "PULL_DOWN", GPIO_PULL_DOWN },
    };

    for (int attempt = 1; attempt <= 2; attempt++) {
        for (size_t mode = 0;
             mode < ARRAY_SIZE(modes);
             mode++) {

            int product = read_register_with_bias(
                0x00, modes[mode].flags
            );

            int inverse = read_register_with_bias(
                0x3F, modes[mode].flags
            );

            if (product < 0 || inverse < 0) {
                LOG_ERR(
                    "Attempt %d %s: GPIO error: "
                    "product=%d inverse=%d",
                    attempt,
                    modes[mode].name,
                    product,
                    inverse
                );
            } else {
                LOG_INF(
                    "Attempt %d %s: "
                    "PRODUCT=0x%02x INVERSE=0x%02x",
                    attempt,
                    modes[mode].name,
                    product,
                    inverse
                );
            }

            k_msleep(50);
        }
    }

    gpio_pin_set(port0, NCS_PIN, 1);
    gpio_pin_set(port0, SCLK_PIN, 1);

    (void)gpio_pin_configure(
        port0, SDIO_PIN, GPIO_INPUT
    );

    LOG_INF(
        "SDIO bias test complete; pins left idle"
    );
}

K_THREAD_DEFINE(
    pmw3610_gpio_probe_tid,
    1536,
    probe_thread,
    NULL, NULL, NULL,
    7, 0, 0
);

