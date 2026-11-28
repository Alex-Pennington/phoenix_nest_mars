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
| 2 | upstream host to Pi | the file is copied whole over ssh into `/var/spool/draws-station/tx` |
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
| 4 | Pi to upstream host | files are copied, then deleted from the Pi |

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

That leaves over half of the 1 GB free. Do not install the desktop. Nothing
that computes (modem, ALE, decoding) runs on the Pi, so there is no row for it.

Recording at 48 kHz stereo would write about 690 MB an hour to the SD card.
The RX helper writes 8 kHz mono instead, about 58 MB an hour, and the files
move to the upstream host.

## Storage

Audio does not stay on the Pi. The Pi keeps recordings in
`/var/spool/draws-station/rx` until the upstream host has copied them, then
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

- What each radio's passband measures (checklist step 16). Not measured: the
  RSP2 was not available.
- A USB CAT cable for the radio on the DRAWS: does Alex have a second one.
- Hamlib model numbers checked in Hamlib 4.5.5: 1020 FT-817, 1041 FT-818,
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

[4] Raspberry Pi Ltd, Linux kernel, rpi-6.12.y: overlay README and
`draws-overlay.dts`. GPL-2.0. https://github.com/raspberrypi/linux

[5] NW Digital Radio, DRAWS product page.
https://nwdigitalradio.com/products/draws%E2%84%A2
