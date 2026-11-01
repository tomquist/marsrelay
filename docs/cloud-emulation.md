# Cloud emulation

`marstack` answers the Marstek cloud's HTTP requests on port 80, and on port 443 over TLS. `capture_dns` points every cloud hostname at Marsrelay, so no DNS setup of your own is needed. Each reply is the one the real cloud sends, so the device behaves as it would online.

| Request | Sent by | What Marsrelay answers |
|---|---|---|
| `getDateInfoeu.php` (and its spellings) | all batteries, Marstek CTs | the current time; see [`time_suffix`](#settings) |
| `setB2500Report` | B2500 | an acknowledgement of the daily report |
| `puterrinfo.php` | B2500 | an acknowledgement of the error log |
| `getDeviceFire` | B2500 | its output power limits; see [B2500](b2500.md#output-power-limits) |
| `getRealtimeSoc` | B2500 after a controller restart | no charge level; see [B2500](b2500.md#charge-level-after-a-controller-restart) |
| `setSmallReporting` | Jupiter | an acknowledgement of the report |
| `setCtReporting` | CT002 (firmware 122 and later), CT003 | an acknowledgement of the report |
| `POST /data-upload/v1/venus/<id>` (TLS) | Venus on control firmware v150 and later | an acknowledgement of the telemetry upload; see [Venus](venus.md#the-network-dropouts-every-15-minutes) |
| `setVenusDReporting`, `setAcCoupleReporting` | Venus before v150, Venus E gen 1/2 | an acknowledgement of the telemetry upload; see [Venus](venus.md#telemetry-on-older-firmware) |
| `schedulings/policies` (TLS) | Venus in AI mode | an empty schedule; see [Venus](venus.md#ai-mode) |
| `backup-configs` | Venus that lost its settings | nothing to restore, as the cloud answers a device without a backup |
| `getDevicesApply` | Venus after the app's "active upgrade" | only with `power_version` set; see [Venus](venus.md#power-class-venus-e-gen-12) |

Any other request to a cloud host gets a 404 and a `No answer for ...` line in the log; see [troubleshooting](troubleshooting.md#repeating-getdateinfoeuphp-requests-and-udp-proxy-log-lines).

## Settings

All optional, on `marstack:`.

| Option | Default | Meaning |
|---|---|---|
| `https.port` | `443` | where the TLS listener binds |
| `https.hold_time` | `25s` | how long an answered connection is held before a clean close. Must exceed the battery's own 20 s receive timeout, or be `0s` to close at once |
| `https.max_body` | `8192` | bytes kept from an upload body |
| `https.max_connections` | `4` | connections that may be held open at once |
| `https.accept_all` | `false` | answer *every* path with `{"code":0}`, not just the upload. Off deliberately: answering an endpoint whose expected reply nobody has reverse-engineered can change what the battery does in untested ways. Watch the log first |
| `raw_responses` | `true` | reproduce the cloud's exact response framing on both ports |
| `time_suffix` | `"04_0_0_0"` | the four trailing fields of the clock reply, mirrored from the real endpoint rather than invented |
| `power_limits` | not set | what a B2500 is told its output power limits are; see [B2500](b2500.md#output-power-limits) |
| `power_version` | not set | what a Venus E gen 1/2 is told its power class is; see [Venus](venus.md#power-class-venus-e-gen-12) |
