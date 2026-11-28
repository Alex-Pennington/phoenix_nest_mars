# DRAWS Pi station (second HF station)

A Raspberry Pi 4 with the NW Digital Radio DRAWS hat, driving a Yaesu radio
from the FT-817/818/857/897 family. The FT-857D is the worked example; the
plan does not depend on which of these radios is on the DRAWS.
It is the RF end of the second test station (two real stations on the air, or
on dummy loads).

This folder holds the plan, the Pi config files, the helpers and the hands-on
checklist. It holds no modem, waveform or ALE code. The modem is
greybeard-ale's. If this station becomes part of the appliance image, the
image side of it moves to greybeard-ale.

Hands-on steps are in [CHECKLIST.md](CHECKLIST.md).

## The role: the Pi is the RF end

The Pi is the second station's RF end, under the station controller on the
upstream host. The upstream host is munin today and Har later. Har is an HP
EliteDesk 800 G5 Mini, the second terminal. It will run the appliance,
installed by the installers only and booting to the kiosk [1].

The Pi does four things, and nothing else:

- **It plays the WAV it is handed.** The upstream host makes the burst
  (`gale-tx` on munin today) and copies the file whole to the Pi. The Pi plays
  it from its own disk, so PTT timing does not depend on the LAN [1], [2].
- **It records what the radio hears and passes it up.** `draws-rxrec` writes
  8 kHz mono files, and the upstream host pulls them off the Pi.
- **It keys PTT.** The Pi keys the radio itself, on the DRAWS hardware PTT
  line, not through gale-key. Every keying guard is on the Pi.
- **It sets the channel.** It writes the frequency its caller gives it through
  CAT and reads it back.

**The Pi is never a compute target.** No modem, no ALE, no core and no
decoding run on it [1]. Everything above the RF end runs on the upstream host.
The only processing on the Pi is the resample between the file rate (8 kHz
mono) and the card rate (48 kHz).

Keying stays a deliberate act. The TX helper refuses to key unless the file
`/etc/draws-station/tx-enabled` exists on the Pi. Each file is keyed by one
explicit order from the upstream host: there is no spool watcher, no
automatic keying and no daemon that keys. Nothing here changes the core: the
core stays pinned to gale-tx/WAV.

**Not done: streaming audio over the LAN.** It would put the network inside
the PTT timing and the 5066 ARQ turnaround. The file is moved whole instead.

## Hardware

| Part | Notes |
|---|---|
| Raspberry Pi 4, 1 GB | DRAWS supports Pi 3 and Pi 4 (vendor page). Not Pi 5. |
| DRAWS hat | TI TLV320AIC3204 codec, 2 radio ports (mini-DIN-6), GNSS with PPS, ADC, second serial port on the aux header. |
| Power | 9 to 15 V DC into the DRAWS 2.1 mm barrel (pigtail included). It powers the Pi too. Use the same 13.8 V supply as the radio so the station has one ground. Do not also power the Pi from USB-C. |
| Radio cable | DRAWS mini-DIN-6 cable (6 ft, included) to the radio's rear DATA jack. Straight through, same pinout both ends. Either DRAWS port; `draws-findport` finds which. |
| CAT cable | USB CAT cable for the radio's rear CAT/LINEAR jack (8-pin mini-DIN, TTL level). A CT-62 alone is RS-232 and needs a USB-serial adapter too. It can go in any Pi USB port; `draws-findcat` finds it. |
| GPS antenna | Optional external active antenna for the DRAWS GNSS. Gives PPS time. |
| SD card | 16 GB or more, good quality. |

The vendor page showed the DRAWS as sold out on Nov 25, 2026. This plan uses
the hat Alex already has.

## Signal path

Transmit, in order:

