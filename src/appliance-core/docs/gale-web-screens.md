# gale-web: operator screen as a kiosk web app — build spec v1.1 (v1.1: section 8, complexity)

Owner of this spec: MARS core coordinator (pilot, "duet"). Builder: Ymir (greybeard-ale).
Replaces gale-term as the operator screen on tty1. gale-term stays as the fallback (tty2, SSH, serial).
Decided by Alex, Nov 25 2026: "your terminal ui is pretty terrable"; "we donthave to stick with ascii terminal,
just that style"; option 1 (kiosk web screen); Vite + React like Alex's other apps; "no need to meet the current
hardware constraints. we will have the hp soon." Target hardware: the HP. Gangleri keeps gale-term until then.

Everything here is decided. The screens, screen ids, keys, core calls and safety rules are the ones in
gale-term-mm-bbs-screens.md v1.2 (same folder). This spec says only how they look and run in the browser,
and what changes from the text version. Where the two differ, this spec wins for gale-web.

## 0. Stack and layout of the code

- Repo: greybeard-ale, folder `apps/gale-web/`. Vite + React 18 + TypeScript, strict mode. No UI kit.
  Plain CSS modules plus one `theme.css` of CSS variables. State: React context + `useReducer`; no Redux.
- Font: IBM Plex Mono (OFL), bundled in `src/assets/fonts`. Nothing is loaded from the internet at runtime:
  no CDN, no Google Fonts, no analytics, no service that phones home. `npm run build` output must work with the
  network unplugged. CI check: grep `dist/` for `http://` and `https://` outside `127.0.0.1`; fail on any hit.
- Builds and tests run on munin (hosts rule). The image build runs `npm ci && npm run build` on munin and copies
  `dist/` into the image at `/opt/gale/web`. Lockfile committed. Node never runs on the appliance.
- Tests: Vitest for components and the reducer; Playwright screen walk (every key in section 3, keyboard and mouse,
  against the stub core) run headless on munin.

## 1. Look: terminal style, not ASCII

- Full screen, dark background, monospace everywhere, fixed character grid feel: rows line up, columns line up.
  Real tables, real borders (1 px), colors for state, no box-drawing characters.
