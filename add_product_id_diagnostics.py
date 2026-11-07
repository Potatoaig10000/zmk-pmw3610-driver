#!/usr/bin/env python3
"""Instrument badjeff/zmk-pmw3610-driver's async Observation check.

Run from the root of a clone/fork of zmk-pmw3610-driver:
    python3 add_product_id_diagnostics.py

This changes src/pmw3610.c only. It preserves the original self-test
and Product-ID rejection behavior and leaves all sensor pin settings intact.
"""
from pathlib import Path

source = Path('src/pmw3610.c')
if not source.is_file():
    raise SystemExit('ERROR: run this script from zmk-pmw3610-driver repository root')
contents = source.read_text()
start_marker = 'static int pmw3610_async_init_check_ob1(const struct device *dev) {'
end_marker = '\nstatic int pmw3610_async_init_configure(const struct device *dev) {'
start = contents.find(start_marker)
end = contents.find(end_marker, start)
if start < 0 or end < 0 or contents.count(start_marker) != 1 or contents.count(end_marker) != 1:
    raise SystemExit('ERROR: source layout unexpected; no changes made')
new_function = '''static int pmw3610_async_init_check_ob1(const struct device *dev) {
    uint8_t observation = 0;
    uint8_t product_id = 0;
    int obs_err = pmw3610_read_reg(dev, PMW3610_REG_OBSERVATION, &observation);
    int id_err = pmw3610_read_reg(dev, PMW3610_REG_PRODUCT_ID, &product_id);

    if (obs_err) {
        LOG_ERR("DIAG: Observation read SPI error %d", obs_err);
    } else {
        LOG_INF("DIAG: Observation (0x2D) = 0x%02x", observation);
    }
    if (id_err) {
        LOG_ERR("DIAG: Product ID read SPI error %d", id_err);
    } else {
        LOG_INF("DIAG: Product ID (0x00) = 0x%02x (expected 0x%02x)",
                product_id, PMW3610_PRODUCT_ID);
    }

    /* Preserve the upstream initialization failure behavior. */
    if (obs_err) {
        LOG_ERR("Can't do self-test");
        return obs_err;
    }
    if ((observation & 0x0F) != 0x0F) {
        LOG_ERR("Failed self-test (0x%x)", observation);
        return -EINVAL;
    }
    if (id_err) {
        LOG_ERR("Cannot obtain product id");
        return id_err;
    }
    if (product_id != PMW3610_PRODUCT_ID) {
        LOG_ERR("Incorrect product id 0x%x (expecting 0x%x)!",
                product_id, PMW3610_PRODUCT_ID);
        return -EIO;
    }
    return 0;
}'''
old_function = contents[start:end]
if 'DIAG: Observation' in old_function:
    raise SystemExit('ERROR: diagnostic already present; no changes made')
if 'PMW3610_REG_OBSERVATION' not in old_function or 'PMW3610_REG_PRODUCT_ID' not in old_function:
    raise SystemExit('ERROR: unexpected upstream function; no changes made')
source.write_text(contents[:start] + new_function + contents[end:])
print('Updated src/pmw3610.c: both registers will be logged at init step 2.')
print('Self-test and Product ID validation remain enforced.')