| Step | Where | What happens |
|---|---|---|
| 1 | upstream host | `gale-tx` writes BURST.wav (8 kHz, 16-bit, mono) |
| 2 | upstream host to Pi | `draws-send` copies the file whole over ssh into `/var/spool/draws-station/tx`, checks its SHA-256 on the Pi, then runs `draws-txwav` on exactly that file |
| 3 | Pi | sox converts to 48 kHz stereo, audio on the left channel only |
| 4 | Pi | PTT on: BCM12 high (left port) or BCM23 (right port), which grounds pin 3 of that DRAWS port |
| 5 | Pi | aplay through the DRAWS DAC to pin 1 of that port, the radio's DATA IN, DIG mode |
| 6 | Pi | PTT off: the same pin low |

Receive, in order:

| Step | Where | What happens |
|---|---|---|
| 1 | radio | DATA OUT 1200 (pin 5) into the DRAWS port's input |
| 2 | Pi | arecord at 48 kHz stereo |
| 3 | Pi | sox keeps that port's channel and writes 8 kHz mono files to the spool |
| 4 | Pi to upstream host | `draws-pull` on the upstream host copies each finished file, checks its SHA-256, then deletes from the Pi only what verified |

The DRAWS left connector is the left ALSA channel and BCM12 PTT. The right
connector is the right channel and BCM23 PTT. `DRAWS_PORT` in `station.env`
picks one, and every helper follows it. It is found at the bench, not
assumed (see "Finding the ports" below). `asound.conf` has both ports, so a
second radio can be added later.

## The radio on the DRAWS (worked example: FT-857D)

The 857D DATA jack is the same 6-pin mini-DIN as the FT-817 and FT-897D, with
the same pins and levels, so the table holds for any of them. Left DRAWS port
shown; the right port uses LOR, IN1_R, IN2_R and BCM23.

| Pin | Radio | DRAWS / ALSA | Level (Yaesu manual, 817/857/897 family) |
|---|---|---|---|
| 1 | DATA IN | LOL out (PCM, LO Driver Gain) | 40 mV p-p at the 1200 setting, 2 V p-p at 9600 |
| 2 | GND | GND | |
| 3 | PTT | BCM12 (left port) | ground to transmit |
| 4 | DATA OUT 9600 | IN1_L | 500 mV p-p, 10 kΩ. Discriminator; FM only. Not used. |
| 5 | DATA OUT 1200 | IN2_L | about 200 mV p-p, 10 kΩ. Fixed-level receive audio. Used. |
| 6 | SQL | not used | |

Radio settings for data (names as on the 857D and 897D menus; check the numbers in the
manual):

- Mode **DIG**, with DIG MODE = **USER-U** and DIG SHIFT = 0.
- **DIG VOX = 0.** VOX keying is forbidden on this project. Keying is only the
  DRAWS PTT line on pin 3. rigctld runs with `-C ptt_type=None`, so CAT
  cannot key either (see "CAT cannot key" below for why `-P NONE` alone does
  not do it).
- PKT RATE = 1200, so DATA IN is the sensitive input that matches the DRAWS
  preset.
- DIG MIC sets the data input gain. Set it with the ALC check in the
  checklist.
- DSP off (no NR, notch or DSP bandpass), NB off, speech processor off,
  AGC slow.
- CAT/LIN/TUN = CAT, so the rear jack talks CAT.
- CAT RATE = 38400, to match `RIG_BAUD` in `station.env`.
- TOT (time-out timer) on, a few minutes. A hardware backstop if the Pi ever
  leaves PTT high.

## Finding the ports

Three things are not assumed: which DRAWS port the radio is on, which USB
port the CAT cable is in, and which radio has the optional filter. The
checklist finds each at the bench.

- **DRAWS port:** `draws-findport` records both ports for 10 seconds and
  compares levels. The port with the radio carries its receive audio; the
  empty one is near silence. It writes `DRAWS_PORT`. Receive only.
