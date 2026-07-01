## ADDED Requirements

### Requirement: Backend Domain Extraction Validation
Validation documentation SHALL include characterization and regression coverage for backend content, quota/accounting, upload lifecycle, and trash lifecycle extraction.

#### Scenario: Upload lifecycle validation executed
- **WHEN** validation covers upload domain extraction
- **THEN** it SHALL verify normal init/chunk/complete, instant upload, finalize-time deduplication, cancellation, expiry cleanup, task terminal states, temporary cleanup, and unchanged API response envelopes

#### Scenario: Content reference validation executed
- **WHEN** validation covers content-domain extraction
- **THEN** it SHALL verify lookup/reuse, content creation, ref-count increments from upload/copy, ref-count decrements from permanent trash deletion, and zero-reference blob cleanup safety

#### Scenario: Quota accounting validation executed
- **WHEN** validation covers quota/accounting extraction
- **THEN** it SHALL verify reservation, insufficient quota rejection, reserved release, reserved-to-used commit, used-storage release on permanent trash deletion, and preservation of current instant-upload and trash accounting behavior

#### Scenario: Failure-domain validation executed
- **WHEN** validation covers DB and filesystem side effects around upload finalization or trash cleanup
- **THEN** it SHALL verify documented compensation or safe-skip behavior for promoted blobs, temporary artifacts, database rollback, and blob deletion failures
