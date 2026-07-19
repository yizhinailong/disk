\set ON_ERROR_STOP on

CREATE TABLE IF NOT EXISTS storage_reconciliation_findings (
    id BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    finding_type VARCHAR(64) NOT NULL,
    resource_id VARCHAR(128) NOT NULL,
    resource_locator VARCHAR(1024) DEFAULT NULL,
    severity SMALLINT NOT NULL,
    resolution_strategy VARCHAR(32) NOT NULL,
    details JSONB NOT NULL DEFAULT '{}'::jsonb,
    occurrences INTEGER NOT NULL DEFAULT 1,
    first_seen_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    last_seen_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    resolved_at TIMESTAMP DEFAULT NULL,
    CONSTRAINT uk_storage_reconciliation_finding
        UNIQUE (finding_type, resource_id),
    CONSTRAINT ck_storage_reconciliation_severity
        CHECK (severity BETWEEN 0 AND 2),
    CONSTRAINT ck_storage_reconciliation_strategy
        CHECK (resolution_strategy IN ('auto_gc', 'alert', 'manual')),
    CONSTRAINT ck_storage_reconciliation_occurrences
        CHECK (occurrences > 0)
);

CREATE INDEX IF NOT EXISTS idx_storage_reconciliation_unresolved
    ON storage_reconciliation_findings (severity DESC, last_seen_at, id)
    WHERE resolved_at IS NULL;

COMMENT ON TABLE storage_reconciliation_findings IS
    'Persistent storage, reference, and quota reconciliation findings';
COMMENT ON COLUMN storage_reconciliation_findings.resource_id IS
    'Bounded database aggregate ID or SHA-256 object-key digest';
COMMENT ON COLUMN storage_reconciliation_findings.resource_locator IS
    'Credential-free object key or database locator';
