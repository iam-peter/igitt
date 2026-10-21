#!/bin/bash
# Builds the APT repository with a stable and a nightly channel, signed with
# the only secret key in the GnuPG home. The site gets the repository in
# apt/ and the public key as key.asc.
#
# Usage: build-apt-repository.sh <packages> <site>
# <packages>/stable and <packages>/nightly hold the .deb of each channel,
# either may be empty.
set -euo pipefail
shopt -s nullglob

if [ $# -ne 2 ]; then
    echo "Usage: $0 <packages> <site>" >&2
    exit 2
fi
packages=$1
site=$2

key=$(gpg --list-secret-keys --with-colons | awk -F: '$1 == "fpr" {print $10; exit}')
if [ -z "$key" ]; then
    echo "No secret key to sign with" >&2
    exit 1
fi

repo=$(mktemp -d)
trap 'rm -rf "$repo"' EXIT
mkdir -p "$repo/conf" "$repo/pool" "$site/apt"

channels=(stable nightly)
for channel in "${channels[@]}"; do
    cat <<EOF
Origin: igitt
Label: igitt
Suite: $channel
Codename: $channel
Architectures: amd64
Components: main
Description: igitt $channel builds
SignWith: $key

EOF
done > "$repo/conf/distributions"

for channel in "${channels[@]}"; do
    for deb in "$packages/$channel"/*.deb; do
        reprepro -b "$repo" includedeb "$channel" "$deb"
    done
done
# Empty channels get their index too, so apt finds them
reprepro -b "$repo" export

mv "$repo/dists" "$repo/pool" "$site/apt/"
gpg --armor --export "$key" > "$site/key.asc"