- **CAT port:** `draws-findcat` lists the USB serial ports under
  `/dev/serial/by-id` (it skips the DRAWS GPS port and the Pi's own UARTs)
  and sends each one the read-only "read frequency and mode" query,
  `00 00 00 00 03`. A Yaesu radio answers with its frequency and mode. It
  writes the `/dev/serial/by-id/...` name of the port that answered into
  `RIG_DEVICE`. That name stays the same whichever USB socket the cable is in.
  It refuses if two radios answer or if a port is already open (stop
  rigctld first).

The CAT probe sends at one rate only, `RIG_BAUD` (38400), and the radio's CAT
RATE menu is set to match before it runs. It does not try other rates on
purpose: bytes sent at the wrong rate reach the radio as different bytes. A
simulation of the two UARTs showed a 9600 query read by a radio set to 4800
can arrive as 0x08, which is PTT on. If nothing answers, the fix is the
radio's menu, not another rate. `--baud` exists only to match a CAT RATE
read off the radio.

### DTR and RTS on the CAT port

Opening a USB serial port raises DTR and RTS. What can be avoided is avoided
in config; the raise at open cannot be.

- **The kernel raises both at every open.** `tty_port_block_til_ready()`
  calls `tty_port_raise_dtr_rts()` whenever the baud rate is not B0, also for
  a non-blocking open [7, `drivers/tty/tty_port.c:521-524`]. That happens
  inside `open(2)`, before any program can act. No setting in rigctld or in
  the probe removes it. Each open leaves a pulse of a few milliseconds.
- **`draws-findcat` lowers both at once** (`TIOCMBIC`, straight after
  `open(2)`, and again after it sets the port up), so its pulse is the
  kernel's alone, and they are low when it closes the port.
- **rigctld lowers both at open** with `-C rts_state=OFF,dtr_state=OFF` in
  `rigctld-draws.service`. In Hamlib 4.6.2 the two names are serial
  settings (`src/serial_cfg_params.h:28-36`), found for a serial rig such as
  the FT-857 (`src/conf.c:1411-1420`, `rigs/yaesu/ft857.c:269`), and set at
  `src/conf.c:454-500`. Hamlib lowers both straight after it opens the port
  and then waits 100 ms (`src/iofunc.c:266-285`, `:298-300`). It does not
  lower them by itself (`src/serial.c:402-403` is commented out), so without
  these two settings they stay high for as long as rigctld runs. Hamlib also
  clears HUPCL (`src/serial.c:594`), so whatever state they are in at exit
  stays. All of this is the same in 4.5.5, at other line numbers.

The FT-857D's CAT/LINEAR jack lists no RTS or DTR pin [8, p.34]. Whether the
USB CAT cable on the Pi connects either line to anything is NOT DETERMINABLE
here: it depends on that cable, and its maker's schematic would answer it. So
the checklist still has only the CAT cable plugged into the Pi while the probe
runs: an audio interface that keys on RTS would blip at every open.

## Passband: the two radios differ

110A and the 3 kHz 110D waveforms occupy about 3 kHz around an 1800 Hz
carrier. Both radios cut that short, and they do not have the same options.
Alex, 2026-11-28 [1]:

- The FT-897D has the Collins 2.8 kHz filter and a TCXO.
- The FT-857D has no optional filter and no TCXO. It is the radio on the
  DRAWS.

What this means for test 3:

- A narrow SSB filter takes the edges off the signal. The modem's equalizer
  copes at low rates; the 2400 and 4800 bps modes and short interleave feel it
  first.
- The two stations will not be mirror images. A failure in one direction only
  may be the filter, not the modem.

The checklist measures each radio's real TX passband (step 16): a swept tone
into the dummy load, received on the RSP2, which is wide and flat, on the
FT-897D with the Collins filter off and then on, and on the FT-857D as it is.
Each radio runs data with the setting that measures widest. The filter table
in the checklist holds the results. Step 16 is not done yet: the RSP2 was not
available.

## The burst path in

`draws-send BURST.wav` runs on the upstream host. It is one order for one file:

1. It copies the file over ssh into the Pi's TX spool under a hidden `.part`
   name.
2. It reads the copy's SHA-256 on the Pi and compares it with the local file.
   On a mismatch it deletes the copy and stops; nothing is keyed.
3. It renames the copy, runs `draws-txwav` on exactly that file, deletes it,
   and returns `draws-txwav`'s output and exit status.

`draws-send` never keys and decides nothing about keying. Every guard stays on
the Pi, in `draws-txwav`: the tx-enabled file, the hard time limit and its
watchdog, PTT low on every exit, one transmission at a time. Nothing on the Pi
watches the spool: a file there is sent only when an order names it.

How the upstream host reaches the Pi comes from `upstream.env` on the upstream
host, not from the code: `DRAWS_PI_HOST` (`user@host`) and
`DRAWS_PI_SSH_OPTS` (extra ssh options). The defaults are how munin logged in
on 2026-11-28: `rayven@192.168.5.216`, key file `~/.ssh/root`, and
`HostKeyAlias=draws1`, because munin's known_hosts holds the Pi's host key
under that name. Moving from munin to Har is a copy of that file and an ssh
key. The key must log in to the Pi with no prompt (BatchMode).

If ssh fails during the send order, `draws-send` exits 11, not 10. Exit 11
means the burst may have gone out: the connection can drop after the Pi has
keyed. It is never retried automatically; a person checks the radio and the
Pi first. Exit 10 is the copy stage, before any order to send, so nothing was
keyed.

Ctrl-C on the upstream host does not stop a burst in progress, and neither
does a dropped connection. The Pi carries on, and `draws-txwav` ends the
transmission at the end of the file, or at `TX_MAX_SECONDS` through its time
limit and its watchdog.

## Setting the channel

`draws-setfreq HZ` runs on the Pi. It takes a frequency from its caller, and
only a frequency. There is no channel table on the Pi: where the table of
record lives is an open point in the roadmap [1], so the caller says which
frequency. It talks to rigctld on localhost and sends exactly three commands:
`F HZ`, then `f` and `m` to read the frequency and the mode back.

- It never sets the mode. The DIG sideband is the operator's panel setting
  [1] (checklist step 9: mode DIG, DIG MODE = USER-U). Hamlib reads DIG with
  USER-U as `PKTUSB` [6, `rigs/yaesu/ft857.c:724-737`].
- It refuses, and says why, if the frequency read back is not the one asked
  for (exit 4) or the mode read back is not `PKTUSB` (exit 5). It does not put
  anything back.
- It holds `draws-txwav`'s transmit lock for the whole change: it refuses
  while a transmission is on (exit 6), and no transmission can start in the
  middle of it.
- It reads the PTT line itself, with a read-only `pinctrl get` of the pin
  `station.env` gives for `DRAWS_PORT`, and refuses unless the line reads
  `lo`, whatever keyed it (exit 7). A line it cannot read counts as high.
  `pinctrl get` prints the level after the `|`, as in
  `12: op dl pd | lo // GPIO12 = output` [9, `pinctrl/pinctrl.c:164`,
  `:176-181`].
- Hamlib keeps a set frequency in a 500 ms cache and would answer `f` from it
  without asking the radio. In 4.6.2 the default is 500 ms
  (`src/rig.c:733`). `rig_set_freq` reads the frequency back only when it is
  not a whole 100 Hz or is above 100 MHz (`:2330-2331`); otherwise it caches
  the value it asked for (`:2071`, `:2377`), and `rig_get_freq` answers from
  the cache while it is younger than the timeout (`:2548-2550`). The rigctld
  unit turns the cache off (`-C cache_timeout=0`; `src/conf.c:139`, `:712-713`,
  `src/misc.c:1988-1995`), and the helper also waits 0.6 s before it reads
  back, so the read-back is the radio's.

`PKTUSB` is also what Hamlib reads for DIG MODE = PSK31-U [6,
`rigs/yaesu/ft857.c:733`]. CAT cannot tell the two apart.

### What Hamlib sends for it (FT-857, model 1022)

The Pi runs Hamlib 4.6.2 (`rigctld Hamlib 4.6.2 2025-02-09 SHA=870364`, read
on the Pi on 2026-11-28). Everything below is from the 4.6.2 source [6],
checked against the release tag (tag 4.6.2 is commit 8703647c, the SHA the
Pi reports). It is not measured on the radio. Where 4.5.5 differs, it says so.

| Command | Frames on the CAT line |
|---|---|
| `F` (set frequency) | Read TX status `00 00 00 00 F7` (`ft857.c:840-859`), repeated up to 10 times 100 ms apart while it reads PTT on (`:988-996`); then `P1 P2 P3 P4 01`, the frequency in BCD (`:999`, `:1004`), and a 1-byte ack (`ft817.c:1520-1551`). In 4.6.2 the ack is read but its value is not checked (`:1540-1547` is `#if 0`); 4.5.5 refused a non-zero ack. The read-back is what catches a frequency that did not take |
| `f` (read frequency), `m` (read mode) | Read frequency and mode `00 00 00 00 03`, then the EEPROM read `00 78 00 00 BB` for the DIG submode (`ft857.c:517-580`, `:569`, `:487-515`). Two reads within 50 ms share one reply (`ft857.h:70`) |

`rig_set_freq` in 4.6.2 also checks for a band change. Its hook,
`rig_band_changed`, does nothing (`src/band_changed.c`), and on a band
change it asks for PTT off only if its cache says PTT is on
(`src/rig.c:2088-2101`); with PTT type None that sends nothing (below).

**No 0xBE.** Every frame `ft857.c` writes goes through one of its four
`write_block` calls (`:500`, `:555`, `:596`, `:618`), and each takes its last
byte, the opcode, from the command table `ncmd` (`:176-218`). That table has no
0xBE, and it is byte for byte the 4.5.5 table. The other four bytes are BCD
digits (at most 0x99), the EEPROM addresses 0x00, 0x68, 0x78 and 0x8C
(`:497-498`), or zero. So 0xBE cannot be in any byte of a frame for set
frequency, read frequency or read mode. 4.0, 4.5.4 and 4.5.5 are the same.

**At rigctld's start** (`src/rig.c:1486-1592`) Hamlib also reads the VFO
(EEPROM `00 68 00 00 BB`, `ft857.c:624-649`), the split state (`F7`, and
`00 8C 00 00 BB` while receiving, `ft857.c:807-838`), and VFO B's frequency
(`src/rig.c:1564`). The FT-857 backend has a `set_vfo` (`ft857.c:380`) and no
targetable VFO (`:298`), so to read VFO B the frontend toggles VFO A/B with
`00 00 00 00 81` (`ft857.c:651-665`), reads, and toggles back
(`src/rig.c:2637`, `:2655`). Inference from the source: each rigctld start
toggles the VFO twice and leaves it where it was. None of these keys, and none
is 0xBE. The FT-857 has no `get_powerstat`, so no power-on bytes are sent at
start (`src/rig.c:1432`; `ft857.c:399` has only `set_powerstat`).

**CAT cannot key, because of `ptt_type=None`, not `-P NONE`.** An earlier
version of this plan said `-P NONE` turned CAT PTT off. It does not, in 4.6.2
as in 4.5.5:

- `-P NONE` sets rigctld's `ptt_type` to `RIG_PTT_NONE`
  (`tests/rigctld.c:411-414`), which is already its default (`:256`). rigctld
  applies it to the PTT port only `if (ptt_type != RIG_PTT_NONE)` (`:711`).
- New in 4.6.2: rigctld also writes that value into the backend's caps
  (`tests/rigctld.c:665`). That does not change the PTT port, which `rig_init`
  already copied from the caps (`src/rig.c:710`, at `:653` of rigctld), and
  nothing in the library reads the caps value back for keying (only
  `rig_get_caps_int`, `src/misc.c:2682-2686`).
- So the PTT type stays the FT-857 backend's, `RIG_PTT_RIG`
  (`rigs/yaesu/ft857.c:267`). With that, a `T 1` sent to rigctld would reach
  `ft857_set_ptt` and send PTT on, `00 00 00 00 08` (`ft857.c:1135-1168`,
  `:1144`).
- `rigctld-draws.service` passes `-C ptt_type=None` (`src/conf.c:109-111`).
  rigctld applies each `-C` setting after `rig_init` (`tests/rigctld.c:666-694`).
  It sets the PTT port, the caps and the copy `rig_open` restores from, all
  to `RIG_PTT_NONE` (`src/conf.c:559`, `:599-602`, `:609`;
  `src/rig.c:972-975`). `-P NONE` is kept; it is harmless.
- With PTT type None, 4.6.2's `rig_set_ptt` calls no backend and sends
  nothing: the case is "allowed for use with VOX" and returns OK
  (`src/rig.c:3820-3822`). That is a change from 4.5.5, which returned an
  error. It still records the request in its PTT cache (`:3831-3842`).
  `rig_get_ptt` returns "not available" (`:4165-4168`).

**Watch out: 4.6.2 skips a misspelled `-C` setting.** An unknown name is logged
and ignored, not refused (`tests/rigctld.c:675-680`); a bad value for a known
name stops rigctld (`:684-689`). So the checklist checks the PTT type on the
running rigctld instead of trusting the unit file.

Checklist step 18 checks it with one read-only query, `t` (read PTT), sent
straight to rigctld on 127.0.0.1:4532:

- With PTT type None, `rig_get_ptt` returns "not available" before any
  backend call, so nothing is sent to the radio (`src/rig.c:4165-4168`), and
  rigctld answers `RPRT -11` (`tests/rigctl_parse.c:1881-1886`;
  `RIG_ENAVAIL` is 11, `include/hamlib/rig.h:202`).
- With CAT PTT still on, `t` reads TX status (`F7`, a read) and rigctld
  answers `0` (`ft857.c:840-859`; `tests/rigctl_parse.c:2693-2706`).
- The unit's `cache_timeout=0` matters here too: with the cache on, a `t`
  within 500 ms of a `T` would answer from the cache (`src/rig.c:3904-3912`).

The check talks to rigctld directly, not through `rigctl -m 2`. In 4.6.2
`rigctl` prints errors as `error = ` followed by its saved error messages
(`tests/rigctl_parse.c:1890-1895`, `src/rig.c:480-482`), so its exact text is
not fixed, and its own start sends reads through rigctld.

**Not closed by config: rigctld's raw command.** rigctld's `w` command sends
whatever bytes the client gives straight to the radio
(`tests/rigctl_parse.c:352`, `:5110`). Any program on the Pi that can reach
127.0.0.1:4532 could send any CAT byte that way, 0xBE or PTT on included.
`draws-setfreq` sends only `F`, `f` and `m`, and nothing on the Pi sends `w`.

## Levels and sample rate

- `gale-tx` writes 8 kHz, 16-bit mono (see `src/appliance-core/src/greybeard_modem.h`).
- The DRAWS runs at 48 kHz, 16-bit, the rate the vendor's own configs use.
  The helpers resample with sox (high quality), not the ALSA linear converter.
- Starting TX levels are the vendor's FT-817 preset (PCM -16.5 dB, LO Driver
  -6 dB). Final TX level is set on the radio's ALC meter: ALC just moving,
  never pinned.
