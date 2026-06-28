## Purpose

TBD.

## Requirements

### Requirement: Desktop shell separation
The system SHALL define the desktop client as three mutually exclusive product flows: Owner Shell for authenticated regular users, Visitor Shell for share-link access, and Admin Shell for authenticated administrators. The desktop client SHALL keep owner JWT authentication, visitor share-token authentication, and administrator role routing separated.

#### Scenario: Owner user enters owner shell
- **WHEN** a user logs in with a JWT whose role is regular user
- **THEN** the desktop client SHALL route the session to Owner Shell and expose owner file, sharing, trash, transfer, and settings capabilities

#### Scenario: Administrator enters admin shell
- **WHEN** a user logs in with a JWT whose role is administrator
- **THEN** the desktop client SHALL route the session to Admin Shell instead of Owner Shell

#### Scenario: Visitor uses share token flow
- **WHEN** a user opens or enters a share link without logging in as an owner
- **THEN** the desktop client SHALL use Visitor Shell with share-token authentication and SHALL NOT expose Owner Shell navigation

#### Scenario: Authentication domains remain isolated
- **WHEN** owner, administrator, and visitor flows call backend APIs
- **THEN** owner and administrator APIs SHALL use bearer JWT authentication while visitor share browse/download APIs SHALL use share-token authentication without reusing owner JWT state

### Requirement: Desktop platform and client integration constraints
The desktop documentation SHALL preserve Linux-first, Windows-ready, Qt/QML client constraints and SHALL treat the desktop client as a pure consumer of existing backend REST APIs.

#### Scenario: Platform behavior specified
- **WHEN** desktop implementation guidance is reviewed
- **THEN** it SHALL require cross-platform Qt APIs, Linux as the primary validation target, and Windows build compatibility without introducing undocumented platform-specific runtime behavior

#### Scenario: Backend contract consumed
- **WHEN** a desktop feature maps to backend behavior
- **THEN** it SHALL consume documented existing REST APIs unless a separate accepted OpenSpec change adds or modifies backend routes

### Requirement: Owner explorer information architecture
The desktop Owner Shell SHALL model file-related navigation as a shared explorer context where My Files, Shares, and Trash are view modes within `PAGE-DRIVE`, while Transfers and Settings remain independent pages.

#### Scenario: File view mode switch
- **WHEN** the owner selects My Files, Shares, or Trash from the file-view navigation group
- **THEN** the desktop client SHALL switch the active data view inside `PAGE-DRIVE` without treating those entries as separate product pages

#### Scenario: Independent owner page switch
- **WHEN** the owner selects Transfers or Settings from the independent-page navigation group
- **THEN** the desktop client SHALL replace the current content with the corresponding independent page

### Requirement: Desktop object taxonomy
The desktop documentation SHALL classify UI objects using stable `PAGE-`, `VIEW-`, `COMP-`, `STATE-`, `FLOW-`, and `CASE-` identifiers and SHALL distinguish pages, view modes, panels, reusable components, and transient UI.

#### Scenario: Component classification
- **WHEN** documentation refers to the folder tree, breadcrumb bar, or page state view
- **THEN** it SHALL classify them as `COMP-*` objects rather than independent `PAGE-*` pages

#### Scenario: View mode classification
- **WHEN** documentation refers to My Files, Shares, or Trash in the owner explorer
- **THEN** it SHALL classify them as `VIEW-*` modes hosted by `PAGE-DRIVE`

### Requirement: Desktop navigation and state model
The desktop client documentation SHALL define navigation flows, page/session states, selection semantics, and transient feedback behavior for owner, visitor, and admin flows.

#### Scenario: Folder drill-down
- **WHEN** an owner opens a folder from My Files, breadcrumb, folder tree, or Up navigation
- **THEN** the documented behavior SHALL keep the user in the same `PAGE-DRIVE` instance, change the active folder context, clear selection, and refresh folder tree, list, and breadcrumb data

#### Scenario: Page state transition
- **WHEN** a loadable page or view begins loading data and receives success, empty, or failure results
- **THEN** the documented state model SHALL transition through `STATE-LOADING` to `STATE-CONTENT`, `STATE-EMPTY`, or `STATE-ERROR` accordingly

#### Scenario: Transient feedback priority
- **WHEN** dialogs, loading states, skeleton placeholders, progress indicators, and toast notifications could appear together
- **THEN** the documentation SHALL define their responsibilities and display priority so they do not become navigation states

### Requirement: Desktop interaction and layout contracts
The desktop documentation SHALL define the owner explorer layout, navigation rail behavior, toolbar actions, list interactions, dialogs, progress indicators, and error feedback using the evidence-status metadata semantics governed by the documentation-governance capability.

#### Scenario: Owner explorer layout reviewed
- **WHEN** the owner explorer layout is reviewed
- **THEN** the documentation SHALL identify the shell header, sidebar/navigation rail, storage/session area, toolbar card, status summary, content panel, optional status area, and view-specific layout differences

#### Scenario: Interaction matrix reviewed
- **WHEN** desktop interactions are reviewed
- **THEN** the documentation SHALL cover click, double-click, right-click, drag-and-drop, multi-select, hover, loading feedback, inline errors, modal dialogs, toast notifications, progress, and skeleton behavior with clear status labels

#### Scenario: Runtime interaction is documented with evidence status
- **WHEN** an interaction is documented as verified runtime behavior
- **THEN** the documentation SHALL identify its UI behavior and SHALL NOT present unsupported interactions such as drag-and-drop or skeleton screens as verified runtime behavior

#### Scenario: Future interaction is documented without behavior change
- **WHEN** an interaction describes future intended behavior rather than verified runtime behavior
- **THEN** the documentation SHALL preserve the desired behavior as a specification while clearly distinguishing it from verified runtime behavior

### Requirement: Desktop implementation traceability
The desktop documentation SHALL map product requirements to QML pages/components, C++ managers/models, context properties, and quick-test evidence where available.

#### Scenario: Implemented behavior has an anchor
- **WHEN** a desktop behavior is recorded as implemented
- **THEN** the documentation SHALL provide an implementation anchor such as a QML component, C++ manager/model, or test evidence reference

#### Scenario: Missing evidence is visible
- **WHEN** code exists but no focused quick-test or verification evidence exists
- **THEN** the documentation SHALL mark the behavior as pending verification rather than treating code presence alone as complete evidence

### Requirement: Admin desktop experience
The desktop documentation SHALL define Admin Shell as a standalone administrator workspace with user management, share management, operation logs, and system monitoring pages.

#### Scenario: Admin navigation rail
- **WHEN** an administrator enters Admin Shell
- **THEN** the desktop client SHALL expose user management, share management, operation logs, and system monitoring navigation items and SHALL NOT expose ordinary owner file browsing actions

#### Scenario: Destructive admin operation
- **WHEN** an administrator deletes a user or force-cancels a share
- **THEN** the documented UI behavior SHALL require an explicit confirmation interaction before invoking the destructive API

### Requirement: Chinese desktop UI terminology
The desktop documentation SHALL preserve a Chinese UI terminology glossary for desktop labels, actions, statuses, dialogs, share/trash wording, transfer states, and user-facing error messages.

#### Scenario: UI text translation update
- **WHEN** desktop UI copy is added or changed
- **THEN** the Chinese terminology glossary SHALL be updated or referenced so translations remain consistent across QML and C++ user-facing messages
