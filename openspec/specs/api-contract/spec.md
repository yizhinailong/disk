# Api Contract Specification

## Purpose

Defines common REST API response, error, authentication header, pagination, tracing, and download response behavior shared by backend clients.

## Requirements

### Requirement: Uniform JSON API Envelope
The system SHALL return JSON API responses using a consistent envelope containing `code`, `message`, and `data`, except for successful binary download responses.

#### Scenario: Successful JSON response
- **WHEN** an API operation completes successfully and returns JSON
- **THEN** the response SHALL include `code: 0`, `message: "success"`, and a `data` value

#### Scenario: Domain error response
- **WHEN** an API operation fails with a domain error
- **THEN** the response SHALL include a stable business error `code`, a human-readable `message`, and `data` set to null or structured error details

### Requirement: Error Code Contract
The system SHALL expose stable business error codes for common, authentication, file, share, Redis, and administrator error domains.

#### Scenario: Known domain failure
- **WHEN** a request fails due to a known domain condition
- **THEN** the system SHALL map the failure to the documented business error code and an appropriate HTTP status code

#### Scenario: Known error uses its default message
- **WHEN** a known business error is returned without a custom message
- **THEN** the response SHALL use that error code's human-readable default message and SHALL NOT fall back to `Unknown error`

#### Scenario: Redis dependency errors expose no upstream diagnostics
- **WHEN** a Redis command, result conversion, or key lookup returns `RedisOperationFailed` or `RedisKeyNotFound`
- **THEN** the error SHALL use the documented fixed default message and SHALL NOT expose the Redis exception text, endpoint, connection details, key, value, command parameters, or credentials

#### Scenario: S3 dependency errors expose no provider diagnostics
- **WHEN** an S3 SDK failure is converted to an API or durable-job domain error
- **THEN** the error SHALL contain only its business code and bounded fixed operation message and SHALL NOT expose provider error codes or messages, endpoints, buckets, object keys, multipart identifiers, signatures, or credentials

#### Scenario: Durable Worker errors are bounded summaries
- **WHEN** the current Worker persists a new retry or dead-letter error that originated from task validation, a dependency result, or an exception
- **THEN** `last_error` SHALL contain only a fixed task-contract, configuration, state, or operation summary and SHALL NOT copy a downstream domain message, unknown job type, exception text, SQL, connection detail, endpoint, object locator, payload value, or credential

### Requirement: Pagination Envelope
The system SHALL return paginated collection results with item data and pagination metadata.

#### Scenario: Paginated list response
- **WHEN** an API returns a paginated collection
- **THEN** the response data SHALL include `items` and pagination fields such as page, page size, total count, and total pages

### Requirement: Authentication Header Contract
The system SHALL use `Authorization: Bearer <access_token>` for authenticated user and administrator APIs and `X-Share-Token` for visitor share APIs.

#### Scenario: Authenticated owner request
- **WHEN** a client calls a protected owner API
- **THEN** the client SHALL provide a bearer access token in the `Authorization` header

#### Scenario: Visitor share request
- **WHEN** a visitor browses or downloads shared content
- **THEN** the client SHALL provide the share access token in the `X-Share-Token` header

### Requirement: Binary Download Contract
The system SHALL support successful binary download responses outside the JSON envelope while preserving JSON error responses for download failures, and clients SHALL bypass JSON envelope unwrapping for successful binary responses.

#### Scenario: Successful binary download
- **WHEN** a client downloads file content successfully
- **THEN** the response SHALL return binary content with appropriate download headers rather than the JSON envelope

#### Scenario: Client receives successful binary response
- **WHEN** a client receives a successful download response configured for binary or Blob content
- **THEN** the client SHALL preserve the binary body and SHALL NOT require `code`, `message`, or `data` envelope fields

#### Scenario: Download error
- **WHEN** a download request fails before streaming content
- **THEN** the response SHALL use the standard JSON error envelope

### Requirement: Request Trace Propagation
The system SHALL propagate request trace identifiers to responses when a request trace ID is available.

#### Scenario: Request trace ID exists
- **WHEN** a request has an associated trace identifier
- **THEN** the response SHALL include it as `X-Request-Id`