- RX target: peaks around -12 dBFS in the recording, with the noise floor
  well above the bottom. Start ADC Level at -2 dB and move it in 1 dB steps.

## Time

The DRAWS GNSS gives NMEA on `/dev/ttySC0` and PPS on BCM7 (`/dev/pps0`).
gpsd plus chrony turn that into a stratum 1 clock. This matters for 3G ALE
(synchronous scanning) and for lining up logs between the two stations.
Without a GPS antenna the Pi falls back to network time.

## CPU and memory (Pi 4, 1 GB)

| Item | Memory |
|---|---|
| Debian 13 (trixie) 64-bit, idle, no desktop | not measured on this Pi; about 150 to 200 MB was the estimate for Raspberry Pi OS Lite |
| gpsd, chrony, rigctld | under 20 MB |
| sox, aplay, arecord while running | under 30 MB |

That leaves over half of the 1 GB free. Do not install the desktop. Nothing
that computes (modem, ALE, decoding) runs on the Pi, so there is no row for it.

Recording at 48 kHz stereo would write about 690 MB an hour to the SD card.
The RX helper writes 8 kHz mono instead, about 58 MB an hour, and the files
move to the upstream host.

## Storage

Audio does not stay on the Pi. The Pi keeps recordings in
`/var/spool/draws-station/rx` until the upstream host has copied them.
`draws-pull`, run on the upstream host, lists the finished files with their
SHA-256, copies each into `LOCAL_RX_DIR` (in `upstream.env`), checks the copy,
and deletes from the Pi only the files whose copy verified. A file that does
not verify stays on the Pi for the next run. `draws-rxrec` gives a file its
`rx-...wav` name only when it is whole, so a file still being written is never
taken. Nothing processes the audio on either side; where it goes after the
upstream host is not set here. It keeps TX WAVs in `/var/spool/draws-station/tx` only while
sending.

