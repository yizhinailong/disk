# disk-tui

Terminal user interface for the [disk](../../) backend, written in Go with
[charmbracelet/bubbletea](https://github.com/charmbracelet/bubbletea) +
[lipgloss](https://github.com/charmbracelet/lipgloss).

`disk-tui` is an independent Go module. The repo's root CMake build does
**not** compile it — build it with the Go toolchain from this directory.

## Build & run

```bash
cd clients/disk-tui
make           # go mod tidy && go build -o bin/disk-tui .
./bin/disk-tui

# …or run directly
go run .
```

A Go 1.26+ toolchain is required (matches `go.mod`).

## Configuration

The base URL and JWT tokens persist to:

- `$XDG_CONFIG_HOME/disk-tui/config.json` (or `$HOME/.config/disk-tui/config.json`)

You can override at runtime:

- `DISK_BASE_URL` — overrides the base URL (default `http://127.0.0.1:8080/`)
- `DISK_TUI_CONFIG` — overrides the config file path

The login screen also lets you edit the server URL inline (`Ctrl+L` login /
`Ctrl+R` register).

## Screens

| Screen | Backend endpoints covered |
|---|---|
| Login / Register | `POST /api/auth/login`, `POST /api/auth/register`, `POST /api/auth/refresh`, `POST /api/auth/logout`, `GET /api/health` |
| Files | `POST /api/file/upload/init`, `POST /api/file/upload/chunk`, `POST /api/file/upload/complete`, `DELETE /api/file/upload/{upload_id}`, `GET /api/file/list`, `GET /api/file/{file_id}`, `GET /api/file/download/{file_id}/info`, `GET /api/file/download/{file_id}` (Range), `PUT /api/file/{file_id}/rename`, `PUT /api/file/move`, `POST /api/file/copy`, `DELETE /api/file`, `POST /api/file/delete`, `GET /api/file/search` |
| Folder | `POST /api/folder/create`, `GET /api/folder/tree`, `GET /api/folder/{folder_id}/breadcrumb`, `PUT /api/folder/{folder_id}/rename` |
| Shares | `POST /api/share`, `GET /api/share`, `GET /api/share/{share_id}`, `PUT /api/share/{share_id}`, `DELETE /api/share`, `POST /api/share/cancel` |
| Share Visitor | `POST /api/share/access/{share_id}`, `GET /api/share/browse/{share_id}`, `GET /api/share/download/{share_id}/{file_id}`, `POST /api/share/save/{share_id}` |
| Trash | `GET /api/trash`, `POST /api/trash/restore`, `DELETE /api/trash`, `POST /api/trash/delete`, `DELETE /api/trash/all` |
| Profile | `GET /api/user/profile`, `PATCH /api/user/profile`, `PUT /api/user/password`, `GET /api/user/storage` |
| Operation Logs | `GET /api/logs` |
| System | `GET /api/system/info`, `GET /api/health` |
| Admin Console | `GET /api/admin/users`, `GET /api/admin/users/{id}`, `PUT /api/admin/users/{id}/status`, `PUT /api/admin/users/{id}/role`, `PUT /api/admin/users/{id}/available-space`, `DELETE /api/admin/users/{id}`, `GET /api/admin/storage/stats`, `GET /api/admin/shares`, `GET /api/admin/shares/{share_id}`, `DELETE /api/admin/shares/{share_id}`, `GET /api/admin/stats/overview`, `GET /api/admin/stats/system`, `GET /api/admin/logs`；管理员分享路径使用字符串外部标识，不使用内部数据库主键 |

That is every backend endpoint registered in `src/controllers/`.

## Key bindings (quick reference)

Global:

- `Ctrl+C` — quit
- `Ctrl+X` — back to main menu
- `Ctrl+O` — logout

Files screen:

- `↑↓` / `jk` — navigate, `→`/`l` open, `←`/`h` up
- `␣` select item, `a` select all, `A` clear selection
- `n` new folder, `u` upload local file, `d` download
- `r`/`R` rename file/folder, `m` move, `c` copy, `D` delete
- `i` detail, `/` search, `s` cycle sort field, `o` toggle order, `t` type filter

Admin console:

- `1`–`4` switch tabs (users / shares / stats / logs)
- `s`/`e`/`q` — change user status / role / available space
- `D` — soft-delete user, `X` — force-cancel share

## Architecture

```
clients/disk-tui/
├── go.mod
├── main.go                 # entry point: loads config, builds client, runs TUI
└── internal/
    ├── client/             # pure HTTP client; one method per backend endpoint
    │   ├── client.go       # core: HTTP roundtrip, JWT refresh, envelope decode
    │   ├── auth.go         # /api/auth/* + /api/health
    │   ├── user.go         # /api/user/*
    │   ├── file.go         # /api/file/* (incl. chunked upload + download)
    │   ├── hash.go         # MD5 helpers for upload integrity
    │   ├── folder.go       # /api/folder/*
    │   ├── share.go        # /api/share/* (owner + visitor flows)
    │   ├── trash.go        # /api/trash/*
    │   ├── admin.go        # /api/admin/*
    │   ├── oplog.go        # /api/logs + /api/system/info
    │   └── types.go        # request/response DTOs
    ├── config/             # on-disk JSON config (URL + tokens)
    ├── tui/                # bubbletea screen-per-file TUI
    │   ├── app.go          # root model + dispatch
    │   ├── login.go        # login / register
    │   ├── menu.go         # main menu
    │   ├── files.go        # file browser
    │   ├── shares.go       # share manager (owner)
    │   ├── visitor.go      # share visitor (browse/save/download)
    │   ├── trash.go        # trash
    │   ├── profile.go      # user profile
    │   ├── logs.go         # operation logs
    │   ├── system.go       # system info
    │   ├── admin.go        # admin console (tabs)
    │   └── theme.go        # lipgloss styles
    └── util/               # byte/time formatting
```

### Auth handling

The client auto-attaches `Authorization: Bearer <access_token>` to every
non-public request. On a 401 (or backend auth-range error code) it tries
`POST /api/auth/refresh` exactly once, retries the original call, and
persists the new tokens via the `TokenSource` adapter.

The share visitor flow (`AccessShare` → `BrowseShare` / `DownloadShareFile`
/ `SaveShareItems`) sets the `X-Share-Token` header instead. Save also
requires the owner JWT.

### Uploads

`UploadFile(localPath, parentID, progress)` performs the full init → chunk
→ complete cycle, skipping chunks already uploaded (resumable uploads) and
honouring instant-upload (秒传) when the backend detects a hash match.
MD5 is computed for the whole file and per-chunk.
