# gale-term: core calls by screen

Every core call gale-term needs for the screens in
docs/gale-term-mm-bbs-screens.md, one screen per section. The full text of
each call is in docs/api.md; this file is the checklist to build against.

## How to call

- One JSON object per line over TCP to `127.0.0.1:4100` (gale-term's
  `--core-port 4100`). One reply line comes back.
- The request is one flat object: `{"cmd":"name","key":"value",...}`. Send
  values as strings. Yes/no is `"Y"`/`"N"` (`"true"`/`"false"` also work).
- Every reply has `ok`. When `ok` is false, `error` says why in words fit
  to show the operator on line 24.
- While training mode is on, every reply has `"training":true`: show the
  TRAINING banner on line 2.
- Ids such as `AAR9AA-17` and `PLAIN-...` are the core's message ids. Show
  them; never build one.

## Every screen: line 1, line 25, training banner

| Need | Call | Request | Reply fields used |
|---|---|---|---|
| Call sign on line 1 | `status` | — | `station` |
| Status strip, line 25 | `status` | — | `queued`, `sending`, `failed`, `unacked`, `held`, `emcon`, `link.gate`, `link.stations[]` (`station`, `state`) |
| Airtime cap on the strip | `status` | — | `cap.per_hour`, `cap.used`, `cap.room` (-1: no cap), `cap.resets_at`, `cap.sources[]` (`source`, `active`, `per_hour`, `used`, `room`, `resets_at`) |
| TRAINING banner | `training_status` | — | `on`, `since`, `samples`, `practice_station` |
| EMCON on/off (confirm, default No) | `emcon` | `on` Y/N | `emcon` |

Poll `status` every 2 s while a screen is shown.

## GUIDE (`?` on any screen)

| Key | Call | Request | Reply |
|---|---|---|---|
| `?` | `guide_help` | `screen` (the screen id), `type` (MSG-COMPOSE only: the template) | `found`, `page` (`id`, `title`, `help`, `keys[]` `{key, what}`, `text`, `example`, `see[]`, `lines[]`), `type` (a page for the template) |
| Open a page by id (from `see`) | `guide_get` | `id` | `found`, `page` as above |
| Contents | `guide_list` | — | `pages[]` (`id`, `title`, `kind`, `origin`, `screens[]`) |
| Walkthrough | `guide_start` | — | `steps[]` (`n`, `id`, `title`, `form`, `screen`, `how`, `done`), `done`, `of`, `next`, `next_title`, `next_screen` |
| Edit a written page | `guide_page_get`, `guide_page_put`, `guide_page_reset` | `id`; `text` for put | `page`; refused if the text holds a frequency |

Draw `page.lines`: they are laid out for 80x25 already.

## TRAINING

| Key | Call | Request | Reply |
|---|---|---|---|
| Training on (confirm, default No) | `training` | `on` Y | `on`, `since`, `samples`, `practice_station`, `real` (`received`, `queued`, `unacked`, `emcon`) |
| Training off | `training` | `on` N | same |
| Start the practice traffic over | `training` | `reload` Y | same |
| Show state | `training_status` | — | same |

While it is on, every screen works as usual against the practice store.
Refused in training: `station_backup`, `station_restore`, `cipher_install`,
`cipher_remove`, and any call with an `out` or `file` path.

## MSG-INBOX (`m`)

