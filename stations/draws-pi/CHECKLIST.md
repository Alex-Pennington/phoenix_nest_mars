# DRAWS Pi station: hands-on checklist

These are the steps that need hands on the Pi or the radios. Do them in
order. Each step says what to look for. The plan and the reasons are in
[README.md](README.md).

Nothing here assumes which DRAWS port the radio is cabled to or which USB
port the CAT cable is in. Steps 10 and 11 find those out at the bench. The
filters are settled (Alex, 2026-11-28, roadmap issue 18 of greybeard-ale): the
FT-897D has the Collins 2.8 kHz filter and a TCXO, and the FT-857D has no
optional filter and no TCXO. The FT-857D is the radio on the DRAWS; an
FT-897D (or 817/818) on the DRAWS works the same way.

The Pi is the RF end. It plays the WAV it is handed, records what the radio
hears, keys PTT and sets the frequency it is given. Nothing that computes
(modem, ALE, decoding) runs on it. The upstream host (munin today, Har later)
makes the bursts and takes the recordings (README, "The role").

All transmit steps are into a dummy load, at low power. Never connect one
radio's antenna jack to the other radio. Keying is only the DRAWS PTT line:
never VOX, and CAT cannot key (rigctld runs with PTT type None; README, "CAT cannot key").

| # | Step | Look for |
|---|---|---|
| 1 | Check the filters on **both** radios, the FT-857D and the FT-897D. On each, open the optional filter entry in the menu (OP FILTER on this family; the number is in each radio's manual) and write down exactly what it shows. Fill in the filter table below. | One line per radio. Settled by Alex on 2026-11-28; the filter table below has his answer. |
| 2 | Make the SD card. Raspberry Pi Imager, Raspberry Pi 4, Raspberry Pi OS Lite (64-bit). In the settings: hostname `draws1`, your user, SSH on with your key. | Card written with no errors. |
| 3 | Build it. Power off. DRAWS on the Pi 4. Card in. Power from the 13.8 V supply into the DRAWS barrel jack (no USB-C). Ethernet to the station LAN. | Pi boots and you can `ssh draws1`. |
| 4 | Install. Run the install block below on the Pi. It ends with a reboot. | No errors. After the reboot, `aplay -l` lists `card N: udrc`. If udrc is missing, stop here and tell me. |
| 5 | Check PTT is safe. `pinctrl get 12` and `pinctrl get 23`. | Both say `op dl` (output, low). |
| 6 | Load the starting levels. `cd ~/phoenix_nest_mars/stations/draws-pi` then `amixer -c udrc -s < alsa/yaesu-data.amixer` then `sudo alsactl store`. Both DRAWS ports get the same levels. | No errors. |
| 7 | GPS time (skip if no GPS antenna yet). Antenna on, wait 10 minutes, then `chronyc sources`. | A line starting `#* PPS`. |
| 8 | Cable the radio. The DRAWS mini-DIN cable from **either** DRAWS port to the radio's rear DATA jack. The USB CAT cable from the radio's CAT jack to **any** Pi USB port. Unplug any other USB serial gadget (sound-card interface, GPS puck, Arduino) from the Pi for steps 10 and 11. | Cables seated. Only the CAT cable is in the Pi's USB. |
| 9 | Set the radio's menus: CAT/LIN/TUN = CAT, CAT RATE = 38400, DIG MODE = USER-U, DIG SHIFT = 0, **DIG VOX = 0**, PKT RATE = 1200, TOT on (a few minutes). Mode DIG. DSP, NB and processor off. AGC slow. Power low. Leave the optional filter menu as you recorded it in step 1. | Radio in DIG, USER-U on the display, CAT RATE reads 38400. |
| 10 | Find the DRAWS port. Tune the radio to a band with noise or signals. `sudo draws-findport --write /etc/draws-station/station.env`. Receive only; nothing is keyed. | It prints each port's level and `DRAWS_PORT=left` or `right`, then `Wrote DRAWS_PORT`. If it says it cannot tell, check the mini-DIN cable and try a busier band. |
| 11 | Find the CAT port. `draws-findcat --list` shows the USB serial ports. Then `sudo draws-findcat --write /etc/draws-station/station.env`. It sends each port one read-only query (read frequency and mode) at 38400 and nothing else. | `radio answered` with the frequency and mode on the radio's display, then `Wrote RIG_DEVICE`. If no radio answers: CAT/LIN/TUN = CAT, CAT RATE = 38400, radio on, cable in the CAT jack. Do not try other rates. If the radio's CAT RATE menu shows another rate, add `--baud` and that rate. |
| 12 | Start CAT. Set `RIG_MODEL` in `/etc/draws-station/station.env` to the radio on the DRAWS (`sudo nano`): 1022 FT-857, 1023 FT-897, 1043 FT-897D, 1020 FT-817, 1041 FT-818. Then `sudo systemctl enable --now rigctld-draws` and `rigctl -m 2 -r 127.0.0.1:4532 f`. | `rigctl` prints the radio's frequency. |
| 13 | Receive check. Tune to a band with signals. `arecord -D draws_rx -f S16_LE -r 48000 -c 2 -d 20 /tmp/rx.wav` then `sox /tmp/rx.wav -n remix 1 stats` (`remix 2` if step 10 said right). | `Pk lev dB` around -12 on signals. If near 0, lower ADC Level; if below -30, raise it (`alsamixer -c udrc`, 1 dB steps). Send me the stats output; I'll check it for hum. |
| 14 | PTT check, dummy load on. `sudo touch /etc/draws-station/tx-enabled` then `draws-ptt on`, look at the radio, `draws-ptt off`. PTT uses the port step 10 found. | TX lights on `on`, goes out on `off`. |
| 15 | TX level. `sox -n -r 8000 -c 1 -b 16 /tmp/tone.wav synth 10 sine 1800 vol 0.5` then `draws-txwav /tmp/tone.wav`. Watch the ALC meter. Repeat, moving DIG MIC on the radio. | ALC just moving, never pinned. Write down the DIG MIC value. |
| 16 | Passband, both radios, each filter setting. `sox -n -r 8000 -c 1 -b 16 /tmp/sweep.wav synth 20 sine 100-3800 vol 0.5` then `draws-txwav /tmp/sweep.wav`, received on the RSP2 at the same time. On the FT-897D, do it with the Collins filter off, then on. The FT-857D has no optional filter: once, as it is. The FT-897D is keyed the way test 3a keyed it. | One RSP2 recording per radio per filter setting, named for the radio and setting. I read the -6 dB edges and tell you which setting to keep on each radio. |
| 17 | Turn keying off when done: `sudo rm /etc/draws-station/tx-enabled` | `draws-ptt on` now says keying is off. |
| 18 | Update the Pi to the RF-end helpers. On the Pi: `cd ~/phoenix_nest_mars && git fetch --depth 1 origin feat/draws-rf-end && git checkout -B feat/draws-rf-end FETCH_HEAD` then `cd stations/draws-pi && sudo install -m 0755 bin/draws-ptt bin/draws-txwav bin/draws-rxrec bin/draws-findcat bin/draws-findport bin/draws-setfreq /usr/local/bin/ && sudo install -m 0644 systemd/rigctld-draws.service /etc/systemd/system/ && sudo systemctl daemon-reload && sudo systemctl restart rigctld-draws` then `rigctld --version` and `rigctl -m 2 -r 127.0.0.1:4532 f`, then the CAT PTT check `rigctl -m 2 -r 127.0.0.1:4532 t`. That last one only reads PTT; nothing is keyed. It leaves `/etc/draws-station/station.env` as the bench left it. | No errors. `rigctl ... f` prints the radio's frequency, as in step 12. `rigctl ... t` prints `get_ptt: error = Feature not available`: CAT PTT is off (README, "CAT cannot key"). If it prints `0` or `1` instead, CAT PTT is still on: stop, do no keying step, and tell me. Send me the `rigctld --version` line. |
| 19 | Frequency read-back, no keying. Radio in DIG, as step 9 left it. On the Pi: `draws-setfreq "$(rigctl -m 2 -r 127.0.0.1:4532 f)"`. It sets the frequency the radio is already on, so the dial does not move. | One line ending `"mode":"PKTUSB","ok":true}`. If it says the mode is not PKTUSB, put the radio in DIG on its panel; the helper never sets the mode. |
| 20 | Set up the upstream host (munin today). On munin: `git clone --depth 1 -b feat/draws-rf-end https://github.com/Alex-Pennington/phoenix_nest_mars ~/draws-upstream` then `install -D -m 0755 ~/draws-upstream/stations/draws-pi/bin/draws-send ~/.local/bin/draws-send && install -D -m 0755 ~/draws-upstream/stations/draws-pi/bin/draws-pull ~/.local/bin/draws-pull && install -D -m 0644 ~/draws-upstream/stations/draws-pi/config/upstream.env ~/.config/draws-station/upstream.env` then `ssh -o BatchMode=yes draws1 true && echo ssh-ok`. | `ssh-ok`. If not: `ssh-copy-id draws1` once (it asks for the Pi's password), then the ssh line again. If the Pi is not reached as `draws1`, the name goes in `DRAWS_PI_HOST` in `~/.config/draws-station/upstream.env`. |
| 21 | First burst from the upstream host, **dummy load on**, power low. On the Pi: `sudo touch /etc/draws-station/tx-enabled`. On munin: `printf 'DE AAM402 DRAWS TEST' > ~/draws-test.bin && gale-tx --waveform 110a --mode 600S --level-dbfs -10 --in ~/draws-test.bin --out ~/draws-test.wav` then `~/.local/bin/draws-send ~/draws-test.wav; echo "exit $?"`. On the Pi, when done: `sudo rm /etc/draws-station/tx-enabled`. | The radio's TX lights for the length of the burst and goes out. munin prints the Pi's `{"event":"tx",...}` line and `exit 0`. `ls /var/spool/draws-station/tx` on the Pi is empty. Without the tx-enabled file the same `draws-send` prints `keying is off` and `exit 3`. |

## Filter table (steps 1 and 16)

| Radio | Optional filter menu shows (step 1) | -6 dB edges, filter off (step 16) | -6 dB edges, filter on (step 16) |
|---|---|---|---|
| FT-857D | No optional filter (Alex, 2026-11-28) | not measured: RSP2 not available | no filter to turn on |
| FT-897D | Collins 2.8 kHz filter (Alex, 2026-11-28) | not measured: RSP2 not available | not measured: RSP2 not available |

## Install block for step 4

Run on the Pi, as your user:

```
sudo apt update
sudo apt install -y git sox alsa-utils libhamlib-utils gpsd gpsd-clients pps-tools chrony rsync
git clone --depth 1 -b feat/draws-station https://github.com/Alex-Pennington/phoenix_nest_mars ~/phoenix_nest_mars
cd ~/phoenix_nest_mars/stations/draws-pi
sudo useradd -r -G audio,gpio,dialout -s /usr/sbin/nologin draws
sudo install -d -m 0755 /etc/draws-station
sudo install -m 0644 config/station.env /etc/draws-station/station.env
sudo install -d -g audio -m 2775 /var/spool/draws-station /var/spool/draws-station/tx /var/spool/draws-station/rx
sudo install -m 0755 bin/draws-ptt bin/draws-txwav bin/draws-rxrec bin/draws-findcat bin/draws-findport bin/draws-setfreq /usr/local/bin/
sudo install -m 0644 alsa/asound.conf /etc/asound.conf
sudo install -m 0644 config/gpsd /etc/default/gpsd
sudo install -m 0644 config/chrony-draws.conf /etc/chrony/conf.d/draws.conf
sudo install -m 0644 systemd/draws-ptt-safe.service systemd/rigctld-draws.service systemd/draws-rxrec.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable draws-ptt-safe.service
sudo tee -a /boot/firmware/config.txt < config/boot-config.txt
sudo reboot
```

The receive recorder (`draws-rxrec.service`) stays off until test 3 needs it.
Keying stays off except between steps 14 and 17, and during step 21.
