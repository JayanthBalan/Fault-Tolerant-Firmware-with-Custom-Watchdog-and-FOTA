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

### `ota_ops` — FOTA with Token Handshake

OTA update flow:

1. **Gatekeeping** — on boot, checks the running partition's OTA image state. Marks valid or triggers rollback depending on `ESP_OTA_IMG_NEW`, `VALID`, or `ABORTED`.
2. **Token Handshake** — HTTP GET to `CERT_URL`. Response is validated against a local `auth_token`. The first two bytes encode the server firmware version, which is compared against the NVS-stored client version before proceeding.
3. **Firmware Download** — streams `firmware.bin` from `OTA_URL` in 1KB chunks via `esp_ota_write()` into the next update partition.
4. **Version Tracking** — firmware version stored in NVS (`ota` namespace). Incremented on validated boot, decremented on rollback.

Authentication fails and OTA is aborted on token mismatch, version downgrade attempt, or HTTP error.

### `task_ops` — Application Tasks

- `countdown` — logs elapsed time since boot in seconds
- `blinker_1` / `blinker_2` — blink the built-in LED N times, mutex-protected to prevent concurrent GPIO access

---

## OTA Token Format

The cert server responds with a 44-byte payload:

```
[v][VV][ ][ ][ ][ ][...TOKEN (38 bytes)...]
 ^  ^^
 |  firmware version (2 ASCII digits)
 literal 'v'
```

Bytes `[6:44]` are compared against the compiled `auth_token`.

---

## Configuration (`ota_ops.h`)

| Macro | Default | Description |
|---|---|---|
| `SSID` / `PASSWORD` | — | WiFi credentials |
| `SERVER_IP` / `SERVER_PORT` | `172.20.181.94:8000` | OTA server |
| `AUTH_BUFFER_SIZE` | `44` | Token payload size (bytes) |
| `OTA_HANDSHAKE_TIMEOUT` | `30000` | HTTP timeout (ms) |
| `MAX_RETRY` | `5` | WiFi reconnect attempts |
| `SYS_POLL_DEL` | `60000` | Post-OTA poll delay (ms) |

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
| `blinker_1_task` | 1000ms | No — terminate on miss |
| `blinker_2_task` | 1500ms | No — terminate on miss |

---

## Dependencies

- `esp_wifi`, `esp_netif`, `esp_event`
- `esp_http_client`
- `app_update` (OTA)
- `nvs_flash`
- `FreeRTOS`
