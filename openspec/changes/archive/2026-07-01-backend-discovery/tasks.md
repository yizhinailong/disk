## Implementation Tasks

### 1. Filter and rate-limit discovery

- [x] Map configured global filters from `config.json`, including each `GlobalFilters` entry and exemption list.
- [x] Map route-level filters on representative file, upload, download, admin, folder, auth, and share routes.
- [x] Verify whether global and route-level `JwtAuthFilter` both execute for protected file routes.
- [x] Verify upload endpoints are JWT-protected and upload-rate-limited exactly as currently configured.
- [x] Verify download endpoints are JWT-protected and download-rate-limited exactly as currently configured.
- [x] Verify public auth, health, and public share endpoints remain reachable without JWT where configured.
- [x] Document Redis failure policy for upload, download, register, admin, folder, and public share rate-limit filters.

### 2. Consistency flow discovery

- [x] Document current upload state transitions for init, resume, chunk, complete, cancel, and expire.
- [x] Document every confirmed path that updates `users.storage_reserved`.
- [x] Document every confirmed path that updates `users.storage_used`.
- [x] Document current `file_contents.ref_count` increment and decrement paths.
- [x] Document current physical temp-file and blob deletion paths, including compensation behavior.
- [x] Document current trash behavior, including whether trashed items continue to count against quota until permanent deletion.
- [x] Document current private and share download metadata side effects for `files.download_count`, `files.last_accessed_at`, and `shares.download_count`.

### 3. Discovery artifact creation

- [x] Create a backend discovery note with filter/rate-limit behavior, upload lifecycle, content lifecycle, quota lifecycle, trash lifecycle, storage deletion paths, and download metadata side effects.
- [x] Separate confirmed current behavior from open product or architecture questions.
- [x] Cite source locations, tests, commands, or runtime observations for each major conclusion.
- [x] Cross-reference the discovery note from `docs/TODO.md` or otherwise make it discoverable from the backend refactor TODO.

### 4. Validation

- [x] Run or add targeted characterization checks needed to confirm framework-dependent filter execution behavior.
- [x] Run relevant existing filter tests if available.
- [x] Review the discovery note against the OpenSpec `backend-discovery` requirements.
- [x] Confirm no application behavior, database schema, public API response shape, or storage layout changed as part of this discovery change.
