# Release checklist

## Source hygiene

- [ ] Repository was generated from the intended stable development tree.
- [ ] `build/` is absent.
- [ ] Flash dumps are absent.
- [ ] Backups are absent.
- [ ] Logs are absent.
- [ ] Wi-Fi credentials are absent.
- [ ] API keys/tokens/private keys are absent.
- [ ] Personal names/data used for testing are absent.
- [ ] Pre-publication scanner has been reviewed.

## Upstream / legal

- [ ] Upstream MIT notices are preserved.
- [ ] `UPSTREAM.md` points to the correct XiaoZhi baseline.
- [ ] Modified source files retain applicable notices.
- [ ] Third-party component licenses are not removed.

## Build

- [ ] Clean build from the public repository succeeds.
- [ ] Firmware identifies XiaoZhi Care version correctly.
- [ ] Partition table matches the documented Care layout.

## Installer

- [ ] Final ZIP generated from the public release commit.
- [ ] SHA-256 file generated.
- [ ] Spanish UI tested.
- [ ] English UI tested.
- [ ] Portuguese UI tested.
- [ ] Progress display tested.
- [ ] Final ZIP installed end-to-end on a test board.
- [ ] Wi-Fi preserved.
- [ ] Display works.
- [ ] Microphone works.
- [ ] Speaker works.
- [ ] Care panel works.
- [ ] LEDs/reminders/radio smoke-tested.

## Rollback

- [ ] Backup created.
- [ ] Rollback completed.
- [ ] Restored 16 MB SHA-256 equals original backup.
- [ ] Original XiaoZhi boots and functions.

## GitHub

- [ ] Tag created: `v0.3.2-alpha`.
- [ ] Release notes reviewed.
- [ ] Installer ZIP attached to GitHub Release.
- [ ] Installer ZIP SHA-256 published.
- [ ] No private backup or Flash dump attached.
