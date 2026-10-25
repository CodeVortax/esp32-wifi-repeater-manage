# Web API

Sign in first: `POST /api/login` with `user` and `pass` returns a `sid` cookie (HttpOnly, SameSite=Strict, 12 h). Every other route answers 401 without it. After 5 wrong tries an address is locked for 60 s (429 with `retryIn`). Every **POST** must also carry the header `X-Requested-With` (any value), otherwise it is rejected with 403. The dashboard does both for you.

Form-encoded bodies, as `application/x-www-form-urlencoded`. A MAC may use `:` or `-` and any case.

| Method and path | Body | Answers |
|---|---|---|
| `POST /api/login` | `user`, `pass` | 200 and cookie / 401 / 429 |
| `POST /api/logout` | | 200 |
| `GET /api/status` | | `{sys, speed, devices[], dns[]}` |
| `GET /api/catalog` | | `{cats[], apps[[name, cat]]}` (ids are positions) |
| `GET /api/apps` | | KB today: `{total:[[app,kb]], devices:{mac:[[app,kb]]}}` |
| `POST /api/rename` | `mac`, `name` (max 32 bytes, empty = "User N") | 200 / 400 |
| `POST /api/speed` | `mac`, `down`, `up` (`default` or kbit/s, `0` = unlimited) | 200 / 400 / 404 |
| `POST /api/speedDefaults` | `gdown`, `gup`, `ddown`, `dup` (kbit/s, `0` = unlimited) | 200 / 400 |
| `GET /api/learn` | | `{rules[], unknown[]}` |
| `POST /api/rules/add` | `suffix` (e.g. `example.com`), `app` (id) | 200 / 400 |
| `POST /api/rules/del` | `suffix` | 200 / 404 |
| `POST /api/unknown/clear` | | 200 |
| `POST /api/reboot` | | 200 |
| `POST /api/ota?type=firmware\|ui` | multipart `file` (`ota-firmware.bin` / `ota-ui.bin`) | 200 and restart / 400 `{error}` |
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
curl -c jar -H 'X-Requested-With: curl' -d user=admin -d pass=yourpassword http://192.168.4.1/api/login
curl -b jar -H 'X-Requested-With: curl' \
     -d 'mac=AA:BB:CC:DD:EE:FF' -d 'mb=2048' http://192.168.4.1/api/setLimit
```

## Status fields

`sys`: `version`, `uptime` (s), `heap`, `upstream`, `staSsid`, `apSsid`, `staIp`, `apIp`, `rssi`, `channel`, `counting` (are the counting hooks installed), `defaultPassword`, `telegram`, `time`.
`speed`: `globalDown`, `globalUp`, `defaultDown`, `defaultUp` (kbit/s, 0 = unlimited).
`devices[]`: `mac`, `name`, `userNo`, `ip`, `bytesToday`, `bytesAllTime`, `limitMB`, `down`, `up` (-1 = default), `blocked`, `whitelisted`, `online`, `cats[]` (KB per category today).
`dns[]`: `ip`, `who`, `domain`, `app`, `ageSec`, newest first.
