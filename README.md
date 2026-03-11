# Fault-Tolerant Firmware with Custom Watchdog and FOTA

ESP32 firmware built on ESP-IDF with a software-level task watchdog supervisor and a secure OTA pipeline with version tracking and rollback protection.

---

## Architecture

```
app_main
├── ota_ops_init()        — NVS init → WiFi STA → OTA gatekeeping → firmware download
├── gpio_init()           — GPIO + mutex setup
├── countdown_task        — elapsed timer, feeds watchdog every 250ms
├── blinker_1_task        — GPIO blink pattern (3x), semaphore-guarded
├── blinker_2_task        — GPIO blink pattern (2x), semaphore-guarded
└── watchdog_task         — periodic task health monitor
```

---

## Modules

### `wdt_ops` — Software Watchdog Supervisor

A custom task-level watchdog independent of the ESP32 hardware TWDT. Each monitored task registers itself with a name, timeout, and criticality flag. The supervisor polls all registered tasks every 1000ms against their last check-in timestamp.

- **Critical task failure** → `esp_ota_mark_app_invalid_rollback_and_reboot()`
- **Non-critical task failure** → task deleted, marked inactive, execution continues

Tasks feed the watchdog via a function pointer (`feed_dog`) assigned at runtime, keeping the watchdog decoupled from task logic.

### `ota_ops` — FOTA with Manifest and SHA256 Verification

OTA update flow:

1. **Gatekeeping** — on boot, checks the running partition's OTA image state. Marks valid or triggers rollback depending on `ESP_OTA_IMG_NEW`, `VALID`, or `ABORTED`.
2. **Manifest Fetch** — HTTPS GET to `MANIFEST_URL` (GitHub raw). JSON response is parsed for `version`, `sha256`, `size`, and `url` fields. Server version is compared against the NVS-stored client version before proceeding.
3. **Firmware Download** — streams `firmware.bin` over HTTPS in 4KB chunks via `esp_ota_write()` into the next update partition. SHA256 is computed incrementally over received bytes.
4. **Verification** — after download, total byte count is checked against the manifest `size` and the computed SHA256 digest is compared against the manifest `sha256`. OTA is aborted on mismatch.
5. **Version Tracking** — firmware version stored in NVS (`ota` namespace), synced from `esp_app_desc_t` on validated boot. A blacklist entry is written to NVS on rollback to prevent re-flashing a known-bad version.

OTA is aborted on version downgrade attempt, blacklisted version, size/SHA256 mismatch, or HTTP error.

### `task_ops` — Application Tasks

- `countdown` — logs elapsed time since boot in seconds
- `blinker_1` / `blinker_2` — blink the built-in LED N times, mutex-protected to prevent concurrent GPIO access

---

## Manifest Format

The manifest server (`MANIFEST_URL`) responds with a JSON payload:

```json
{
  "version": "1.0.1",
  "sha256": "ABC123...64hexchars",
  "size": 123456,
  "url": "firmware.bin"
}
```

`version` is compared against the NVS-stored client version using semantic versioning (`major.minor.patch`). `url` is appended to `GITHUB_RAW_BASE` to construct the firmware download URL.

---

## Configuration (`ota_ops.h`)

| Macro | Default | Description |
|---|---|---|
| `SSID` / `PASSWORD` | — | WiFi credentials |
| `GITHUB_USER` / `GITHUB_REPO` / `GITHUB_BRANCH` | — | GitHub source for manifest and firmware |
| `MANIFEST_BUF_SIZE` | `512` | Manifest JSON buffer size (bytes) |
| `OTA_BUF_SIZE` | `4096` | Firmware download chunk size (bytes) |
| `OTA_HANDSHAKE_TIMEOUT` | `30000` | HTTP timeout (ms) |
| `MAX_RETRY` | `5` | WiFi reconnect attempts |

---

## Partition Table

Custom partition layout defined in `partitions.csv` with two OTA app slots enabling A/B switching via `esp_ota_get_next_update_partition()`.

---

## Build

**Toolchain:** ESP-IDF  
**Board:** NodeMCU-32S (ESP32)  
**Flash:** 4MB

```bash
# ESP-IDF
idf.py build
idf.py -p PORT flash monitor

# PlatformIO
pio run -t upload
pio device monitor
```

Serve firmware and cert files over HTTP before flashing:

```bash
python3 -m http.server 8000
```

---

## Watchdog Timeout Reference

| Task | Timeout | Critical |
|---|---|---|
| `countdown_task` | 500ms | Yes — rollback on miss |
| `blinker_1_task` | 100ms | No — terminate on miss |
| `blinker_2_task` | 1500ms | No — terminate on miss |

---

## Dependencies

- `esp_wifi`, `esp_netif`, `esp_event`
- `esp_http_client`, `esp_crt_bundle` (HTTPS)
- `app_update` (OTA)
- `nvs_flash`
- `cJSON`
- `mbedtls` (SHA256)
- `FreeRTOS`
