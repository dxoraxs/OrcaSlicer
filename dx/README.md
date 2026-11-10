# dxoraxs fork of OrcaSlicer (MaxEllis MCP build)

`main` = the newest MaxEllis release tag (`v*-mcp.*`) plus this folder, two workflows and a
README note. There are no source changes.

Until 2.4.2 the fork carried its own Remote API routes (project save/new/open, plates). Upstream
shipped its own in `v2.4.2-mcp.13` to `mcp.15`, with different request bodies, so at
`v2.5.0-mcp.16` ours were dropped: the merge commit takes upstream's tree as it is. The old
handlers stay in history (`RemoteAPIDx.cpp`, last in commit 29a9d868aa).

## Builds and updates

- `dx-build-macos` builds macOS arm64 and replaces the .dmg in the rolling release `orca-dx`.
  The Actions token may not create tags in this fork, so that release was created once by hand
  (`gh release create orca-dx --target main` with a personal token); the workflow only
  swaps its asset and notes.
- `dx-sync-upstream` (daily) merges a new MaxEllis release and starts the build; a conflict or a
  refused push opens an issue, then run `dx/sync-upstream.sh` locally. Upstream rebuilds its
  branch on each new OrcaSlicer base (2.4 to 2.5 did), and a plain merge then conflicts in
  thousands of files: take upstream's tree whole (`git merge -s ours --no-commit <tag>`,
  `git read-tree -u --reset <tag>`, then restore `dx/`, the two `dx-*.yml` workflows and the
  README note from `main`).
