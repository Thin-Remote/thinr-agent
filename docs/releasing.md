# Releasing the agent: staged rollout, panic and freeze

Operational guide for the publication side of the safe auto-updater (agent
v1.8.0). It covers how a stable release is published with the staged-rollout
fields, how pre-releases are kept off the stable channel, and the two manual
recovery actions (panic rollback and freeze/unfreeze).

The agent-side logic (soak/ramp timers, per-device hash, rollback watchdog) is
described in `docs/design/safe-auto-updater.md`. The manifest field reference
lives in `docs/update-manifest.md`.

## The manifest

Each channel has a `<channel>.json` manifest at `get.thinremote.io`
(`latest.json`, `main.json`, `develop.json`, and one `<tag>.json` per archived
version). Shape:

```json
{
  "version": "v1.8.0",
  "checksums": { "x86_64-linux-musl": "sha256:<64 hex>", "...": "..." },
  "published_at": 1762000000,
  "soak_h": 72,
  "ramp_h": 96
}
```

| Field          | Type           | Meaning                                                            |
|----------------|----------------|--------------------------------------------------------------------|
| `version`      | string         | Exact channel/tag string. The updater compares by inequality.      |
| `checksums`    | object         | One `"<arch-suffix>": "sha256:<hex>"` per published binary.         |
| `published_at` | integer (epoch s UTC) | When this manifest was published. Taken once per CI run.    |
| `soak_h`       | integer (hours)| Quiet period after publish during which no auto device applies.    |
| `ramp_h`       | integer (hours)| Window over which exposure widens, after the soak.                 |
| `frozen_at`    | integer (epoch s UTC), optional | Present only while the ramp is frozen. Absent means not frozen. |

`published_at`, `soak_h` and `ramp_h` are the contract shared with the
staged-rollout agent task. Branch channels (`main`, `develop`) and pre-releases
always carry `soak_h=0`/`ramp_h=0`, so the current updater applies them without
any delay and older agents (which read only `version` and `checksums`) ignore
the extra fields.

## Publishing a stable release

Push a tag matching exactly `vN.N.N` (no suffix). `build.yml` builds the matrix,
archives the binaries under `binaries/<tag>/`, writes `<tag>.json`, and promotes
to the stable channel: it copies the binaries to `binaries/latest/`, writes
`latest.json`, and invalidates `/latest.json` and `/binaries/latest/*`.

```bash
git tag v1.8.0 && git push origin v1.8.0
```

### Parametrizing soak and ramp

Defaults are `soak_h=72` and `ramp_h=96`. Override them without editing the
workflow by setting repository variables (Settings > Secrets and variables >
Actions > Variables):

- `AGENT_SOAK_H`
- `AGENT_RAMP_H`

Both must be non-negative integers. The workflow validates them before any
upload, so an invalid value aborts the publication before anything reaches S3.

## Stable vs pre-release

Stable means the tag matches exactly `vN.N.N`. Anything with a suffix
(`v1.9.0-rc1`, `-beta`, `-alpha.2`, `-rc.1`, ...) is a pre-release. This single
rule lives in `scripts/release/lib.sh` (`thinr_is_stable_tag`) and is used by
both the publication job (`build.yml`) and the GitHub release job
(`release.yml`), so they never disagree.

A pre-release is built and archived under `binaries/<tag>/` with its own
`<tag>.json`, but it never writes `latest.json`, never copies to
`binaries/latest/`, and never regenerates `install.sh`. This matters because
auto-update is on by default: a pre-release that reached `latest` would roll out
to the whole casual-user fleet.

Classify a ref locally with:

```bash
scripts/release/channel.sh classify refs/tags/v1.9.0-rc1
scripts/release/channel.sh is-stable v1.8.0   # exit 0 if stable
```

## Panic: roll the fleet back to a known-good release

`panic.sh` re-points `latest` at a previously archived stable release. Because
the agent applies on `current != latest`, devices already on the bad version
*downgrade* to the target (through the rollback watchdog). The repointed
manifest is published with `soak_h=0`, `ramp_h=0`, `published_at=now` and no
`frozen_at`, so every device applies the change immediately.

Run it from the Actions tab: workflow **Panic rollback**, input `version`
(for example `v1.7.4`). `dry_run` previews the plan without mutating anything.

What it guarantees before touching anything:

- the target is a stable version (`vN.N.N`); pre-release or malformed inputs fail;
- a binary exists for every architecture currently served by `latest`;
- each SHA256 is computable.

On success it uploads the binaries to `binaries/latest/` first and the manifest
last, invalidates `/latest.json` and `/binaries/latest/*`, and logs
origin->target version and the actor. It never uses `--delete` and never touches
the versioned archives (`binaries/<version>/`), so you can panic again to climb
back up. Repointing `latest` to the version it already serves is a no-op.

Local dry run against a simulated CDN directory (no AWS):

```bash
scripts/release/panic.sh --version v1.7.4 --cdn-dir /path/to/cdnroot
```

where `cdnroot` contains `latest.json`, `binaries/latest/` and
`binaries/<version>/`.

## Freeze / unfreeze the ramp

A less drastic alternative to a rollback: stop the ramp from widening further
while you investigate, without changing the version. `freeze.sh` sets or clears
`frozen_at` in `latest.json` and leaves every other field, and the binaries,
untouched. It is idempotent in both directions and invalidates `/latest.json`.

Run it from the Actions tab: workflow **Freeze rollout**, input `action`
(`freeze` or `unfreeze`). Locally:

```bash
scripts/release/freeze.sh --freeze   --cdn-dir /path/to/cdnroot
scripts/release/freeze.sh --unfreeze --cdn-dir /path/to/cdnroot
```

A later stable release, or a panic rollback, publishes a fresh manifest with no
`frozen_at`, so freezing never leaks into the next version.

## How fast devices see a change

`latest.json` is served with `max-age=60` and the binaries with `max-age=3600`,
and every action invalidates the relevant CloudFront paths. After a panic or a
stable publish a device sees the new manifest within roughly the 60 s TTL plus
the invalidation, and then applies on its next auto poll.

## Testing the scripts locally

All four scripts run on `ubuntu-latest` and locally with only `bash`, `jq` and a
SHA256 tool, no AWS credentials. `gen-manifest.sh` writes a file; `panic.sh` and
`freeze.sh` take `--cdn-dir DIR` to read and write a local directory that mimics
the CDN layout, or `--dry-run` to preview. Seed a directory with fake binaries
named `thinr-agent.<arch>` and exercise the whole flow offline.

## Note on CLAUDE.md

The root `CLAUDE.md` states that only the `release: published` event updates
`latest.json`. That is not what the CI does. The stable channel is updated by
the **tag push** itself, in the `upload_to_s3` job of
`.github/workflows/build.yml` (it classifies any exact `vN.N.N` tag as a stable
release and promotes to `latest`). `release.yml` only creates the GitHub Release
on the same tag push. The publication therefore does not depend on the GitHub
Release existing, which is why the stable rule is decided by the tag name and
not by the release event.
