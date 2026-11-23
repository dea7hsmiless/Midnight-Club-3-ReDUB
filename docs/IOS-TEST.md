# Testing ReDUB 0.2.5 on an iPhone

This build has never run on a device. Everything below is the first-run checklist,
and the log line to look for at each step.

## What you are installing

`ios/dist/ReDUB-0.2.5-ios-unsigned.ipa` — 31 MB, **no disc inside**, so it needs
a game folder on the phone:

- If you already have 0.2.4 installed and sign this with the **same certificate
  and bundle id** (`com.ladastra.mcdub3remix`), the app's Documents folder — and
  the extracted disc in `Documents/game` — is kept. It just works.
- Otherwise: put the disc ISO somewhere the Files app can see (AirDrop, iCloud
  Drive, a USB import), then in the launcher use **Install from ISO** and pick it.
  That extracts it to `Documents/game` (4.4 GB; it needs the space free).

Apply `ios/dist/mcdub3remix.entitlements` when signing. It asks for the increased
memory limit, which the game needs; without it the app is likely to be killed
mid-boot.

## First run

You should see: the ReDUB launcher (a native screen, not the Xbox one), then
"Starting…", then the game.

The log is `Documents/mcdub3remix-launch.log`. In the Files app: **On My iPhone →
ReDUB**. If the app exits before the launcher appears, that log is the only thing
worth sending.

## Online is already configured

The app arrives with `Documents/mcdub3remix_env.txt` containing:

```
MC3_ROAM_JOIN=162.35.56.66
MC3_NETPLAY=1
MC3_ROAM_NAME=Donte
```

One line is the whole online setup (`MC3_ROAM_JOIN`), and `MC3_NETPLAY=1` makes
System Link use the relay too. Edit it in the Files app if the server changes;
restart the app after editing.

## Test 1 — free roam

On the PC, `ReDUB-0.2.5-local-test\mcdub3remix_env.txt` already points at the same
server. Start it, then on the phone: **Online → Free Roam**.

Phone log should show:

```
[roam] relay 162.35.56.66:47210 (group 0): datagrams go through it
[roam] started as "Donte" (id ........)
[roam] <the PC's name> joined
```

and the PC shows the phone joined. You should see each other's car, with the name
over it.

## Test 2 — a race

Both: **Online → System Link**. One creates a match, the other finds it and joins.

Phone log should show:

```
[ROAM] System Link will go through the relay (virtual LAN, MC3_NETPLAY=1 to use it)
[XNET] relay: virtual LAN 10.77.x.y
[XNET] HLE up: transport relay, address 10.77.x.y
```

Both lines must show the **same** 10.77 address. On a server with `--verbose`,
each System Link packet appears as `(11)`.

## If something is wrong

| What you see | What it means |
|---|---|
| App exits before the launcher | read `mcdub3remix-launch.log`; nothing else helps |
| Launcher appears, game never starts | same log; likely the disc is missing or the entitlement was not applied |
| `[XNET] HLE up: transport udp` | the build predates the relayed System Link, or `MC3_ROAM_JOIN`/`MC3_NETPLAY` was not read (check the seeded file still exists) |
| Free roam says no hub answers | packets not arriving: relay down, or UDP 47210 blocked — the PC build says the same thing |
| Both joined, no car | different room numbers |
| Race lobby empty on one side | both must be in the same room; the `(11)` lines on the server say whether packets are moving |

## What this build does not include

- A 60 Hz default. Set `MC3_FRAME_RATE=60` in the settings file (or use Port
  Options → FRAME RATE), and restart.
- An in-app online settings page. `MC3_ROAM_JOIN` / `MC3_NETPLAY` are edited in
  the settings file, as above.

The in-game Port Options menu **is** the touch-friendly two-page layout in this
build: the main page has the touch controls and FRAME RATE, `ADVANCED...` opens
the second page (steering, tilt, haptics, filtering, sharpening), and
`BACK TO OPTIONS` returns to the first page.
