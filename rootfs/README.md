# rootfs: files installed on the Cosmo's Debian root system

Paths mirror the device. Install as root with root ownership, e.g.
`tar -C rootfs -cf - <paths> | ssh <cosmo> 'sudo tar --no-same-owner -C / -xf -'`,
then `systemctl daemon-reload` for units.

- `etc/systemd/system/cosmo-boot-ok.service`, `usr/local/sbin/cosmo-boot-ok`:
  clear the initramfs tripwire (COSMO-OK in `cache`) once sshd is running.
  Enabled for multi-user.target.
- `usr/local/bin/cosmo-power` (+ `reboot`, `shutdown`, `poweroff`, `halt`
  links), `etc/sudoers.d/cosmo-power` (mode 0440): any user may reboot or
  power off without a password.
- `usr/local/sbin/cosmo-power-key`, `etc/systemd/system/cosmo-power-key.service`
  (enabled), `etc/systemd/logind.conf.d/cosmo-power-key.conf`: hold the power
  key 3 s = clean power off; logind ignores the key. Needs mt6358-keys on.
