# Keycode events only exist on the central half (or an unsplit board).
if((NOT CONFIG_ZMK_SPLIT) OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
  # target_sources(app) from a module is registered before the app's own sources, so this
  # file's event subscription lands ahead of hid_listener's in the .event_subscription section.
  target_sources_ifdef(CONFIG_ZMK_AUTO_SHIFT app PRIVATE src/auto_shift.c)
endif()
