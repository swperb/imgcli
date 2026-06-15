# winget manifest for imgcli

These three files are the [winget](https://learn.microsoft.com/windows/package-manager/)
manifest for `swperb.imgcli`, pinned to the **v0.3.0** Windows release
(`imgcli-windows-x86_64.zip`, a static mingw-w64 build). They install the nested
`imgcli\imgcli.exe` as a portable command aliased to `imgcli`.

## Validate locally (on Windows)

```powershell
winget validate --manifest packaging\winget
winget install --manifest packaging\winget   # test install from the local manifest
```

## Publish to the public winget repo

winget packages live in [microsoft/winget-pkgs](https://github.com/microsoft/winget-pkgs);
publishing is a reviewed pull request. The easiest path is
[`wingetcreate`](https://github.com/microsoft/winget-create):

```powershell
winget install Microsoft.WingetCreate
# Regenerates + submits the PR (prompts through the fields, then opens the PR):
wingetcreate update swperb.imgcli `
  --version 0.3.0 `
  --urls https://github.com/swperb/imgcli/releases/download/v0.3.0/imgcli-windows-x86_64.zip `
  --submit
```

Or copy these files into `manifests/s/swperb/imgcli/0.3.0/` in a fork of
microsoft/winget-pkgs and open a PR. Their CI validates the manifest and the
install; a maintainer (or the bot) merges it.

## Bumping the version

On each release, update `PackageVersion` in all three files and the
`InstallerUrl` + `InstallerSha256` in the installer manifest (the sha256 is the
uppercase hash of the new `imgcli-windows-x86_64.zip`), then re-submit.
