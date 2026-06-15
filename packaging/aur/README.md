# AUR packaging for imgcli

`PKGBUILD` + `.SRCINFO` to publish imgcli to the [Arch User Repository](https://aur.archlinux.org).
It builds from the tagged source tarball (`v0.2.0`) and installs the `imgcli` binary, license, and README.

## Test the build locally (on Arch)

```sh
cp PKGBUILD .SRCINFO /tmp/imgcli-aur && cd /tmp/imgcli-aur
makepkg -si          # build + install
namcap PKGBUILD      # optional lint
```

## Publish to the AUR (one-time, needs your AUR account + SSH key)

The AUR is a git host; publishing requires an [AUR account](https://aur.archlinux.org/register)
with an SSH key added to your profile. It cannot be done with a GitHub token.

```sh
git clone ssh://aur@aur.archlinux.org/imgcli.git aur-imgcli
cd aur-imgcli
cp /path/to/repo/packaging/aur/PKGBUILD .
makepkg --printsrcinfo > .SRCINFO          # regenerate to be safe
git add PKGBUILD .SRCINFO
git commit -m "imgcli 0.2.0"
git push
```

## On each new release

Bump `pkgver`, update the `sha256sums` to the new tag tarball
(`curl -L https://github.com/swperb/imgcli/archive/refs/tags/vX.Y.Z.tar.gz | sha256sum`),
regenerate `.SRCINFO`, commit and push.
