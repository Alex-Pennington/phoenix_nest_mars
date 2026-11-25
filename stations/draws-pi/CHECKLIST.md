# DRAWS Pi station: hands-on checklist

These are the steps that need hands on the Pi or the radios. Do them in
order. Each step says what to look for. The plan and the reasons are in
[README.md](README.md).

All transmit steps are into a dummy load, at low power. Never connect one
radio's antenna jack to the other radio.

| # | Step | Look for |
|---|---|---|
| 1 | Check the filters. On the FT-857D and the FT-897D, open the menu entry for the optional filter (OP FILTER in the menu list) and write down what each radio shows as installed. | One line per radio, e.g. "897D: 2.3K listed". This settles which radio has the extra filter. |
| 2 | Make the SD card. Raspberry Pi Imager, Raspberry Pi 4, Raspberry Pi OS Lite (64-bit). In the settings: hostname `draws1`, your user, SSH on with your key. | Card written with no errors. |
| 3 | Build it. Power off. DRAWS on the Pi 4. Card in. Power from the 13.8 V supply into the DRAWS barrel jack (no USB-C). Ethernet to the station LAN. | Pi boots and you can `ssh draws1`. |
| 4 | Install. Run the install block below on the Pi. It ends with a reboot. | No errors. After the reboot, `aplay -l` lists `card N: udrc`. If udrc is missing, stop here and tell me. |
| 5 | Check PTT is safe. `pinctrl get 12` and `pinctrl get 23`. | Both say `op dl` (output, low). |
| 6 | Load the starting levels. `cd ~/phoenix_nest_mars/stations/draws-pi` then `amixer -c udrc -s < alsa/ft857d.amixer` then `sudo alsactl store`. | No errors. |
| 7 | GPS time (skip if no GPS antenna yet). Antenna on, wait 10 minutes, then `chronyc sources`. | A line starting `#* PPS`. |
| 8 | Cable the 857D. DRAWS **left** mini-DIN port to the 857D rear DATA jack. USB CAT cable to the 857D CAT jack and a Pi USB port. | Cables seated. |
| 9 | Set the 857D menus: DIG MODE = USER-U, DIG SHIFT = 0, **DIG VOX = 0**, PKT RATE = 1200, CAT RATE = 38400, TOT on (a few minutes). Mode DIG. DSP, NB and processor off. AGC slow. Power low. Filter: the widest one it has. | Radio in DIG, USER-U on the display. |
| 10 | Start CAT. `ls /dev/serial/by-id/`, then put that name in `RIG_DEVICE` in `/etc/draws-station/station.env` (`sudo nano`). Then `sudo systemctl enable --now rigctld-ft857d` and `rigctl -m 2 -r 127.0.0.1:4532 f`. | `rigctl` prints the 857D's frequency. If the model is wrong, `rigctl -l \| grep 857` shows the right number for `RIG_MODEL`. |
| 11 | Receive check. Tune to a band with signals. `arecord -D draws_rx -f S16_LE -r 48000 -c 2 -d 20 /tmp/rx.wav` then `sox /tmp/rx.wav -n remix 1 stats`. | `Pk lev dB` around -12 on signals. If near 0, lower ADC Level; if below -30, raise it (`alsamixer -c udrc`, 1 dB steps). Send me the stats output; I'll check it for hum. |
| 12 | PTT check, dummy load on. `sudo touch /etc/draws-station/tx-enabled` then `draws-ptt on`, look at the radio, `draws-ptt off`. | TX lights on `on`, goes out on `off`. |
| 13 | TX level. `sox -n -r 8000 -c 1 -b 16 /tmp/tone.wav synth 10 sine 1800 vol 0.5` then `draws-txwav /tmp/tone.wav`. Watch the ALC meter. Repeat, moving DIG MIC on the radio. | ALC just moving, never pinned. Write down the DIG MIC value. |
| 14 | Passband, both radios. `sox -n -r 8000 -c 1 -b 16 /tmp/sweep.wav synth 20 sine 100-3800 vol 0.5` then `draws-txwav /tmp/sweep.wav`, received on the RSP2 at the same time. Do the same sweep on the 897D the way test 3a keyed it. | I need the two RSP2 recordings. I read the -6 dB edges from them and tell you which filter setting to keep. |
| 15 | Turn keying off when done: `sudo rm /etc/draws-station/tx-enabled` | `draws-ptt on` now says keying is off. |

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
sudo install -m 0755 bin/draws-ptt bin/draws-txwav bin/draws-rxrec /usr/local/bin/
sudo install -m 0644 alsa/asound.conf /etc/asound.conf
sudo install -m 0644 config/gpsd /etc/default/gpsd
sudo install -m 0644 config/chrony-draws.conf /etc/chrony/conf.d/draws.conf
sudo install -m 0644 systemd/draws-ptt-safe.service systemd/rigctld-ft857d.service systemd/draws-rxrec.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable draws-ptt-safe.service
sudo tee -a /boot/firmware/config.txt < config/boot-config.txt
sudo reboot
```

The receive recorder (`draws-rxrec.service`) stays off until test 3 needs it.
Keying stays off except between steps 12 and 15.
