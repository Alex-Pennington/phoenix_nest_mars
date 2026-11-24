# gale-term: Message Machine screens (inbox, BBS, routing) — build spec v1.1 (v1.1: mouse added, section 1a)

Owner of this spec: MARS core coordinator (pilot). Builder: Ymir (gale-term, greybeard-ale).
Reference: Message Machine user manual (GRSS, 2020) and MultitermBasic guide, as uploaded by Alex.
Everything here is decided. Where a core call is not yet in the core, build the screen against an
adapter with the exact names below and a stub; the core thread ships the real ops.

## 0. Fix first (blocks everything)

Gangleri shows no `m` key because gale-term is not launched with `--core-port 4100`.
Add `--core-port 4100` to the gale-term launcher/service on the image and on Gangleri.
Done when: `m` appears on the main screen on Gangleri and opens MSG-INBOX.

## 1. Rules for every screen

- 80x25, keyboard only. Line 1: screen id left, station call + UTC clock right.
  Line 24: help line for this screen. Line 25: status strip (queued / on air / failed / link / EMCON).
- `?` opens the guide page for this screen id. `Esc` goes back. `F10` or `q` quits to main.
- TRAINING banner on line 2 whenever training mode is on.
- Every confirm is "No" by default; Enter on the default does nothing.
- Nothing on these screens keys the radio except an explicit TX action the operator confirms,
  or machine-mode auto-sends the operator has enabled (section 5).
- Lists: Up/Down move, PgUp/PgDn page, Enter opens, `/` filters. Selected row shown inverse.

## 1a. Mouse (added v1.1, Alex Nov 24)

The mouse is a second way in; the keyboard stays complete. Nothing is mouse-only.
- Console: tty1 has no X, so the image runs `gpm` (console mouse daemon) and gale-term uses curses mouse events
  (works the same under an xterm/SSH). If no mouse is present, nothing changes.
- Left click: on a list row selects it, double click opens it (same as Enter). On a tab label switches tab.
  On an action-bar item does that action. On a key in the main-screen key map opens that screen.
- Right click: anywhere = Esc (back).
- Wheel: scrolls lists and text (3 lines per notch).
- Hover: the item under the pointer shows its one-line help on line 24 (MultitermBasic-style tips).
- Confirms: a click can only pick No or open the confirm. Anything that keys the radio, switches machine mode or
  routing on, or discards/marks all needs the keyboard `y` (a click on Yes does nothing but flash "press y").
  This keeps a stray click from transmitting.
- Fields: click puts the cursor in the field; typing is keyboard.
- Load: gpm is a few hundred KB and idle when the mouse is still; gale-term only redraws on events.
Tests: the screen-walk test also drives each screen by synthetic mouse events (click row, tab, action, right-click back).

## 2. Key map from main screen

| Key | Screen id | What |
|---|---|---|
| m | MSG-INBOX | In Box (all received, newest first) |
| p | MSG-PENDING | Pending action queue + formatting errors |
| o | MSG-OUTBOX | Waiting to go / on air / failed (exists in current work) |
| c | MSG-COMPOSE | New message (exists) |
| s | MSG-SEARCH | Search |
| a | MSG-ACTIVITY | Activity |
| r | MSG-REPORTS | Reports |
| b | BBS-SETUP | Machine mode (BBS) settings |
| g | BBS-GUARD | Guard list + RI alias map |
| u | BBS-ROUTES | Routing rules |

## 3. Screens

### MSG-INBOX
Columns: `#  DTG  PREC  FROM  TO  MSG ID  STATE` (state: NEW / READ / QSL / PROC / DISC / DELIV).
Toggles shown on line 3: `[M]ine only  [D]elivered hidden  [P]rocessed shown` (keys M, D, P).
Enter -> MSG-VIEW. `k` = QSL selected. `x` = mark processed. `e` = export to the export folder.
Core: `list` with `{"dir":"in","mine":bool,"hide_delivered":bool,"include_processed":bool,"limit":N,"after_id":...}`.

### MSG-PENDING
Two lists stacked. Top "Pending action" (unprocessed). Bottom "Formatting errors" with the error text.
Line 3: `Pending: N   Errors: N   Last processed: <id>`.
Keys: Enter -> MSG-VIEW; `x` mark processed; `X` mark ALL processed (confirm, default No);
`d` discard (errors list only); `D` discard all errors (confirm, default No).
Core: `pending` -> `{"pending":[...],"errors":[{"id","raw","errors":[...]}]}`; `mark` `{"id","state":"processed|discarded|reviewed"}`; `mark_all` `{"list":"pending|errors","state"}`.

