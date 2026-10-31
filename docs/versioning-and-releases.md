# Versions, migration and releases

The root `CMakeLists.txt` is the engine version source. Release versions use
`MAJOR.MINOR.PATCH`: MAJOR changes compatibility, MINOR adds compatible features,
and PATCH fixes defects. During 0.x development, format compatibility is checked
explicitly; a newer version number alone is never a migration implementation.
Only stable three-part versions are published by this workflow. Prerelease and
build-metadata tags are deliberately rejected.

The editor displays its version and checks GitHub's latest published stable release
once at startup, in a background task. The request uses HTTPS certificate checking,
a five-second timeout and a 1 MiB response limit. Offline operation and an empty
release history do not prevent editing. “Releases / Download update” opens the
repository's release page. Installation is manual: extract the new package to a
separate directory and run its launcher. Running binaries are never overwritten.

## Project manifest

`project.bengine` is the manifest; it continues to reference the main scene:

```json
{
  "format": "bubble-engine-project",
  "version": 2,
  "engine_version": "0.1.0",
  "minimum_engine_version": "0.1.0",
  "project_version": "0.1.0",
  "scene": "main.scene"
}
```

`version` is the integer serialization schema, independent of both SemVer values.
`engine_version` records the engine that last saved the manifest;
`minimum_engine_version` is its required engine floor. `project_version` belongs
to the game and survives editor saves; editing it never changes the engine version.
Projects saved by newer engines or requiring a newer engine are refused before
replacing ECS state or project files. Update the engine and reopen the project.
Scene documents retain their existing format version 1.

## Incremental migration and recovery

`project_migration_manager` applies ordered format transitions in memory.
Currently 1 → 2 adds version metadata and preserves all existing JSON members.
Each future transition must have its own conversion and validation tests. There
is no guessed conversion for unsupported future formats.

Loading validates the converted manifest, scene, hierarchy, components and resources
in a staged ECS before writing the migration or replacing the active scene.
The original manifest is copied byte-for-byte to
`project.bengine.migration-backup-N/project.bengine`; the copy is read back and
verified before replacement. Only the manifest changes in 1 → 2, so scenes,
scripts and assets require no conversion and remain untouched. Every future
migration that changes additional files must back up those files too.

Replacement reuses the existing transaction journal: interrupted writes are
recovered on the next load. Backups are retained, never overwritten, and excluded
from game exports. To restore, close the editor and copy the backup manifest over
the project manifest. If a `.transaction` exists, let recovery finish before
restoring. This protocol protects against process interruption; it does not claim
power-loss durability (`fsync` is not provided by the existing transaction layer).
The runtime converts legacy metadata in memory without writing to game packages.

## Publishing

1. Set `VERSION 0.2.0` in the root `CMakeLists.txt` and commit the release changes.
2. Run `git tag v0.2.0` and `git push origin v0.2.0`.

The release workflow first rejects invalid or mismatched tags, then builds and
runs CTest on Linux and Windows. It builds relocatable release binaries, collects
shared dependencies, assets and sources needed for the existing game export
workflow, and uploads ZIP packages and SHA-256 files as Actions artifacts. Only
when both platforms succeed does it publish a GitHub Release with both packages,
checksums and a changelog containing generated release notes and commit history.
The workflow needs repository permission to publish releases.

Extract the package and run `BubbleEngine.sh` on Linux or `BubbleEngine.cmd` on
Windows. These launchers set the installation root and asset working directory.
Linux graphics drivers and system C runtime remain supplied by the host. Building
exported games still requires the existing platform build dependencies/toolchains.
Editor development builds additionally require libcurl headers/library
(`libcurl4-openssl-dev` on Ubuntu, `curl:x64-windows` through vcpkg on Windows).
