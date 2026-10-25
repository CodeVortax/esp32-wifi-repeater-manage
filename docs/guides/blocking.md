# Ad, tracker and site blocking

The repeater is every device's DNS server, so it can refuse to resolve a name. A blocked name is answered with `0.0.0.0` (and "no data" for IPv6 and other record types) and nothing is forwarded.

## Three lists

Checked for the name and every parent (`a.b.example.com`, `b.example.com`, `example.com`):

1. **Allow** (your hosts file, `@@example.com`) always wins.
2. **Block** (your hosts file).
3. **Built-in ad / tracker list**, switchable on the Block page.

## The built-in list

`blocklist/sources/*.txt` (Peter Lowe's list, AdAway's list) and `blocklist/extra.txt` are merged by `scripts/gen_blocklist.py`. Names under `blocklist/keep.txt` (google.com, facebook.com, whatsapp.net ...) are never included, and a listed parent replaces all its children. The result is written to `src/blocklist_data.h` as a Golomb-Rice coded set of 64-bit name hashes: **about 2.8 bytes per name** (6,572 names = 18 KB), stored in flash and searched in place, so it needs no RAM. Because only hashes are stored, about 1 lookup in a million can match a name that is not in the list; add it to your allow rules if you ever meet one. `make -C test` checks the encoder against the C++ decoder, 300,000 random names and a list of names that must never be blocked.

Credits: Peter Lowe (pgl.yoyo.org/adservers), AdAway (adaway.org, CC BY 3.0).

## Your hosts file

Edit it on the Block page, import a text file, or use the Block / Allow buttons. Up to 12 KB, 1,500 block rules and 100 allow rules. Understood per line: `0.0.0.0 example.com`, `127.0.0.1 example.com`, `example.com`, `||example.com^`, `@@example.com`, and `#` comments. It is stored in NVS (not in the dashboard files), so updating the dashboard never deletes it, and a failed save keeps the old file.

## DNS provider

Block page > DNS provider. Family resolvers (Cloudflare Family, AdGuard Family, OpenDNS FamilyShield, CleanBrowsing Family) filter adult sites for every device. It is plain DNS over UDP; DNS over HTTPS / TLS is not supported. If the first server does not answer within 1.2 s the second is asked.

## What it cannot do

Phones using **Private DNS**, **DNS over HTTPS** in the browser, or a **VPN** never ask the repeater, so they are not filtered and not blocked. Turn those off on the device. Apps that use fixed IP addresses are not affected by DNS blocking either.