## Known problems

- **Kernel breakage.** DRAWS drivers have broken on some Pi kernels before
  (5.10.11, 5.15.61). The Pi on the bench runs Debian 13 (trixie) with kernel
  6.18.50+rpt-rpi-v8 and Hamlib 4.6.2, as read on the Pi on 2026-11-28. It is
  not the vendor's NWDR23 image (Bullseye, kernel 6.1). The `draws` overlay is
  in the Raspberry Pi kernel tree for 6.18 [4]. Check that `aplay -l` shows
  `udrc` after every kernel update, and hold the kernel if it ever breaks.
  `config.txt` is in `/boot/firmware` on trixie, where the install block
  puts the DRAWS lines.
- **GPIO numbering.** Newer kernels move sysfs GPIO numbers (the old
  `/sys/class/gpio/gpio12` scripts from the vendor fail). The helpers use
  `pinctrl`, which takes plain BCM numbers.
- **One user of the card.** Only one program can open `hw:udrc` at a time.
  `asound.conf` puts dmix and dsnoop in front so TX and RX helpers can run
  together.
- **The 120 Hz buzz** seen on munin's FT-897D receive audio is a PC sound card
  and ground issue. The DRAWS on the same 13.8 V supply as the radio avoids
  that path. Measure it on the first recording; do not assume it.

