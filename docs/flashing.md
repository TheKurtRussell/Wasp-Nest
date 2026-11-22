# Installing Wasp Nest (no toolchain needed)

This is the beginner path: let GitHub build the firmware for you and give you a
one-click web flasher. You never install ESP-IDF.

## One-time setup

1. Create a free GitHub account if you don't have one.
2. Make a new repository (e.g. `waspnest`). Upload the entire `waspnest/`
   folder (drag-and-drop works in the GitHub web UI), or push it with git.
3. In the repo: **Settings → Pages → Build and deployment → Source =
   "GitHub Actions"**.
4. Go to the **Actions** tab. The `build` workflow runs automatically on every
   push (you can also click **Run workflow**). First run takes ~5–10 min.

When it finishes (green check) you have two things:

- A **web flasher page** at `https://<your-username>.github.io/<repo>/`
- Both firmware images as downloadable **Artifacts** on the workflow run
  (`waspnest-p4` and `waspnest-c6`).

## Flashing the Tab5

### Step 1 — P4 app (browser, over USB-C)
1. Open the Pages URL in **Chrome or Edge** (Web Serial only works there).
2. Plug the Tab5 into the computer with **USB-C**.
3. Click **Install**, pick the Tab5's port, wait for it to finish.

### Step 2 — C6 radio service (one time, from SD)
The C6 chip has no USB of its own, so the P4 flashes it for you using
`esp-serial-flasher` — you just supply the image on the SD card.
1. From the Pages site (or the run Artifacts) download **`merged-c6.bin`**.
2. Copy it to the Tab5's microSD as **`/waspnest/c6_service.bin`**.
3. Insert the card and boot. If the C6 isn't programmed yet, the P4 flashes it
   automatically on boot; otherwise tap **Flash C6** on the Status tab.
4. Done — the C6 keeps that firmware. From now on you only repeat Step 1 to
   update the app.

## Updating later
Push a change (or edit on GitHub) → Actions rebuilds → re-flash the P4 from the
same web page. Only re-do Step 2 if the C6 firmware itself changed.

## Troubleshooting
- **No port appears in the browser:** use Chrome/Edge, try a different USB-C
  cable (some are power-only), and make sure no other serial monitor is open.
- **"Chip not supported":** the page uses a P4-aware ESP Web Tools build; hard-
  refresh, or check the workflow actually produced `merged-p4.bin`.
- **C6 flash fails:** confirm `merged-c6.bin` is at `/waspnest/c6_service.bin`
  on the card, and see the RESET/BOOT pin note in
  `components/hc_c6updater/hc_c6updater.c` (must match your board revision).

## Why not the Launcher?
The bmorcelli Launcher loads apps onto a *single* main chip and has no Tab5/P4
build. It can't reach the separate C6 radio at all. So for the Tab5 the web
flasher (P4) + P4-flashes-C6 (SD) is the right path. Keep Launcher for your
Cardputer.
