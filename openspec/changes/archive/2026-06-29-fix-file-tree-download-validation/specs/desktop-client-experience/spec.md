## ADDED Requirements

### Requirement: Desktop Visitor Download Resume
The desktop client SHALL support resumable downloads in Visitor Shell when shared file metadata and the share download endpoint support byte ranges.

#### Scenario: Visitor download resumes from partial file
- **WHEN** a visitor starts downloading a shared file and a valid partial target file already exists
- **THEN** the desktop client SHALL resume from the partial file using visitor share-token authentication and SHALL show the task as a ranged transfer

#### Scenario: Visitor download restarts when resume is unavailable
- **WHEN** a visitor starts downloading a shared file and the existing target file cannot be resumed
- **THEN** the desktop client SHALL restart the shared download as a full transfer and SHALL keep the visitor transfer task visible

### Requirement: Desktop Download Integrity Feedback
The desktop client SHALL verify completed owner and visitor downloads before presenting them as completed and SHALL surface size or hash validation failures as transfer errors.

#### Scenario: Download passes completion validation
- **WHEN** an owner or visitor download finishes and the local file matches expected size and available hash metadata
- **THEN** the desktop client SHALL mark the task completed and show full progress

#### Scenario: Download fails size validation
- **WHEN** an owner or visitor download finishes but the local file size differs from the expected size
- **THEN** the desktop client SHALL mark the task failed with a validation error and SHALL NOT show it as completed

#### Scenario: Download fails hash validation
- **WHEN** an owner or visitor download finishes but the local file hash differs from the expected hash metadata
- **THEN** the desktop client SHALL mark the task failed with a validation error and SHALL NOT show it as completed
