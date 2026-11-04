# Channel update manifest

The agent polls `https://get.thinremote.io/<channel>.json` to decide whether to
self-update. This file is the contract between the agent (consumer, task #2) and
the release CI that produces it (publisher, task #5).

## Format

```json
{
  "version": "v1.8.0",
  "published_at": 1762000000,
  "soak_h": 72,
  "ramp_h": 96,
  "frozen_at": 1762500000,
  "checksums": { "<arch>": "sha256:<hex>" }
}
```

| Field          | Type            | Required | Meaning |
|----------------|-----------------|----------|---------|
| `version`      | string          | yes      | Target version. Compared to `AGENT_VERSION` by string inequality: any difference is a candidate, including a lower version (panic downgrade). |
| `checksums`    | object          | yes      | `"<AGENT_BINARY_SUFFIX>": "sha256:<hex>"` per architecture, as today. |
| `published_at` | integer (epoch s, UTC) | no | When the release was published. |
| `soak_h`       | number (hours ≥ 0) | no    | Soak period after publish during which nobody auto-updates. |
| `ramp_h`       | number (hours ≥ 0) | no    | Duration over which the eligible fraction grows from 0 to 1. |
| `frozen_at`    | integer (epoch s, UTC) | no | If present, ramp exposure is frozen at its level at this instant. |

The staging block is `published_at` + `soak_h` + `ramp_h` **as a whole**: either
all three are present and valid, or none (legacy `main`/`develop` manifests,
eligible immediately). A partial or invalid block (negative, non-numeric,
`soak_h`/`ramp_h` outside `[0, 8760]`) is treated as invalid: **not eligible**
(fail-safe), never inherited. Unknown fields are ignored. Default staged values
published by CI: `soak_h = 72`, `ramp_h = 96` (≈ 1 week to 100%).

## Staged-rollout eligibility (pure, deterministic)

A device computes a fixed fraction in `[0, 1)` from its `device_id` and the
target `version`:

```
f = uint64_big_endian( SHA-256("<device_id>:<version>")[0..8] ) / 2^64      (clamped < 1)
```

Let `soak_end = published_at + soak_h · 3600`, and
`ramp_ref = min(now, frozen_at)` when `frozen_at` is set, else `now`.

- `now < published_at`  → not eligible (clock behind / not yet published).
- `now < soak_end`      → not eligible (soaking).
- `ramp_h == 0`         → eligible once the soak has elapsed.
- otherwise eligible ⇔ `(ramp_ref − soak_end) / 3600 / ramp_h > f`.

Mixing the version into the hash rotates which devices go first on each release
(the canary group is not always the same machines). The eligible set only grows
with time (nested bands). The orchestrated `update` resource and `thinr-agent
update --now` ignore the staging block (they apply immediately).

## Reproducible test vectors

`SHA-256("<device_id>:<version>")`, first 8 bytes big-endian over 2^64:

| device_id    | version | fraction       |
|--------------|---------|----------------|
| `device-aaa` | `v1.8.0`| 0.6856489929   |
| `device-bbb` | `v1.8.0`| 0.1306323212   |
| `device-aaa` | `v1.8.1`| 0.1111382881   |

Reproduce with: `printf '<device_id>:<version>' | openssl dgst -sha256`, take the
first 16 hex chars as a big-endian u64 and divide by 2^64. These are asserted in
`agent/tests/update_mode_tests.cpp`.
