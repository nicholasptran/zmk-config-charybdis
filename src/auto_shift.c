/*
 * QMK-style global auto shift for ZMK.
 *
 * An eligible key press is captured. If it is still held after the tapping term it is re-raised
 * with an implicit LSHIFT; if it is released or any other keycode event arrives first it is
 * re-raised unshifted. Re-raising uses ZMK_EVENT_RAISE_AFTER, so only listeners after this one
 * (hold-tap, hid_listener, ...) see it and this listener can never recapture its own event.
 */

#define DT_DRV_COMPAT zmk_behavior_auto_shift

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <drivers/behavior.h>
#include <zmk/behavior.h>

#include <dt-bindings/zmk/modifiers.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/hid.h>
#include <zmk/keys.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

struct behavior_auto_shift_config {
    uint32_t tapping_term_ms;
    bool enabled_on_boot;
};

// Single global state; one behavior instance is expected.
static bool as_enabled;
static uint32_t as_tapping_term_ms = 170;

static bool pending_active;
static struct zmk_keycode_state_changed_event pending;

// Keys already sent as a shifted tap (press + release) at the tapping term, like QMK's default.
// Their physical release is swallowed. Every eligible usage is below 0x40.
static uint64_t tapped_mask;

static struct k_work_delayable as_work;

static bool as_is_eligible_key(uint16_t page, uint32_t id) {
    if (page != HID_USAGE_KEY) {
        return false;
    }

    return (id >= HID_USAGE_KEY_KEYBOARD_A && id <= HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS) ||
           (id >= HID_USAGE_KEY_KEYBOARD_MINUS_AND_UNDERSCORE &&
            id <= HID_USAGE_KEY_KEYBOARD_GRAVE_ACCENT_AND_TILDE &&
            id != 0x32 /* non-US hash */) ||
           (id >= HID_USAGE_KEY_KEYBOARD_COMMA_AND_LESS_THAN &&
            id <= HID_USAGE_KEY_KEYBOARD_SLASH_AND_QUESTION_MARK);
}

extern const struct zmk_listener zmk_listener_auto_shift;

// Send the pending press on to the listeners after us. Unshifted: just the press (its real
// release follows later). Shifted: a full shifted tap, and the real release gets swallowed.
static void as_flush_pending(bool shift) {
    if (!pending_active) {
        return;
    }

    pending_active = false;
    k_work_cancel_delayable(&as_work);

    // Raise local copies so a re-entrant capture can't overwrite what's being raised.
    struct zmk_keycode_state_changed_event down = pending;

    LOG_DBG("Flushing pending 0x%02X %s", down.data.keycode, shift ? "shifted" : "unshifted");

    if (!shift) {
        ZMK_EVENT_RAISE_AFTER(down, auto_shift);
        return;
    }

    down.data.implicit_modifiers |= MOD_LSFT;
    struct zmk_keycode_state_changed_event up = down;
    up.data.state = false;
    up.data.timestamp = k_uptime_get();

    tapped_mask |= BIT64(down.data.keycode);
    ZMK_EVENT_RAISE_AFTER(down, auto_shift);
    ZMK_EVENT_RAISE_AFTER(up, auto_shift);
}

static void as_work_handler(struct k_work *work) {
    // Same context as the listener (system workqueue), so no locking needed.
    as_flush_pending(true);
}

static int as_on_keycode(struct zmk_keycode_state_changed *ev) {
    if (!ev->state) {
        // Only the pending key's own release resolves it; other releases pass by (like QMK),
        // so rolled typing can still auto shift the second key.
        if (pending_active && ev->usage_page == pending.data.usage_page &&
            ev->keycode == pending.data.keycode) {
            as_flush_pending(false);
            return ZMK_EV_EVENT_BUBBLE;
        }

        if (ev->usage_page == HID_USAGE_KEY && ev->keycode < 64 &&
            (tapped_mask & BIT64(ev->keycode))) {
            tapped_mask &= ~BIT64(ev->keycode);
            return ZMK_EV_EVENT_HANDLED;
        }

        return ZMK_EV_EVENT_BUBBLE;
    }

    // Any other press resolves the pending key unshifted first (tap preferred).
    as_flush_pending(false);

    if (!as_enabled || !as_is_eligible_key(ev->usage_page, ev->keycode) ||
        ev->implicit_modifiers != 0 || ev->explicit_modifiers != 0 ||
        zmk_hid_get_explicit_mods() != 0) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    // A press that is already older than the term came from something that already decided it
    // was a hold (e.g. a mod-tap's hold binding). Send it as-is instead of shifting it.
    int64_t elapsed = k_uptime_get() - ev->timestamp;
    if (elapsed >= as_tapping_term_ms) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    pending = copy_raised_zmk_keycode_state_changed(ev);
    pending_active = true;
    k_work_schedule(&as_work, K_MSEC(as_tapping_term_ms - elapsed));

    return ZMK_EV_EVENT_CAPTURED;
}

static int auto_shift_listener(const zmk_event_t *eh) {
    struct zmk_keycode_state_changed *kc = as_zmk_keycode_state_changed(eh);
    if (kc != NULL) {
        return as_on_keycode(kc);
    }

    // A physical key press (mouse buttons, layer keys, anything) also resolves the pending key,
    // so it can't be overtaken on the wire.
    struct zmk_position_state_changed *pos = as_zmk_position_state_changed(eh);
    if (pos != NULL && pos->state) {
        as_flush_pending(false);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(auto_shift, auto_shift_listener);
ZMK_SUBSCRIPTION(auto_shift, zmk_keycode_state_changed);
ZMK_SUBSCRIPTION(auto_shift, zmk_position_state_changed);

static int on_auto_shift_binding_pressed(struct zmk_behavior_binding *binding,
                                         struct zmk_behavior_binding_event event) {
    as_enabled = !as_enabled;

    if (!as_enabled) {
        as_flush_pending(false);
    }

    LOG_DBG("Auto shift %s", as_enabled ? "on" : "off");
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_auto_shift_binding_released(struct zmk_behavior_binding *binding,
                                          struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static int behavior_auto_shift_init(const struct device *dev) {
    const struct behavior_auto_shift_config *cfg = dev->config;

    as_tapping_term_ms = cfg->tapping_term_ms;
    as_enabled = cfg->enabled_on_boot;
    k_work_init_delayable(&as_work, as_work_handler);
    return 0;
}

static const struct behavior_driver_api behavior_auto_shift_driver_api = {
    .binding_pressed = on_auto_shift_binding_pressed,
    .binding_released = on_auto_shift_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

#define AS_INST(n)                                                                                 \
    static const struct behavior_auto_shift_config behavior_auto_shift_config_##n = {              \
        .tapping_term_ms = DT_INST_PROP(n, tapping_term_ms),                                       \
        .enabled_on_boot = DT_INST_PROP(n, enabled_on_boot),                                       \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_auto_shift_init, NULL, NULL,                               \
                            &behavior_auto_shift_config_##n, POST_KERNEL,                          \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_auto_shift_driver_api);

DT_INST_FOREACH_STATUS_OKAY(AS_INST)

#endif // DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)
