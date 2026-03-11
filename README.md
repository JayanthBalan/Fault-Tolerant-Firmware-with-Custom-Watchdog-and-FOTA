# Fault-Tolerant Firmware with Custom Watchdog and FOTA

ESP32 firmware built on ESP-IDF with a software-level task watchdog supervisor and a secure OTA pipeline with manifest-based version tracking, SHA256 verification, and rollback protection.

---

## Architecture

```
app_main
├── ota_ops_init()        — NVS init → WiFi STA → OTA gatekeeping → firmware download check
├── gpio_init()           — GPIO + mutex setup
├── countdown_task        — elapsed timer, feeds watchdog every 250ms
├── blinker_1_task        — GPIO blink pattern (3x), semaphore-guarded
├── blinker_2_task        — GPIO blink pattern (2x), semaphore-guarded
└── watchdog_task         — periodic task health monitor, handles termination and rollback
```

---

## Modules

### `wdt_ops` — Software Watchdog Supervisor

A custom task-level watchdog independent of the ESP32 hardware TWDT. Each monitored task registers itself with a name, timeout, criticality flag, and an optional rollback wait window. The supervisor polls all registered tasks every 1000ms against their last check-in timestamp.

- **Critical task failure** → calls the `rollback()` function pointer → `app_rollback()` → blacklists the running firmware version in NVS → `esp_ota_mark_app_invalid_rollback_and_reboot()`
- **Non-critical task failure** → sends a task notification to the offending task, waits up to `rollback_ticks` for an acknowledgment, then calls `vTaskDelete`. If no ack arrives in time, escalates to rollback.

Tasks register via `task_register()` and feed the watchdog through a function pointer (`feed_dog`) that gets wired up at runtime in `app_main`. This keeps the watchdog fully decoupled from task logic — tasks don't need to know anything about the watchdog implementation.

#### Cooperative Termination

When a non-critical task times out, the watchdog sends it a `xTaskNotifyGive()` signal rather than force-deleting it. The task is expected to catch this at the top of its loop (or inside inner delay loops), release any held mutexes, send an ack back to the watchdog, then `vTaskDelete(NULL)`. This avoids the FreeRTOS priority inheritance assert that occurs when an external caller forcibly releases a mutex owned by another task.

### `ota_ops` — FOTA via GitHub Raw

OTA update flow on every boot:

1. **Gatekeeping** (`ota_gatekeep`) — checks the running partition's OTA image state on every boot. Marks valid or triggers rollback depending on `ESP_OTA_IMG_NEW`, `VALID`, or `ABORTED`. Always syncs the NVS firmware version from `esp_app_desc_t` so NVS reflects reality even after a rollback.

2. **Manifest Fetch** — HTTPS GET to `manifest.json` on the GitHub raw branch. The manifest contains the target version, expected binary size, SHA256 hash, and relative firmware URL.

3. **Version Check + Blacklist Guard** — compares semver of manifest against NVS-stored client version. Skips download if already up to date, server version is older, or the server version matches the stored bad-firmware blacklist entry.

4. **Firmware Download** — streams `firmware.bin` in 4KB chunks via `esp_http_client_read()` into the next OTA partition, computing SHA256 incrementally throughout.

5. **Verification** — after download, checks total byte count against manifest `size` and the computed SHA256 against manifest `sha256`. Both must match before `esp_ota_end()` and `esp_ota_set_boot_partition()` are called. On any mismatch, `esp_ota_end()` is called without committing, and the firmware is discarded.

#### Rollback Blacklist

When the watchdog triggers a rollback, `app_rollback()` reads the currently running firmware version from `esp_app_desc_t` and writes it to NVS under the key `bad_version` before calling `esp_ota_mark_app_invalid_rollback_and_reboot()`. On the next boot, `should_update()` checks this key and refuses to re-download that version, breaking the rollback loop.

The blacklist stores one version at a time (last-write-wins). If a newer bad version is pushed and causes another rollback, it simply overwrites the previous entry.

