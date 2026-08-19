# Changelog

## 0.5.1 (2026-08-19)

- The update popup now asks if you want to open the download page, and Yes opens it in your
  browser. Before this you had to type the address yourself.

## 0.5.0 (2026-08-19)

- Live reload. The plugin now watches `tex_overrides` while you play. Save an edited
  `overlays.xml` and the tattoo moves in game within a second or two. New `.ytd` and `.ydd`
  files are picked up without a restart. Overwritten textures show the next time the game
  reloads that item, so take the outfit or tattoo off and put it back on to see them. When
  something cannot be applied live, the log says so.
- Crash saver. If the game crashes right after a live change, the next launch refuses to load
  the files involved and says so in the log, so one broken file cannot crash the game twice.
  Delete `_quarantine.txt` from `tex_overrides` to let them load again.

## 0.4.1 (2026-08-19)

- Dropped `.meta` files (like `shop_tattoo.meta`) now get an "ignored" line in the log instead of
  silence, wherever they sit in `tex_overrides`. They hold shop data, not looks; the README
  explains what to do instead, and reserves the pack-folder layout
  (`tex_overrides/mplowrider/shop_tattoo.meta`) for them.

## 0.4.0 (2026-08-18)

- Update check. At startup the plugin asks GitHub for the newest release number, and shows a
  small popup when a newer version is out. That is its only network use; nothing about you or
  your game is sent. Turn it off with an empty `_NO_UPDATE_CHECK` file in `tex_overrides`, or
  skip everything with `_OFF` as before.

## 0.3.0 (2026-08-18)

- Tattoos, skin, face paint, beards and other body overlays can be replaced by putting the
  `.ytd` straight into `tex_overrides`, without any folder. The file replaces the one texture
  with the same name. Any name is accepted, so custom server tattoo packs work too.
- Tattoos can be moved, resized and rotated by putting an edited `overlays.xml` into
  `tex_overrides`. Before changing anything, the plugin checks that the file still matches the
  running game, and skips the file if it does not line up.
- Added `docs/overlay_index.tsv`, a table of all 3,921 tattoos and overlays in the base game. For
  each one it lists the file that owns it, its position, size, rotation, and the texture name.
- The hook is installed earlier now, while FiveM is still loading and before the game has run any
  code, so nothing can be executing the code being patched. The old approach tried to pause all
  threads first, which FiveM blocks anyway.
- Rewrote the README and COLLECTIONS.md in plainer English.

## 0.2.0

- First public version: clothing replacement per collection folder, claim and re-assert at the
  streaming layer, full logging.
