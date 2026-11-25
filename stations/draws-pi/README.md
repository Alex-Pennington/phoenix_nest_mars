# DRAWS Pi station (second HF station)

A Raspberry Pi 4 with the NW Digital Radio DRAWS hat, driving a Yaesu FT-857D.
It is the second transmitter for test 3 (two real stations on the air, or on
dummy loads), and a model for a low-cost member station later.

This folder holds the plan, the Pi config files, three small helpers and the
hands-on checklist. It holds no modem, waveform or ALE code. The modem is
greybeard-ale's. If this station becomes part of the appliance image, the
image side of it moves to greybeard-ale.

Hands-on steps are in [CHECKLIST.md](CHECKLIST.md).

## Recommendation

1. **Phase 1, test 3: the Pi is a radio head.** munin makes the burst WAVs
   with `gale-tx`, as it does today. The Pi takes a WAV, keys the 857D on the
   DRAWS PTT line, plays it, and unkeys. The Pi also records what the 857D
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
| Power | 9 to 15 V DC into the DRAWS 2.1 mm barrel (pigtail included). It powers the Pi too. Use the same 13.8 V supply as the 857D so the station has one ground. Do not also power the Pi from USB-C. |
| Radio cable | DRAWS mini-DIN-6 cable (6 ft, included) to the 857D rear DATA jack. Straight through, same pinout both ends. |
| CAT cable | USB CAT cable for the 857D rear CAT/LINEAR jack (8-pin mini-DIN, TTL level). A CT-62 alone is RS-232 and needs a USB-serial adapter too. |
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
| 4 | Pi | PTT on: BCM12 high, which grounds pin 3 of the left DRAWS port |
| 5 | Pi | aplay through the DRAWS DAC and LOL output to pin 1, 857D DATA IN, DIG mode |
| 6 | Pi | PTT off: BCM12 low |

Receive, in order:

| Step | Where | What happens |
|---|---|---|
| 1 | 857D | DATA OUT 1200 (pin 5) into DRAWS IN2_L |
| 2 | Pi | arecord at 48 kHz stereo |
| 3 | Pi | sox keeps the left channel and writes 8 kHz mono files to the spool |
| 4 | Pi to data store | files are copied, then deleted from the Pi |

The DRAWS left connector is the left ALSA channel and BCM12 PTT. The right
connector is the right channel and BCM23 PTT. Only the left port is used here;
`asound.conf` is set up so the right port can be added later.

## FT-857D on the DRAWS

The 857D DATA jack is the same 6-pin mini-DIN as the FT-817 and FT-897D.

| Pin | 857D | DRAWS / ALSA | Level (Yaesu manual, 817/857/897 family) |
|---|---|---|---|
| 1 | DATA IN | LOL out (PCM, LO Driver Gain) | 40 mV p-p at the 1200 setting, 2 V p-p at 9600 |
| 2 | GND | GND | |
| 3 | PTT | BCM12 (left port) | ground to transmit |
| 4 | DATA OUT 9600 | IN1_L | 500 mV p-p, 10 kΩ. Discriminator; FM only. Not used. |
| 5 | DATA OUT 1200 | IN2_L | about 200 mV p-p, 10 kΩ. Fixed-level receive audio. Used. |
| 6 | SQL | not used | |

Radio settings for data (names as on the 857D menu; check the numbers in the
manual):

- Mode **DIG**, with DIG MODE = **USER-U** and DIG SHIFT = 0.
- **DIG VOX = 0.** VOX keying is forbidden on this project. Keying is only the
  PTT line on pin 3 (or CAT, never VOX).
- PKT RATE = 1200, so DATA IN is the sensitive input that matches the DRAWS
  preset.
- DIG MIC sets the data input gain. Set it with the ALC check in the
  checklist.
- DSP off (no NR, notch or DSP bandpass), NB off, speech processor off,
  AGC slow.
- CAT RATE = 38400, to match `station.env`.
- TOT (time-out timer) on, a few minutes. A hardware backstop if the Pi ever
  leaves PTT high.

## Passband: the two radios differ

110A and the 3 kHz 110D waveforms occupy about 3 kHz around an 1800 Hz
carrier. Both radios cut that short, and Alex says they do not have the same
optional filter:

- The FT-897D menu lists a "2.3K" filter. Alex thinks that is how a 2.8 kHz
  filter shows up, since the menu had no entry for it. Not confirmed.
- Which radio holds which filter is not confirmed.

What this means for test 3:

- A narrow SSB filter takes the edges off the signal. The modem's equalizer
  copes at low rates; the 2400 and 4800 bps modes and short interleave feel it
  first.
- The two stations will not be mirror images. A failure in one direction only
  may be the filter, not the modem.

So the checklist has Alex read the filter menu on both radios, and measure each
radio's real TX passband once: a swept tone into the dummy load, received on
the RSP2, which is wide and flat. Run data with the widest filter each radio
has. If the 2.3K entry is the unknown one, run with it off first, then on, and
keep the result that measures wider.

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
that keys the 857D. It is not a big brain and holds no permanent data.

## Files

| Path | What |
|---|---|
| `CHECKLIST.md` | Alex's hands-on steps, in order |
| `config/boot-config.txt` | lines to add to `/boot/firmware/config.txt` |
| `config/station.env` | PTT pins, rig model, serial device, TX time limit |
| `config/gpsd` and `config/chrony-draws.conf` | GPS and PPS time |
| `alsa/asound.conf` | named devices for the left port, with dmix and dsnoop |
| `alsa/ft857d.amixer` | starting codec levels for the 857D, fed to `amixer -s` |
| `bin/draws-ptt` | key, unkey or show the PTT line |
| `bin/draws-txwav` | send one WAV: key, play, unkey, with a hard time limit |
| `bin/draws-rxrec` | record the left port to 8 kHz mono files |
| `systemd/draws-ptt-safe.service` | PTT low at boot and at shutdown |
| `systemd/rigctld-ft857d.service` | Hamlib CAT daemon, localhost only |
| `systemd/draws-rxrec.service` | receive recorder, off until enabled |
| `tests/dry-run.sh` | runs the helpers with no hardware; used in development |

## Open items

- Which filter is in which radio (checklist step).
- A USB CAT cable for the 857D: does Alex have a second one.
- Hamlib model number: 1022 is FT-857 in current Hamlib. Confirm with
  `rigctl -l | grep 857` on the Pi.
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