| Key | Call | Request | Reply |
|---|---|---|---|
| Open, page, toggles M/D/P | `list` | `dir` "in", `mine`, `hide_delivered`, `include_processed`, `limit`, `after_id` (the last row's id, for the next page) | `messages[]` (rows, below), `more`, `last_id` |
| Enter | → MSG-VIEW | | |
| `k` QSL | `qsl` | `id` (`send` Y only after a confirm: it keys the radio) | `sent`, `out_id`, `not_sent`, `emcon` |
| `x` processed | `mark` | `id`, `state` "processed" | `state` |
| `e` export | `export` | `id` | `folder`, `files[]` |

Row fields: `record` (#), `dtg`, `prec`, `from`, `to`, `id` (MSG ID
column), `state` (NEW / READ / QSL / PROC / DISC / DELIV), `serial`, `type`,
`core_state`, `mine`, `read`, `processed`, `discarded`, `delivered`,
`format_error`, `enciphered`, `tag`, `corrected`, `qsl[]`, `created_at`.

## MSG-PENDING (`p`)

| Key | Call | Request | Reply |
|---|---|---|---|
| Open | `pending` | `limit`, `all` (Y also lists overheard traffic) | `pending_count`, `errors_count`, `last_processed`, `pending[]` (rows), `errors[]` (`record`, `id`, `dtg`, `from`, `raw`, `errors[]`) |
| Enter | → MSG-VIEW | | |
| `x` | `mark` | `id`, `state` "processed" | `state` |
| `X` (confirm, default No) | `mark_all` | `list` "pending", `state` "processed" | `marked` |
| `d` (errors list) | `mark` | `id`, `state` "discarded" | `state` |
| `D` (confirm, default No) | `mark_all` | `list` "errors", `state` "discarded" | `marked` |

Line 3: `Pending: <pending_count>   Errors: <errors_count>   Last processed: <last_processed>`.

## MSG-VIEW

| Key | Call | Request | Reply |
|---|---|---|---|
| Open | `get` | `id`, `read` Y | `messages[0]` (`body` for TEXT, `state`, `from`, `to`, `prec`), `inbox[0]` (the row: header, QSL list, delivered, tag), `events[]` (`seq`, `ts`, `text`) for EVENTS, `props` for PROPS, `decoded` and `decoded_note` for DECODED, `corrected_text` |
| `t` tag | `tag` | `id`, `text` (one line, 80 at most; empty clears) | `tag` |
| `E` edit, save | `correct` | `id`, `body` (empty removes the copy) | `corrected` |
| `k` QSL | `qsl` | `id`, `send` | as in MSG-INBOX |
| `x` / `d` | `mark` | `id`, `state` "processed" / "discarded" | `state` |
| `e` export | `export` | `id` | `folder`, `files[]` |
| `y` copy groups | — | gale-term's own clipboard, from `body` | |
| `T` transmit, after `y` on the confirm panel | `retransmit` | `id`, `to`, `ale`, `de` (blank or this station), `key` (an indicator or net name from `keys_list`), `compress`, `lines` ("3-5,9"), `prec`, `route`, `confirm` "Y" | `out_id`, `to`, `key`, `route`, `route_note`, `emcon`, `compress` |

`props`: `precedence`, `compress`, `encrypt`, `key` (the indicator only),
`source`, `destination`, `ale`, `data_rate`, `interleave` (both empty: the
modem's), `checksum` (`pass` / `fail` / `none`).

The confirm panel's "This will key the radio on <route>" takes `route`
from `status.modem` before the call. Route pick list: `route_list` →
`rules[].route`. Key pick list: `keys_list`. `retransmit` without
`confirm` "Y" is refused.

## MSG-OUTBOX (`o`)

| Key | Call | Request | Reply |
|---|---|---|---|
| Waiting to go | `queue` | `limit` | `messages[]` (`id`, `to`, `prec`, `state`, `attempts`, `sends`, `waiting_for_link`, `last_error`) |
| On air / sent / failed | `list` | `dir` "out", `limit`, `after_id` | rows; `core_state` is `queued`, `sending`, `sent`, `acked`, `failed`, `held`, `cancelled`, `expired` |
| Hold / release / cancel / requeue | `hold`, `release`, `cancel`, `requeue` | `id` | `id`, `state` |

## MSG-COMPOSE (`c`)

| Key | Call | Request | Reply |
|---|---|---|---|
| Pick a template | `compose_templates` | — | `templates[]` (`id`, `title`, `summary`, `procedure`, `prec`, `procedures[]`), `procedures[]` (`id`, `title`, `available`, `why_not`) |
| The form | `compose_form` | `template`, `procedure` | `fields[]` (`key`, `label`, `kind`, `required`, `default`, `hint`, `example`, `choices[]`) |
| Check as typed | `compose_check` | `template`, `procedure`, each field by `key` | `ready`, `issues[]` (`field`, `level`, `text`, `fix`, `example`), `preview`, `to_targets[]` |
| Send (confirm, default No) | `compose_send` | same as check, plus `key` for codress, `hold` Y | `id`, `ids[]`, `to_targets[]`, `refused[]`, `codress_key` |
| Draft from MSG-REPORTS | — | `report` → `draft` (`template` FREE_FORM, `prec`, `subject`, `text`): fill the form with it; nothing is sent | |

## MSG-SEARCH (`s`)

| Key | Call | Request | Reply |
|---|---|---|---|
| Search | `search` | `sender`, `serial`, `dtg` (`%` wildcard in all three), `recipient`, `types` ("SITREP,..."), `filters` ("delivered,unprocessed,processed,discarded"), `expr`, `limit` | `messages[]` (rows), `filter` (line 22), `more` |
| Enter | → MSG-VIEW | | |

`expr`: `field op value` with `AND`, `OR`, `NOT`, parentheses. Fields
`from, to, serial, dtg, type, prec, state, tag, id, key, text`; ops `=`,
`!=`, `~` (contains); quote values with spaces. A bad one comes back
`ok` false with the character where it went wrong: show it on line 24.

## MSG-ACTIVITY (`a`)

| Key | Call | Request | Reply |
|---|---|---|---|
| Counters line | `activity_counters` | — | `watched_routes`, `routing`, `stored`, `received_session`, `processed_session`, `duplicates_session`, `purged_session`, `session_since` |
| IN/OUT tab | `log` | `stream` "io", `after`, `limit` | `events[]` (`seq`, `ts`, `id`, `text`, `dir`, `route`), `last` |
| APP tab | `log` | `stream` "app", `after`, `limit` | same, no `dir` |
| EVENTS tab, with its search box | `log` | `stream` "msg", `q`, `after`, `limit` | same |

Poll every 2 s with `after` = the last reply's `last`. `c` clears the view
only; the core keeps the log.

## MSG-REPORTS (`r`)

| Key | Call | Request | Reply |
|---|---|---|---|
| `1` station log | `report` | `kind` "station_log", `from`, `to` (YYYY-MM-DD UTC), `lines_per_page` | `text`, `pages[]`, `lines`, `from`, `to` |
| `2` COMTRAFSTAT | `report` | `kind` "comtrafstat" (status now; dates unused) | same |
| `3` COMTRAFSUM | `report` | `kind` "comtrafsum", `from`, `to` | same, and `draft`: open MSG-COMPOSE with it |
| `e` export | — | gale-term writes `text` to the export folder | |

## BBS-SETUP (`b`)

| Key | Call | Request | Reply |
|---|---|---|---|
| Open | `bbs_get` | — | `machine_mode`, `suffix`, `machine_call`, `accept`, `ack`, `ack_delay_ms`, `ack_to`, `commands_ct_only`, `max_errors`, `accept_malformed`, `zee_multi`, `zee_max_bytes`, `zee_limit`, `data_rate_bps`, `duty_s`, `remote_sync`, `duplicates`, `purge_days`, `auto_mark_days`, `auto_per_hour`, `auto_sent_last_hour`, `routing`, `broadcast_ri`, `version`, `auto_send`, `auto_send_why_not` |
| Save (confirm, default No; machine mode ON has its own confirm) | `bbs_set` | any of the keys above | same as `bbs_get`, or `ok` false with nothing changed |
| Purge now: dry run | `purge` | `dry_run` Y, `days` (blank: `purge_days`) | `count`, `in`, `out`, `kept` (`pending`, `owed_qsl`, `routing_due`, `on_qru`, `part_resend`), `items[]` (`id`, `direction`, `state`, `age_days`) |
| Purge now: after `y` on "Delete <count> messages?" | `purge` | `dry_run` N, same `days` | `purged` |

Machine mode ON text: "Other stations can pull traffic from you and your
radio will answer them by itself." `auto_mark_days` is saved but not acted
on yet. Nothing is purged unless the operator runs it.

## BBS-GUARD (`g`)

| Key | Call | Request | Reply |
|---|---|---|---|
| Guard list | `guard_list` | — | `items[]` |
| Add / edit / remove (confirm) | `guard_set` | `items` (the whole list, comma separated; empty clears) | `items[]` |
| Alias map | `alias_list` | — | `aliases[]` (`ri`, `members`) |
| Add / edit | `alias_set` | `ri`, `members` ("A;B;C") | `ok` |
| Remove (confirm, default No) | `alias_del` | `ri` | `ok` |

## BBS-ROUTES (`u`)

| Key | Call | Request | Reply |
|---|---|---|---|
| Open | `route_list` | — | `routing`, `broadcast_ri`, `auto_send`, `auto_send_why_not`, `rules[]` (`id`, `pos`, `match`, `route`, `ale[]`, `key`, `sched`, `compress`, `mark_processed`), `routed[]` (`msg_id`, `rule`, `state`, `out_id`, `due`, `processed`, `why`) |
| Routing ON/OFF, broadcast RI (confirm) | `bbs_set` | `routing`, `broadcast_ri` | settings |
| `a` add / Enter edit | `route_set` | `id` (blank: new), `match`, `route`, `ale` (comma list, calling order), `key`, `sched` (blank or HHMMZ), `compress`, `mark_processed` | `id` |
| `Del` (confirm, default No) | `route_del` | `id` | `ok` |
| `+` / `-` | `route_move` | `id`, `dir` "up" / "down" (or `to`, a position) | `ok` |
| `R` re-run | `route_rerun` | `dry_run` "true" first (show `count`), then `dry_run` "false" after `y` | `count`, `items[]`, `auto_send_why_not` |

## PARTS (the resend queue for long tapes and bundles)

The screens spec has no screen id for it yet; the main screen shows the
`summary` line when `sends` is above 0.

| Key | Call | Request | Reply |
|---|---|---|---|
| Sets and requests | `parts_list` | `all` Y/N | `hold_s`, `sets[]` (`kind`, `tag`, `from`, `parts`, `have`, `state`, `missing`, `id`, `drops_in_s`), `requests[]` (`id`, `from`, `kind`, `tag`, `missing`, `states`) |
| Ask for missing parts | `parts_request` | `id` (a part of the set) | `to`, `tag`, `id`, `asked` |
| The queue | `parts_queue` | — | `summary`, `sends`, `stations`, `items[]` (`kind`, `tag`, `part`, `to`, `asked_by`), `flagged[]`, `held[]`, `per_hour`, `sent_last_hour`, `room`, `emcon` |
| Send it (confirm, default No: keys the radio) | `parts_queue_send` | `when` "NOW" or "NET", `net` | `sent[]`, `sends`, `stations`, `held`, `left_for_cap`, `room_at`, `failed`, `left` |
| Resend by hand (confirm) | `parts_resend` | `id`, or `to`, `tag`, `parts` ("2 4") | `resent[]`, `not_found`, `failed` |
