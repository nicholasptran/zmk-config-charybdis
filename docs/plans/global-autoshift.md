# Global auto shift — extract module, import, merge

Branch: `global-autoshift`. Status ledger:

- [x] **Phase 0 — module works in-repo.** `src/auto_shift.c` + root `CMakeLists.txt`, `Kconfig`,
  `dts/bindings/behaviors/zmk,behavior-auto-shift.yaml`, `zephyr/module.yml` (cmake/kconfig/dts_root).
  CI green on all three targets (run 42282323042); `auto_shift.c` compiles in the
  `charybdis_right` build, which is the split central (`Kconfig.defconfig:1-6`). Flashed and confirmed
  working by the user.
- [x] **Phase 1 — extract the module into its own repo.** `nicholasptran/zmk-auto-shift` @ `ddbdd26`
  (`main`): the four files byte-identical to this branch, `module.yml` `name: zmk-auto-shift` with
  only `dts_root`, README written, template placeholders and `west.yml` removed.
- [x] **Phase 2 — import the module here, delete the in-repo copy.** `a2cb83b`; CI run 42860405249
  green on all three targets, `charybdis_right` compiled `zmk-auto-shift/src/auto_shift.c`.
- [ ] **Phase 3 — merge `global-autoshift` into `main`.**

## Decisions already made

- Global, QMK-style: every plain `&kp` letter/digit/punctuation key auto shifts. No per-key `&as`
  macros (removed, along with the `NO_AS` toggle-layer workaround).
- Held past `tapping-term-ms` → one shifted *tap* (press + release); the physical release is
  swallowed. No auto-repeat of the shifted char (QMK default).
- Presses already older than the term (a mod-tap's hold binding) are not shifted, so
  `&mt LEFT_BRACKET LEFT_BRACE` hold still gives `[`.
- Only the pending key's own release, any other keycode press, or any position press resolves the
  pending key unshifted. Other keys' releases don't.
- Keys with implicit/explicit mods, or pressed while any explicit mod is held, are never shifted.
- Toggle is a behavior instance declared **in the keymap's `behaviors {}`** (so the nickcoutsos
  keymap-editor sees it): `as_tog`, `tapping-term-ms = <170>`, `enabled-on-boot`. Bound on the
  ARROW layer, top row, 6th key.
- The source file must stay added with `target_sources(app PRIVATE ...)` from the module's
  `CMakeLists.txt` — **not** `zephyr_library()`. ZMK's event subscription section is unsorted
  (`app/include/linker/zmk-events.ld`), so listener order is link order; module sources land
  before `app/CMakeLists.txt`'s `hid_listener.c`, which the listener must precede.

## Phase 1 — extract the module into its own repo

Read cold: `src/auto_shift.c`, `CMakeLists.txt`, `Kconfig`, `dts/bindings/behaviors/zmk,behavior-auto-shift.yaml`,
`zephyr/module.yml` (all on this branch).

1. New GitHub repo (name TBD by the user, e.g. `zmk-auto-shift`) with layout:
   `CMakeLists.txt`, `Kconfig`, `src/auto_shift.c`, `dts/bindings/behaviors/zmk,behavior-auto-shift.yaml`,
   `zephyr/module.yml` (`name: <repo>`, `build: cmake: .`, `kconfig: Kconfig`, `settings: dts_root: .`
   — no `board_root`), plus a README showing the keymap snippet and the toggle.
2. Copy the files unchanged; keep the CMake gate
   `(NOT CONFIG_ZMK_SPLIT) OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL` and the `target_sources(app ...)` call.

**Done when:** the new repo's default branch has those five files and `zephyr/module.yml` has no
`board_root`.

## Phase 2 — import the module here, delete the in-repo copy

Read cold: `config/west.yml`, `zephyr/module.yml`.

1. `config/west.yml`: add a remote for the user's GitHub (`url-base: https://github.com/nicholasptran`)
   and a project for the new repo, `revision: main` (same style as `zmk-pointing-acceleration`).
2. Delete root `CMakeLists.txt`, `Kconfig`, `src/`, `dts/` from this repo.
3. `zephyr/module.yml`: back to only `build: settings: board_root: .` (what `main` has).
4. Keymap stays as is (the `as_tog` node and `&as_tog` binding don't change).

**Done when:** push to `global-autoshift` → GitHub Actions build is green for `charybdis_left`,
`charybdis_right`, `settings_reset`, **and** `gh run view <id> --log | grep auto_shift.c` shows it
compiled in the `charybdis_right` job (proves it came from the imported module, not a stale copy).

## Phase 3 — merge to main

1. Flash the Phase 2 firmware, confirm tap → lowercase, hold → shifted, `&as_tog` toggles.
2. Merge `global-autoshift` into `main` (PR or fast-forward, user's call); delete this plan file in
   the merge or right after.

**Done when:** `main` contains the `west.yml` import and CI on `main` is green.
