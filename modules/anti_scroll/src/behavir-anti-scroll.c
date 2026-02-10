// SPDX-License-Identifier: MIT
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zmk/behavior.h>
#include <zmk/behavior-helpers.h>
#include <zmk/event_manager.h>
#include <zmk/events/sensor_event.h>
#include <zmk/keymap.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct anti_scroll_config {
    int32_t cooldown_ms;
    int32_t window_ms;
    int32_t threshold;
};

struct anti_scroll_state {
    int32_t last_events[32];
    uint8_t idx;
    bool blocked;
    int64_t block_until;
};

static struct anti_scroll_state state = {0};

static int behavior_anti_scroll_sensor_binding_pressed(struct zmk_behavior_binding *binding,
                                                       struct zmk_behavior_binding_event event,
                                                       int32_t sensor_value) {
    const struct device *dev = binding->behavior_dev;
    const struct anti_scroll_config *cfg = dev->config;

    int64_t now = k_uptime_get();

    /* 解除チェック */
    if (state.blocked && now > state.block_until) {
        state.blocked = false;
        LOG_INF("Anti-scroll: unblocked");
    }

    /* ブロック中なら無視 */
    if (state.blocked) {
        return ZMK_BEHAVIOR_OPAQUE;
    }

    /* イベント記録 */
    state.last_events[state.idx] = now;
    state.idx = (state.idx + 1) % ARRAY_SIZE(state.last_events);

    /* window 内のイベント数を数える */
    int count = 0;
    for (int i = 0; i < ARRAY_SIZE(state.last_events); i++) {
        if (now - state.last_events[i] <= cfg->window_ms) {
            count++;
        }
    }

    /* 暴走判定 */
    if (count >= cfg->threshold) {
        state.blocked = true;
        state.block_until = now + cfg->cooldown_ms;
        LOG_WRN("Anti-scroll: runaway detected, blocking for %d ms", cfg->cooldown_ms);
        return ZMK_BEHAVIOR_OPAQUE;
    }

    /* rotate-var と同じ動作を呼ぶ */
    int step = binding->param1;
    int direction = binding->param2;

    struct sensor_value val = {
        .val1 = step * direction,
        .val2 = 0,
    };

    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api anti_scroll_driver_api = {
    .sensor_binding_pressed = behavior_anti_scroll_sensor_binding_pressed,
};

static int anti_scroll_init(const struct device *dev) {
    return 0;
}

#define ANTI_SCROLL_INST(n)                                                   \
    static const struct anti_scroll_config anti_scroll_config_##n = {         \
        .cooldown_ms = DT_INST_PROP(n, cooldown_ms),                          \
        .window_ms = DT_INST_PROP(n, window_ms),                              \
        .threshold = DT_INST_PROP(n, threshold),                              \
    };                                                                         \
    DEVICE_DT_INST_DEFINE(n, anti_scroll_init, NULL, NULL,                    \
                          &anti_scroll_config_##n, POST_KERNEL,               \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                \
                          &anti_scroll_driver_api);

DT_INST_FOREACH_STATUS_OKAY(ANTI_SCROLL_INST)