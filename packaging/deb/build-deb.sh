#!/bin/sh
# Builds the Debian package from the AppDir linuxdeploy fills for the
# AppImage. Only Qt and its ICU come along, in /opt/igitt: distributions
# ship a Qt too old for igitt. Every other library comes from the system
# and becomes a dependency, which dpkg-shlibdeps works out, so this has to
# run on the oldest system the package is for.
#
# Usage: build-deb.sh <AppDir> <version> <output.deb>
set -eu

if [ $# -ne 3 ]; then
    echo "Usage: $0 <AppDir> <version> <output.deb>" >&2
    exit 2
fi
appdir=$1
version=$2
output=$3
logo=$(cd "$(dirname "$0")/../../src/logo" && pwd)

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
# Under debian/ dpkg-shlibdeps takes the libraries in it as the package's own
root=$work/debian/igitt
prefix=$root/opt/igitt

mkdir -p "$prefix/lib" "$root/usr/bin" "$root/DEBIAN"
cp -a "$appdir/usr/bin" "$appdir/usr/plugins" "$appdir/usr/qml" "$appdir/usr/translations" "$prefix/"
cp -a "$appdir"/usr/lib/libQt6* "$appdir"/usr/lib/libicu* "$prefix/lib/"
ln -s /opt/igitt/bin/igitt "$root/usr/bin/igitt"
install -Dm644 "$logo/igitt.desktop" "$root/usr/share/applications/igitt.desktop"
install -Dm644 "$logo/igitt.svg" "$root/usr/share/icons/hicolor/scalable/apps/igitt.svg"
chmod -R u+rwX,go+rX,go-w "$root"

printf 'Source: igitt\n\nPackage: igitt\nArchitecture: amd64\n' > "$work/debian/control"
libs=$(cd "$work" && find debian/igitt/opt -type f \( -name '*.so*' -o -path '*/bin/igitt' \) \
    -exec dpkg-shlibdeps -O -l"$prefix/lib" {} + | sed -n 's/^shlibs:Depends=//p')

cat > "$root/DEBIAN/control" <<EOF
Package: igitt
Version: $version
Architecture: amd64
Maintainer: Henning Gruendl <henning.gruendl@qt.io>
Installed-Size: $(du -sk "$root" | cut -f1)
Depends: git (>= 1:2.40), $libs
Section: vcs
Priority: optional
Homepage: https://github.com/iam-peter/igitt
Description: Git history browser and commit tool
 igitt combines what gitk and git gui do: browsing the history, staging
 and committing. It also folds working tree changes into any earlier
 commit and edits the history like git rebase -i, without touching the
 working tree.
EOF

dpkg-deb --root-owner-group -Zxz -b "$root" "$output"
