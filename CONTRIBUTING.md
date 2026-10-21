# Contributing

Thanks for helping. This is a small firmware, and it stays easy to maintain if changes stay small and focused.

## Ground rules

- **Nothing slow in the web handlers.** They run in the `async_tcp` task. Start a task or return a result you can poll, like ping does.
- **No allocation and no `Serial` inside a spinlock.** Copy under the lock, build strings outside it.
- **Other tasks read snapshots** of the device table (`quota_snapshot`), never the table itself.
- **Everything the browser shows is text.** Build DOM nodes with `h()` or `textContent`. Never `innerHTML` with data from the device or the network.
- **Every state-changing route** goes through `guard(req, true)`.
- **Validate before saving.** A bad setting must never be able to lock someone out.
- **Licence.** Code is `AGPL-3.0-only`. If you add code from another project, keep its notice and say so in the pull request.

## Getting started

```bash
git clone https://github.com/CodeVortax/esp32-wifi-repeater-manage && cd esp32-wifi-repeater-manage
pip install -U platformio
./build.sh
```

See [Building](docs/guides/building.md). Read the [Architecture overview](docs/architecture/overview.md) first.

## Bug reports

Include: the version (`FW_VERSION`), your board, what you did and what you expected, and the **serial log from boot**. For counting problems, include the lines with `[QUOTA]`.

## Pull requests

- One topic per pull request.
- `./build.sh merge` must pass (CI runs it).
- Update `CHANGELOG.md` and the docs if behaviour changes.