## Where this sits in the station topology

The DRAWS Pi is the RF end of the second station: the edge of the Physical
layer, below the modem and below ALE [2]. It is the only thing that keys the
radio on the DRAWS. Above it is the upstream host (munin today, Har later),
which runs everything that computes. The Pi holds no permanent data.

Open points about the RF end (keying by CAT or GPIO, who writes CAT, where the
channel table lives, the names) are asked when stage 2 of the roadmap is done
[1]. Until then the Pi keys on the hardware line only and takes a frequency,
not a channel designation, from its caller.

## Files

| Path | What |
|---|---|
| `CHECKLIST.md` | Alex's hands-on steps, in order |
| `config/boot-config.txt` | lines to add to `/boot/firmware/config.txt` |
| `config/station.env` | DRAWS port, PTT pins, rig model, CAT port and rate, TX time limit |
| `config/upstream.env` | on the upstream host: the Pi's ssh name and the Pi's spool paths, for `draws-send` and `draws-pull` |
| `config/gpsd` and `config/chrony-draws.conf` | GPS and PPS time |
| `alsa/asound.conf` | named devices for both ports, with dmix and dsnoop |
| `alsa/yaesu-data.amixer` | starting codec levels for the 817/857/897 DATA jack, fed to `amixer -s` |
| `bin/draws-ptt` | key, unkey or show the PTT line |
| `bin/draws-txwav` | send one WAV: key, play, unkey, with a hard time limit |
| `bin/draws-send` | on the upstream host: copy one WAV to the Pi, verify it, and have the Pi send exactly that file |
| `bin/draws-setfreq` | set the frequency through rigctld and read frequency and mode back; never sets the mode |
| `bin/draws-rxrec` | record the port to 8 kHz mono files |
| `bin/draws-pull` | on the upstream host: copy the finished recordings off the Pi, verify each, delete from the Pi only what verified |
| `bin/draws-findport` | find which DRAWS port the radio is on (receive only) |
| `bin/draws-findcat` | find which USB serial port answers CAT (one read-only query) |
| `systemd/draws-ptt-safe.service` | PTT low at boot and at shutdown |
| `systemd/rigctld-draws.service` | Hamlib CAT daemon, localhost only, PTT type None, no frontend cache |
| `systemd/draws-rxrec.service` | receive recorder, off until enabled |
| `tests/dry-run.sh` | runs the helpers with no hardware; used in development |
| `tests/findcat-test.py` | the CAT finder against fake ports (pseudo-terminals) |
| `tests/fake-rigctld.py` | a stand-in for rigctld's TCP protocol, for the `draws-setfreq` tests |

