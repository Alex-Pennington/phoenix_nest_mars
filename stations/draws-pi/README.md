# DRAWS Pi station (second HF station)

A Raspberry Pi 4 with the NW Digital Radio DRAWS hat, driving a Yaesu radio
from the FT-817/818/857/897 family. The FT-857D is the worked example; the
plan does not depend on which of these radios is on the DRAWS.
It is the second transmitter for test 3 (two real stations on the air, or on
dummy loads), and a model for a low-cost member station later.

This folder holds the plan, the Pi config files, five small helpers and the
hands-on checklist. It holds no modem, waveform or ALE code. The modem is
greybeard-ale's. If this station becomes part of the appliance image, the
image side of it moves to greybeard-ale.

Hands-on steps are in [CHECKLIST.md](CHECKLIST.md).

## Recommendation

1. **Phase 1, test 3: the Pi is a radio head.** munin makes the burst WAVs
   with `gale-tx`, as it does today. The Pi takes a WAV, keys the radio on the
   DRAWS PTT line, plays it, and unkeys. The Pi also records what the radio
   hears and hands the recordings to the data store. No modem runs on the Pi.
2. **Phase 2: the modem runs on the Pi.** When greybeard-ale has an aarch64
   build of `gale-tx`/`gale-rx`, they run on the Pi itself. A Pi 4 with 1 GB
   has the CPU and memory for a 110A/110D 3 kHz modem.
3. **Not recommended: streaming audio over the LAN to munin.** It adds jitter
   and a network dependency to PTT timing and to 5066 ARQ turnaround, and
   gains nothing over running the modem on the Pi.

Keying stays a deliberate act. The TX helper refuses to key unless the file
`/etc/draws-station/tx-enabled` exists on the Pi. Nothing here changes the
core: the core stays pinned to gale-tx/WAV.

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
| 1 | munin | `gale-tx` writes BURST.wav (8 kHz, 16-bit, mono) |
| 2 | munin to Pi | rsync over ssh into `/var/spool/draws-station/tx` |
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
| 4 | Pi to data store | files are copied, then deleted from the Pi |

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
  DRAWS PTT line on pin 3. rigctld runs with `-P NONE`, so CAT cannot key
  either.
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

Opening a serial port briefly raises DTR and RTS on many USB adapters. The
probe drops both at once. Yaesu CAT cables don't use them, but an audio
interface that keys on RTS would blip, so the checklist has only the CAT
cable plugged into the Pi while the probe runs.

## Passband: the two radios differ

110A and the 3 kHz 110D waveforms occupy about 3 kHz around an 1800 Hz
carrier. Both radios cut that short, and Alex says they do not have the same
optional filter:

- Alex has seen a "2.3K" entry on one radio's filter menu and thinks it may
  be how a 2.8 kHz filter shows up. Which radio, and what it really is, are
  not confirmed.
- Neither radio is assumed to have the extra filter. The checklist reads the
  filter menu on both.

What this means for test 3:

- A narrow SSB filter takes the edges off the signal. The modem's equalizer
  copes at low rates; the 2400 and 4800 bps modes and short interleave feel it
  first.
- The two stations will not be mirror images. A failure in one direction only
  may be the filter, not the modem.

So the checklist has Alex read the filter menu on both radios, and measure each
radio's real TX passband: a swept tone into the dummy load, received on the
RSP2, which is wide and flat, with the optional filter off and then on on any
radio that has one. Each radio runs data with the setting that measures
widest. The filter table in the checklist holds the results.

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
| Raspberry Pi OS Lite 64-bit, idle, no desktop | about 150 to 200 MB |
| gpsd, chrony, rigctld | under 20 MB |
| sox, aplay, arecord while running | under 30 MB |
| Phase 2: gale modem, 3 kHz, 8 kHz sample rate | well under 100 MB (estimate) |

That leaves over half of the 1 GB free. Do not install the desktop. One Pi 4
core is plenty for one 3 kHz modem channel.

Recording at 48 kHz stereo would write about 690 MB an hour to the SD card.
The RX helper writes 8 kHz mono instead, about 58 MB an hour, and the files
move to the data store.

## Storage

Audio lives permanently only on the data store. The Pi keeps recordings in
`/var/spool/draws-station/rx` until they are copied to the data store, then
deletes them. It keeps TX WAVs in `/var/spool/draws-station/tx` only while
sending.

## Known problems

- **Kernel breakage.** DRAWS drivers have broken on some Pi kernels before
  (5.10.11, 5.15.61). The vendor's last image (NWDR23, April 2023) is Bullseye
  with kernel 6.1. The `draws` overlay is still in the Raspberry Pi kernel tree
  for 6.12 and 6.18. Plan: current Raspberry Pi OS Lite 64-bit, check
  `aplay -l` shows `udrc` after every kernel update, and hold the kernel if it
  ever breaks.
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

For test 3 the DRAWS Pi is a second station's terminal: it is the only thing
that keys the radio on the DRAWS. It is not a big brain and holds no permanent data.

## Files

| Path | What |
|---|---|
| `CHECKLIST.md` | Alex's hands-on steps, in order |
| `config/boot-config.txt` | lines to add to `/boot/firmware/config.txt` |
| `config/station.env` | DRAWS port, PTT pins, rig model, CAT port and rate, TX time limit |
| `config/gpsd` and `config/chrony-draws.conf` | GPS and PPS time |
| `alsa/asound.conf` | named devices for both ports, with dmix and dsnoop |
| `alsa/yaesu-data.amixer` | starting codec levels for the 817/857/897 DATA jack, fed to `amixer -s` |
| `bin/draws-ptt` | key, unkey or show the PTT line |
| `bin/draws-txwav` | send one WAV: key, play, unkey, with a hard time limit |
| `bin/draws-rxrec` | record the port to 8 kHz mono files |
| `bin/draws-findport` | find which DRAWS port the radio is on (receive only) |
| `bin/draws-findcat` | find which USB serial port answers CAT (one read-only query) |
| `systemd/draws-ptt-safe.service` | PTT low at boot and at shutdown |
| `systemd/rigctld-draws.service` | Hamlib CAT daemon, localhost only, no PTT |
| `systemd/draws-rxrec.service` | receive recorder, off until enabled |
| `tests/dry-run.sh` | runs the helpers with no hardware; used in development |
| `tests/findcat-test.py` | the CAT finder against fake ports (pseudo-terminals) |

## Open items

- Which radio, if either, has the optional filter, and what each measures
  (checklist steps 1 and 16).
- A USB CAT cable for the radio on the DRAWS: does Alex have a second one.
- Hamlib model numbers checked in Hamlib 4.5.5: 1020 FT-817, 1041 FT-818,
  1022 FT-857, 1023 FT-897, 1043 FT-897D. Confirm with `rigctl -l` on the Pi.
- gale-rx's input format for Phase 2. This plan assumes 8 kHz mono, the same
  as gale-tx's output.
- The aarch64 build of gale-tx and gale-rx is greybeard-ale's to make.

## Sources

- NW Digital Radio n7nix repository: DRAWS config, ALSA, HF and radio notes,
  `setalsa-ft817.sh`, GPS install
  (https://github.com/nwdigitalradio/n7nix)
- Raspberry Pi kernel overlay README and `draws-overlay.dts`, rpi-6.12.y
  (https://github.com/raspberrypi/linux)
- DRAWS product page (https://nwdigitalradio.com/products/draws%E2%84%A2)