- Themes (key `F12` cycles, saved in the core's station settings, not the browser):
  `amber` (default: amber on near-black), `green` (green phosphor), `day` (dark text on light, for sunlight).
- Text size: `Ctrl+=` / `Ctrl+-` / `Ctrl+0`, steps of 10 %, saved the same way. Line height 1.4. No italics.
- State colors (all themes): NEW bright, FAILED red, ON AIR flashing red dot, QSL green, HELD amber, TRAINING
  magenta band. Never color alone: every state also has its word.
- Minimum window 1280x720; designed for 1920x1080. Layout scales with text size; nothing scrolls sideways.

## 2. Frame (every screen)

Top to bottom, always present, never covered:
1. Title bar: screen id left; station call, TRAINING badge if on, UTC clock (HH:MM:SSZ, from the core's `status`) right.
2. Main area: left rail = key map (section 3, always visible, current screen highlighted, each entry clickable);
   right = the screen.
3. Action bar: the keys that work on this screen, as buttons with the key shown first (`k QSL`).
4. Help line: one-line help for the focused field or row (and for the item under the mouse).
5. Status strip: queued / on air / failed / link state / EMCON / airtime room this hour (`status.cap`) /
   tuner (when present) / core connection. Click on a counter opens its screen.
6. F-key bar: `F1 Guide  F2 Compose  F3 Inbox  F4 Pending  F5 Outbox  F6 Search  F7 Activity  F8 BBS  F9 Routes  F10 Main  F11 Resends  F12 Theme`.

No pop-up dialogs, no overlays, no browser `alert/confirm/prompt`. A confirm opens as a panel that replaces the
action bar and help line in place (section 4). The browser's own menus, back button and context menu are off.

## 3. Keys

Every key from gale-term-mm-bbs-screens.md v1.2 works the same here (single letters from the main screen:
m p o c s a r b g u e; on screens: k x X d D e E t T y R A s, `/` filter, Enter, Esc, Up/Down/PgUp/PgDn,
Left/Right for tabs, `?` guide). Added in gale-web:
- F-keys as in section 2. `Tab`/`Shift+Tab` move between panes and fields.
- Letters go to the focused text field when one has focus; `Esc` leaves the field first, then goes back.
- `Ctrl+K` command line: type a screen id or action name, Enter runs it (same safety as the key).
Mouse: as section 1a of the text spec (click selects, double click opens, right click = Esc, wheel scrolls,
hover = help line). The keyboard stays complete; nothing is mouse-only.

## 4. Safety (unchanged rules, browser form)

- Every confirm defaults to No. Enter on the default does nothing.
- Anything that keys the radio, turns Machine mode or routing on, or discards / marks all is confirmed only by the
  physical keyboard `y`. A click on Yes, a touch, or a synthetic key event does nothing but flash "press y".
  Implementation: accept only `KeyboardEvent` with `isTrusted === true`, key `y`, while the confirm panel has focus.
- The confirm panel states the act in words: "This will key the radio on <route>." plus route, recipient, key name.
- Keying goes through gale-key, and the core is its caller (docs/api.md, "Keying: the core asks gale-key"). When the
  confirm panel for a written burst opens, gale-web calls `key_prepare {msg_id}` and shows the `confirm` summary it
  returns (world, channel label, control, ID, PTT method). On the trusted `y` it calls
  `key_confirm {prepare_id, confirm:"Y"}`; on No, leaving the panel or `Esc` it calls `key_cancel {prepare_id}`. The
  prepare lasts at most `expires_s` (120 s): past it the panel closes and must be opened again. A refused prepare shows
  its `error` in the panel and offers no `y`. The token stays in the core: gale-web never sees it.
- Turning Machine mode, routing or auto-QSL on also needs gale-key's standing grant: the confirm panel calls
  `standing_prepare {mode}` (`machine`, `routing`, `auto_qsl`), shows its summary, and the `y` calls `key_confirm`.
  Turning one off calls `standing_off {mode}`. Until the grant is made, that mode's automatic sends stay written.
- A sent message is marked sent only when gale-key's result says it keyed; the reply to `key_confirm` is shown, not
  taken as the result. The status strip shows `status.keying.why` when gale-key isn't answering.
- Nothing on screen ever shows key material, passphrases or secrets; key names only.
- Transmit stays on the pinned route the core reports; the screen never picks a route the core did not offer.

## 5. Talking to the core

- The core serves gale-web itself: static files from `/opt/gale/web` and a WebSocket at `ws://127.0.0.1:4101/ws`,
  both bound to 127.0.0.1 only. Core flags: `--web-port 4101 --web-root /opt/gale/web`. Built by the core owner (duet).
- Frames are the same JSON objects as the line API on 4100, one object per WebSocket text frame. The client adds
  `"rid": <int>` to each request; the core echoes `rid` in the reply. Any reply without `rid` is a push (none yet).
- Client module `src/core/api.ts` exposes one typed function per core call named in the text spec
  (`list`, `pending`, `mark`, `mark_all`, `get`, `qsl`, `export`, `retransmit`, `tag`, `correct`, `search`, `log`,
  `activity_counters`, `report`, `bbs_get`, `bbs_set`, `guard_list`, `guard_set`, `alias_list`, `alias_set`,
  `alias_del`, `route_list`, `route_set`, `route_del`, `route_move`, `route_rerun`, `parts_queue`,
  `parts_queue_send`, `parts_queue_drop`, `status`, `key_prepare`, `key_confirm`, `key_cancel`, `standing_prepare`,
  `standing_off`, `key_status`, plus compose/outbox calls gale-term already uses).
  Same names, same fields as the text spec. A stub (`src/core/stub.ts`) answers all of them from fixtures for
  dev and Playwright; `?stub=1` selects it in dev builds only.
- Polling: `status` every 1 s; activity tails every 2 s with `after`; lists refresh after any action that changes them.
- Connection lost: the status strip shows CORE DOWN in red, screens keep the last data greyed, reconnect every 2 s.
  No action that changes anything is allowed while down.

## 6. Kiosk on the image

- tty1: `cage -- chromium --kiosk --app=http://127.0.0.1:4101/ --no-first-run --noerrdialogs
  --disable-translate --disable-features=Translate,MediaRouter --disable-pinch --overscroll-history-navigation=0
  --user-data-dir=/var/lib/gale/chromium` started by a systemd unit after the core, restarted on exit.
- tty2: gale-term as today (fallback, `--core-port 4100`). SSH and serial still get gale-term.
- No other apps, no desktop, no browser UI. Ctrl+Alt+F2 reaches gale-term.

## 7. Build order (each step: screens + guide pages + walk test, then deploy)

- W0 Frame: title bar, key-map rail, action bar, help line, status strip, F-key bar, themes, text size, stub core,
  WebSocket client, CORE DOWN handling. Done when the frame runs on munin against the stub and the real core.
- W1 Message Machine: MSG-INBOX, MSG-VIEW (tabs), MSG-PENDING, MSG-OUTBOX, MSG-COMPOSE.
- W2 MSG-SEARCH, MSG-ACTIVITY, MSG-REPORTS, RESENDS.
- W3 BBS-SETUP, BBS-GUARD, BBS-ROUTES, TUNER (from at200pc-tuner.md when its calls land).
- W4 Guide pages for every screen id, training mode banner and store.
- W5 Kiosk unit in the image, first boot on the HP.

## 8. Member complaint about MSC: complexity (Alex, Nov 25)

Complexity was the complaint. Every screen is checked against these rules, and the Playwright walk tests them:
- Two layers. The default "Operate" layer shows only what a member needs on a normal net: inbox, read, compose,
  send, outbox, status. Everything else (BBS, guard, routes, reports, search filters, tuner, resends) sits under
  a "Station" layer behind one key (`F8`), labelled as setup.
- Every field has a default that works. A member can send a correct message by filling only To, Subject and Text;
  precedence, classification, DTG, serial, route and key are filled for them and shown, not asked.
- One decision per panel. No screen asks more than one question at a time; the guide tells them the next step.
- Plain words first, MARS terms second ("Waiting to send (OUTBOX)").
- Walk test: a first-time member sends one message from power-on using only the Operate layer, under 10 keystrokes
  beyond typing the text, with no guide page opened.

## 9. Done means

Each screen: reachable by its key and its rail entry, guide page present, help line present, works against the stub
and the real core, Playwright walk covers keyboard and mouse, no network calls in `dist/`, runs in the kiosk on the HP.
