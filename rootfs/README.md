# rootfs: files installed on the Cosmo's Debian root system

Paths mirror the device. Install as root with root ownership, e.g.
`tar -C rootfs -cf - <paths> | ssh <cosmo> 'sudo tar --no-same-owner -C / -xf -'`,
then `systemctl daemon-reload` for units.

- `etc/systemd/system/cosmo-boot-ok.service`, `usr/local/sbin/cosmo-boot-ok`:
  clear the initramfs tripwire (COSMO-OK in `cache`) once multi-user.target
  is reached (no SSH or network dependency).
  Enabled for multi-user.target.
- `usr/local/bin/cosmo-power` (+ `reboot`, `shutdown`, `poweroff`, `halt`
  links), `etc/sudoers.d/cosmo-power` (mode 0440): any user may reboot or
  power off without a password.
- `usr/local/sbin/cosmo-power-key`, `etc/systemd/system/cosmo-power-key.service`
  (enabled), `etc/systemd/logind.conf.d/cosmo-power-key.conf`: hold the power
  (Esc) key 4 s = clean power off; a tap is Esc; logind ignores power keys. Needs mt6358-keys on.