## Open items

- What each radio's passband measures (checklist step 16). Not measured: the
  RSP2 was not available.
- ~~A USB CAT cable for the radio on the DRAWS: does Alex have a second one.~~
  Closed 2026-11-28: CAT works on the FT-857D (Alex: "one of the steps was
  proving the CAT works so that is done"). Steps 11, 12, 18 and 19 passed; on
  2026-11-28 the coordinator read the frequency and set it back over CAT
  (`draws-setfreq`, mode PKTUSB, ok). The cable is a Prolific USB-serial
  (`/dev/serial/by-id/usb-Prolific_Technology_Inc._USB-Serial_Controller_D-if00-port0`).
- Hamlib model numbers checked in Hamlib 4.6.2, the Pi's version
  (`include/hamlib/riglist.h:116-139` [6]): 1020 FT-817, 1041 FT-818,
  1022 FT-857, 1023 FT-897, 1043 FT-897D. Confirm with `rigctl -l` on the Pi.

## Sources

[1] Roadmap issue 18, greybeard-ale (Alex-Pennington/greybeard-ale#18): the
test stations table (Alex, 2026-11-28, on the filters and TCXOs), D17, and
section 7, "Beside the stages: the second station" and "The second station as
one terminal".

[2] Alex's summary of the second station as one terminal, given 2026-11-27,
kept on saga as `~/msc-handoff/second-station-rf-terminal-2026-11-27.md`.

[3] NW Digital Radio, n7nix repository: DRAWS config, ALSA, HF and radio
notes, `setalsa-ft817.sh`, GPS install. https://github.com/nwdigitalradio/n7nix

[4] Raspberry Pi Ltd, Linux kernel, rpi-6.18.y: overlay README and
`arch/arm/boot/dts/overlays/draws-overlay.dts`. GPL-2.0. https://github.com/raspberrypi/linux

[5] NW Digital Radio, DRAWS product page.
https://nwdigitalradio.com/products/draws%E2%84%A2

[6] Hamlib project, *Hamlib*, release 4.6.2 (tag 4.6.2, commit
8703647c8cb277cad66f5a4eaff103126b7a1b5a): `rigs/yaesu/ft857.c`,
`rigs/yaesu/ft857.h`, `rigs/yaesu/ft817.c`, `src/rig.c`, `src/conf.c`,
`src/serial_cfg_params.h`, `src/iofunc.c`, `src/serial.c`, `src/misc.c`,
`include/hamlib/riglist.h`,
`src/band_changed.c`, `tests/rigctld.c`, `tests/rigctl_parse.c`,
`include/hamlib/rig.h`. Cited by file and line. LGPL-2.1-or-later.
https://github.com/Hamlib/Hamlib/releases/tag/4.6.2

[7] Raspberry Pi Ltd, Linux kernel, rpi-6.18.y (the Pi runs 6.18):
`drivers/tty/tty_port.c`.
GPL-2.0. https://github.com/raspberrypi/linux

[8] Yaesu Musen Co., Ltd., *FT-857D Operating Manual*, cited by printed page.
On saga as `~/msc-handoff/manuals/yaesu_ft-857d_operating-manual.pdf`.

[9] Raspberry Pi Ltd, *utils* repository, `pinctrl/pinctrl.c`, commit
cf42db188fda. BSD-3-Clause. https://github.com/raspberrypi/utils
