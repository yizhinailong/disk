# Kubernetes reference deployment

This directory is a production reference, not a directly deployable environment.
Every `replace-with-*` value and the example image tag must be replaced in a
reviewed overlay. Release images must be pinned by digest.

The minimum supported Kubernetes version is 1.30. The committed 4-replica HPA
ceiling, hard same-role host anti-affinity, and one rolling surge require at
least five schedulable nodes across two real zones. The cluster must also
provide Metrics Server and an External Metrics API that exposes the two series from
`deploy/prometheus/disk-autoscaling.yml` without renaming them.
Prometheus discovery must rewrite the target `job` label from each Pod's
`prometheus.io/job` annotation, preserving `disk-api` and `disk-worker` as the
stable values used by alert and autoscaling rules.

## Resource layers

- `platform/` creates the `disk` Namespace, the tokenless runtime ServiceAccount,
  and non-secret runtime configuration.
- `migration/` renders only the one-shot expand migration Job. Every release
  overlay must add a unique, DNS-safe `nameSuffix`; use `kubectl create -k` so
  each release produces one auditable execution instead of reusing a prior Job.
- `workloads/worker/` and `workloads/api/` are independent release entries for
  each role's Deployment, disruption budget, and autoscaler; the API entry also
  owns the API-only Service. `workloads/` aggregates both entries only for
  steady-state rendering after both role Deployments already run the reviewed
  digest.

The repository deliberately contains no Secret manifest. Provision
`disk-runtime-secrets` in the `disk` Namespace through the platform secret
manager with these required keys:

```text
DATABASE_PASSWORD
REDIS_PASSWORD
REDIS_CA_CERT
JWT_SECRET
DISK_S3_ACCESS_KEY
DISK_S3_SECRET_KEY
```

`DISK_S3_SESSION_TOKEN` is optional for short-lived credentials. Do not create
the Secret with command-line literals because shell history and release logs are
not approved secret stores.

The application image uses Drogon's plaintext Redis client only against
`127.0.0.1:6379` inside each Pod. The `redis-tls-proxy` HAProxy sidecar owns the
private upstream connection, requires TLS 1.2 or newer, validates the CA and
certificate hostname, sends SNI, and re-resolves the stable writer name. Replace
both `REDIS_TLS_UPSTREAM_HOST` and `REDIS_TLS_SERVER_NAME` in the target overlay,
mount the reviewed PEM CA as `REDIS_CA_CERT`, and pin the HAProxy image by digest
alongside the application image. Redis authentication remains end-to-end at the
RESP layer through `REDIS_PASSWORD`; the proxy never receives that secret.

The runtime image and every Pod security context use numeric UID/GID `10001`.
Keep that identity aligned in overlays: kubelet must be able to prove
`runAsNonRoot`, and the writable `emptyDir` mounts must remain group-writable by
that process identity.

## Release contract

`release-plan.json` is the machine-readable ordering contract. A release must:

1. Render all three layers, reject placeholders or non-digest images, perform
   server-side dry-runs, validate the Redis TLS proxy/CA policy, apply the
   platform layer, verify five eligible nodes in two zones, and pass the
   repository capacity checker for both `2:2` and `4:4`.
2. Create exactly one release-suffixed migration Job, wait for `Complete`, and
   archive its logs plus the `schema_migrations` version/checksum snapshot.
3. Apply `workloads/worker/` with `maxUnavailable=0/maxSurge=1` and wait for
   both readiness and task-lease evidence.
4. Apply `workloads/api/` with the same one-process surge boundary and wait for
   Service probes and cross-instance business checks.
5. Verify both HPA objects can read CPU and external metrics, then exercise a
   Redis writer DNS failover through both roles before accepting the release.

On an application rollback, freeze new upload-task creation using the documented
gate, roll API back before Worker, and wait for each role to drain. Preserve the
expand schema. Kubernetes automation must never invoke a destructive migration
rollback.

The static repository contract is available without a cluster:

```bash
uv run test/integration/test_distributed_topology.py
```

When `kubectl` is available, also render each layer locally and run the rendered
candidate through the target cluster's server-side dry-run before creating the
migration Job.
