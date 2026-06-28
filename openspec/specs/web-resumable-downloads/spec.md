# Web Resumable Downloads Specification

## Purpose

Defines Web client download task behavior for runtime pause, continuation, cancellation, progress accounting, and byte-range resumption.

## Requirements

### Requirement: Web Runtime Download Task State
The Web client SHALL maintain runtime download task state that records the file identity, expected total size, received byte count, range support, current status, progress, and any partial response data needed to continue a paused download.

#### Scenario: Download receives bytes
- **WHEN** a Web download receives response chunks from the server
- **THEN** the Web client SHALL increase the task received byte count and update progress from the received byte count and expected total size

#### Scenario: Download task reaches terminal state
- **WHEN** a Web download is completed, cancelled, removed, or fails without retry
- **THEN** the Web client SHALL release abort controllers and partial response data that are no longer needed for continuation

### Requirement: Web Download Pause
The Web client SHALL pause an active download by aborting the in-flight request while preserving the received byte count and reusable partial content for resumable files.

#### Scenario: Pause active ranged download
- **WHEN** the user pauses an active Web download for a file that supports range requests
- **THEN** the Web client SHALL abort the current request, mark the task as paused, and preserve the bytes already received for later continuation

#### Scenario: Pause non-resumable download
- **WHEN** the user pauses an active Web download for a file that does not support range requests
- **THEN** the Web client SHALL make clear that continuation from the previous offset is unavailable and SHALL NOT claim that already received bytes will be reused

### Requirement: Web Download Resume
The Web client SHALL resume paused downloads from the current received byte offset when range continuation is supported and SHALL validate the resumed response before appending data.

#### Scenario: Resume ranged download
- **WHEN** the user resumes a paused Web download whose received byte count is greater than zero and whose metadata supports range requests
- **THEN** the Web client SHALL request `Range: bytes=<received_bytes>-` and continue progress from the existing received byte count

#### Scenario: Resumed response matches requested offset
- **WHEN** the server responds to a resumed Web download request with HTTP 206 and a `Content-Range` starting at the requested byte offset
- **THEN** the Web client SHALL append the new response bytes after the preserved partial bytes

#### Scenario: Resumed response does not match requested offset
- **WHEN** the server response for a resumed Web download cannot be proven to continue at the requested byte offset
- **THEN** the Web client SHALL NOT append the response to preserved partial bytes and SHALL fail or restart the task deterministically without corrupting the saved file

### Requirement: Web Download Completion
The Web client SHALL save a completed download only after assembling the exact bytes received for the full file.

#### Scenario: Resumed download completes
- **WHEN** a paused ranged Web download resumes and all remaining bytes are received
- **THEN** the Web client SHALL save a blob assembled from preserved partial bytes plus resumed bytes and mark the task completed with 100 percent progress

#### Scenario: Download is cancelled
- **WHEN** the user cancels a Web download
- **THEN** the Web client SHALL abort any active request, discard partial bytes, and mark the task cancelled
