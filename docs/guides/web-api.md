# Web API

Every route needs HTTP Basic auth (`admin` and the dashboard password). Every **POST** must also carry the header `X-Requested-With` (any value), otherwise it is rejected with 403. The dashboard does both for you.

Form-encoded bodies, as `application/x-www-form-urlencoded`. A MAC may use `:` or `-` and any case.

| Method and path | Body | Answers |
|---|---|---|
| `GET /api/status` | | `{sys, devices[], dns[]}` |
| `POST /api/block` | `mac` | 200 / 404 |
| `POST /api/unblock` | `mac` | 200 / 404 |
| `POST /api/whitelist` | `mac`, `enabled` (`1` or `0`) | 200 / 404 |
| `POST /api/setLimit` | `mac`, `mb` (0-10000000, `0` = unlimited) | 200 / 400 / 404 |
| `POST /api/ping` | `host` (hostname or IP) | 202 and the state; 409 if busy or invalid |
| `GET /api/ping` | | `{"state":"idle\|running\|done\|error", ...}` |
| `GET /api/settings` | | current SSIDs and flags (never passwords) |
| `POST /api/settings` | any of `sta_ssid`, `sta_password`, `ap_ssid`, `ap_password`, `admin_password`, `tg_token`, `tg_chat_id` | 200 `saved, rebooting...` / `no changes`; 400 with the reason |
| `WS /ws` | | pushes the `/api/status` JSON every 2 s |

## Example

```bash
curl -u admin:yourpassword -H 'X-Requested-With: curl' \
     -d 'mac=AA:BB:CC:DD:EE:FF' -d 'mb=2048' http://192.168.4.1/api/setLimit
```

## Status fields

`sys`: `version`, `uptime` (s), `heap`, `upstream`, `staSsid`, `apSsid`, `staIp`, `apIp`, `rssi`, `channel`, `counting` (are the counting hooks installed), `defaultPassword`, `telegram`, `time`.
`devices[]`: `mac`, `bytesToday`, `bytesAllTime`, `limitMB`, `blocked`, `whitelisted`, `online`.
`dns[]`: `ip`, `domain`, `ageSec`, newest first.