### `task_ops` — Application Tasks

- `countdown` — logs elapsed time since boot in seconds, feeds watchdog every 250ms inside a 40-iteration inner loop
- `blinker_1` — waits 5 seconds holding the GPIO mutex, blinks the built-in LED 3 times
- `blinker_2` — same structure, blinks 2 times with a 1.5s timeout registered on the watchdog

Both blinker tasks check for the termination notification at the top of `while(1)` and also inside the inner delay loop, so the watchdog's ack window is always reachable without waiting for the full loop to complete.

---

## Partition Table

```
# Name     Type    SubType   Offset      Size
nvs        data    nvs       0x9000      0x5000
otadata    data    ota       0xe000      0x2000
app0       app     ota_0     0x10000     0x1E0000
app1       app     ota_1     0x1F0000    0x1E0000
spiffs     data    spiffs    0x3D0000    0x3000
```

Two equal OTA slots for A/B partition switching. `esp_ota_get_next_update_partition()` selects whichever slot isn't currently running.

---

## Manifest Format

Place `manifest.json` at the root of the configured GitHub branch:

```json
{
  "version": "1.1.4",
  "sha256": "ABCDEF1234...",
  "size": 1009808,
  "url": "tools/firmware.bin"
}
```

`sha256` must be uppercase hex (64 chars). `url` is a path relative to the GitHub raw base. `size` is the exact byte count of the binary.

---

## Watchdog Task Reference

| Task | Timeout | Critical | Rollback Wait |
|---|---|---|---|
| `countdown_task` | 500ms | Yes — rollback on miss | — |
| `blinker_1_task` | 100ms | No — cooperative terminate | 10ms |
| `blinker_2_task` | 1500ms | No — cooperative terminate | 1500ms |

---

## Stack Sizes

| Task | Stack | Min Free (observed) |
|---|---|---|
| watchdog_task | 8192 | ~7704 words |
| blinker_1_task | 4096 | ~3792 words |
| blinker_2_task | 4096 | ~3580 words |
| countdown_task | 4096 | ~2244 words |

---

## Configuration (`ota_ops.h`)

| Macro | Default | Description |
|---|---|---|
| `SSID` / `PASSWORD` | — | WiFi credentials |
| `GITHUB_USER/REPO/BRANCH` | see header | GitHub raw source for manifest + firmware |
| `OTA_BUF_SIZE` | `4096` | Chunk size for firmware download |
| `MANIFEST_BUF_SIZE` | `512` | Max bytes for manifest JSON |
| `OTA_HANDSHAKE_TIMEOUT` | `30000` | HTTP timeout (ms) |
| `MAX_RETRY` | `5` | WiFi reconnect attempts |
| `NVS_BAD_VER_KEY` | `"bad_version"` | NVS key for blacklisted firmware version |

---

## Build

**Toolchain:** ESP-IDF 5.5.0  
**Board:** NodeMCU-32S (ESP32)  
**Flash:** 4MB  
**Framework:** PlatformIO + ESP-IDF component model

```bash
# PlatformIO
pio run -t upload
pio device monitor

# ESP-IDF
idf.py build
idf.py -p PORT flash monitor
```

### Updating Firmware on GitHub

After building, grab the SHA256 and size, then update `manifest.json`:

```powershell
# SHA256
Get-FileHash .pio/build/nodemcu-32s/firmware.bin -Algorithm SHA256

# Size
ls .pio/build/nodemcu-32s/firmware.bin
```

Copy `firmware.bin` to `tools/`, update `manifest.json` with the new version, hash, size, and commit to the branch.

---

## Dependencies

- `esp_wifi`, `esp_netif`, `esp_event`
- `esp_http_client`, `esp_crt_bundle`
- `app_update` (OTA)
- `nvs_flash`
- `mbedtls` (SHA256)
- `cJSON`
- `FreeRTOS`
