# Beta testing

Thanks for testing. Go down the list once after installing, and tick what
works. Anything that fails is worth a report, even small things.

## The checks

**Start and setup**
- [ ] Setup opened on the first start and created your account
- [ ] After the restart, the login screen appeared and your password works

**Screen and input**
- [ ] The picture is right: no flicker, no wrong colours
- [ ] Touch works across the whole screen
- [ ] Every key on the keyboard types the right character
- [ ] The keyboard backlight turns on

**Network**
- [ ] Wi-Fi connects, and Firefox opens a web page
- [ ] Wi-Fi reconnects by itself after a restart
- [ ] Bluetooth finds and pairs a device (mouse, keyboard or headphones)

**Sound**
- [ ] The speaker plays sound
- [ ] Headphones play sound

**Sleep and battery**
- [ ] Closing the lid puts the Cosmo to sleep; opening it wakes it
- [ ] The power key and Esc wake it
- [ ] After a night asleep the battery has dropped by only a few percent
- [ ] A wall charger charges it, and the battery percent goes up

**Ports and cards**
- [ ] A USB stick or mouse works in the **left** port
- [ ] A USB stick or mouse works in the **right** port
- [ ] A microSD card shows up in the file manager

**Cover screen**
- [ ] The small outer screen shows the time and battery

**Apps**
- [ ] Firefox, Thunderbird and LibreOffice open
- [ ] Discover installs an app from Flathub
- [ ] A game (KPatience, KSudoku or KBlocks) runs

**Off and on**
- [ ] Shut down from the menu switches the Cosmo off
- [ ] Restart from the menu comes back to the login screen

## Known issues

You do not need to report these:

- Wi-Fi is switched off while the Cosmo sleeps and comes back on waking.
- After sleep, the **right** USB port may stop working until a restart.
- The left USB port supplies power to devices only until the first sleep.
- Phone calls, mobile data, cameras, GPS, screen rotation and the
  fingerprint reader do not work yet.
- The bootloader shows the orange-state warning at every start.

## Reporting a problem

Open an issue at <https://github.com/atzandrew/cosmo-linux618/issues>.

Say **what you did, what you expected, and what happened**. Then open
Konsole and attach the output of these commands:

```sh
cat /etc/gemini/build-info
journalctl -b -p warning --no-pager > ~/cosmo-log.txt
```

`cosmo-log.txt` is in your home folder; attach it to the issue.

**If the Cosmo crashed or restarted by itself**, the log from before the
restart is the useful one:

```sh
journalctl -b -1 -p warning --no-pager > ~/cosmo-log-before.txt
```

If an app crashed, a crash window may offer to save a report: attach that
too.
