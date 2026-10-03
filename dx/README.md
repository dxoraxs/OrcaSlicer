# dxoraxs fork of OrcaSlicer (MaxEllis MCP build)

`dx/main` = the newest MaxEllis release tag (`v*-mcp.*`, branch `remote-api-242` upstream)
plus our Remote API additions. Upstream code is untouched except the routes and declarations
in `RemoteAPIController.*`; our handlers live in `src/slic3r/GUI/RemoteAPI/RemoteAPIDx.cpp`
(and `handle_project_save` in the controller).

## Remote API additions

| Route | Body | Does |
|-------|------|------|
| `POST /api/v1/project/save` | `{"path"?, "overwrite"?}` | save the project as .3mf, no dialog; no path = in place |
| `POST /api/v1/project/new` | `{"discard"?}` | empty project |
| `POST /api/v1/project/open` | `{"path", "discard"?}` | open a .3mf as a project (models, plates, settings) |
| `GET /api/v1/plates` | | plates with their objects, current index |
| `POST /api/v1/plates` | `{"name"?}` | add a plate and select it |
| `POST /api/v1/plates/select` | `{"index"}` | select a plate |
| `DELETE /api/v1/plates/{index}` | | delete an empty plate (never the last) |
| `POST /api/v1/objects/{id}/plate` | `{"index"}` | move an object to a plate |

Nothing here may open a modal dialog (a modal blocks the GUI loop and every later API call
times out): with unsaved changes, new/open answer 409 `unsaved_changes` unless `discard: true`.
`/status` lists `project_save`, `project_open`, `plates` in `capabilities`.

## Builds and updates

- `dx-build-macos` builds macOS arm64 and replaces the .dmg in the rolling release `orca-dx`.
  The Actions token may not create tags in this fork, so that release was created once by hand
  (`gh release create orca-dx --target dx/main` with a personal token); the workflow only
  swaps its asset and notes.
- `dx-sync-upstream` (daily) merges a new MaxEllis release and starts the build; a conflict or a
  refused push opens an issue, then run `dx/sync-upstream.sh` locally.