### MSG-VIEW (tabs with Left/Right: TEXT | DECODED | EVENTS | PROPS)
Header: record #, received, stored, message id, operator tag (editable, `t`), QSL list, delivered state.
- TEXT: message as received over the air, scrollable. `E` edit (opens editor; save writes a corrected copy, original kept).
- DECODED: our cipher only. If key not held: "No key for this message on this station."
- EVENTS: per-message event log, oldest first.
- PROPS: precedence, compress, encrypt, key name (never key material), source, destination, ALE address, data rate, interleave, checksum.
Action bar (line 23): `k QSL  x Processed  d Discard  e Export  y Copy groups  T Transmit...`
`T` opens a confirm panel: route (pick list from BBS-ROUTES streams), recipient, DE, ALE address, key, compress,
whole or selected lines. Shows "This will key the radio on <route>." Default No.
Core: `get` (adds `events`, `props`, `decoded`), `mark`, `qsl` `{"id"}`, `export` `{"id"}`, `retransmit` `{"id","route","to","de","ale","key","compress","lines"?}`, `tag` `{"id","text"}`, `correct` `{"id","body"}`.

### MSG-SEARCH
Fields: Sender contains, Serial, DTG (text, `%` wildcard allowed), Recipient contains, Type (USMTF list),
filters [Delivered][Unprocessed][Processed][Discarded]. Advanced line: free filter expression
(field op value, AND/OR, parentheses) — the core parses it; no raw SQL reaches SQLite.
Results list same columns as MSG-INBOX; Enter -> MSG-VIEW. Line 22 shows the effective filter.
Core: `search` `{"sender","serial","dtg","recipient","types","filters","expr","limit"}`.

### MSG-ACTIVITY (tabs: IN/OUT | APP | EVENTS)
Counters line: watched routes, files processed, stored, duplicates this session, purged this session.
- IN/OUT: every message and machine command received and every reply/message sent, time-stamped, with direction and route.
- APP: the core's own log.
- EVENTS: search box, then events for matching messages.
Tails live (poll every 2 s with `after`). `c` clears view only.
Core: `log` with `{"stream":"io|app|msg","after","limit","q"?}`; `activity_counters`.

### MSG-REPORTS
Pick begin and end date (UTC, typed YYYY-MM-DD or arrow keys on a month grid), lines per page.
Keys: `1` Station log, `2` COMTRAFSTAT (pending list), `3` COMTRAFSUM (period summary, opens in MSG-COMPOSE as a draft).
Output scrolls; `e` exports to file.
Core: `report` `{"kind":"station_log|comtrafstat|comtrafsum","from","to"}`.

### BBS-SETUP (machine mode)
Fields:
- Machine mode: OFF / ON (default OFF). ON shows: "Other stations can pull traffic from you and your radio will answer them by itself."
- Machine call suffix: `-CM` (fixed, shown).
- Accept from: Call signs only / Call signs and guarded RIs / All (relay node).
- Acknowledge: off / auto QSL after N ms (default 2000).
- Accept commands only on encrypted circuits: yes/no (default yes); plaintext command gets an error reply.
- Ignore message with more than N errors (default 3). Accept malformed: yes/no (default no).
- ZEE: allow multi-message; max size bytes (default computed from data rate x duty cycle; show the number).
- Allow remote sync: yes/no (default no).
- Duplicates: exact copy, or same message id + sender + recipients (FL2 re-file is not a duplicate) — fixed, shown.
- Purge processed/discarded after N days (default 30); auto-mark old processed after N days.
Save confirm, default No. Core: `bbs_get`, `bbs_set`.

### BBS-GUARD
Left: Guard list (own calls and RIs this station delivers for). Right: RI alias map (collective RI -> member list).
`a` add, `Enter` edit, `Del` remove (confirm, default No), Tab switches panes.
Core: `guard_list`, `guard_set` `{"items":"A,B,C"}`, `alias_list`, `alias_set` `{"ri","members":"A;B;C"}`, `alias_del`.

### BBS-ROUTES
Top: Enable routing ON/OFF; Broadcast RI (stripped from matched messages).
List columns: `#  MATCH  ROUTE  ALE ADDRS  KEY  SCHED  COMP  PROC`.
Edit panel: match (RI, partial RI, or callsign wildcard like UHE*), route (stream), ALE addresses (ordered, comma list),
circuit key name (our cipher keys only), schedule (none / HHMMZ), compress yes/no, mark routed as processed yes/no.
Keys: `a` add, Enter edit, `Del` remove, `+`/`-` reorder (first match wins), `R` re-run rules over unprocessed (confirm, default No; shows count first).
Core: `route_list`, `route_set`, `route_del`, `route_move`, `route_rerun` `{"dry_run":true}` then `{"dry_run":false}`.
ALE calling is greybeard-ale's: the core hands the ordered address list to the link controller; gale-term only edits it.

## 4. Guide pages
One page per screen id above, plain language, what each key does, one worked example. Training mode uses the same screens
with the training store; the TRAINING banner shows.

## 5. Principle 5 (nothing keys without the operator)
Auto-QSL, answers to QRU/ZEE/VER and routed sends key the radio. They happen only when Machine mode is ON (BBS-SETUP)
and, for routed sends, routing is ON. They obey EMCON and the per-hour airtime cap, appear in MSG-OUTBOX and IN/OUT,
and stay on the pinned transmit route until Alex unlocks transmit. Turning Machine mode ON is a confirm, default No.

## 6. Done means
Each screen: reachable by its key, guide page present, help line present, works against the stub and against the real core
once the op exists, screen-walk test covers it, runs on Gangleri.
