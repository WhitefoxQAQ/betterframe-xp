#!/bin/sh
# Build the release files: dist/betterframe-xp-<version>.tar.gz and dist/SHA256SUMS.
# Only committed files go in (git archive), so the work folder's leftovers and tests stay out.
# Run after the commit that the tag v<version> points to (HEAD is archived).
#   sh scripts/package.sh 0.1.1
set -eu
cd "$(dirname "$0")/.."

ver=${1:?usage: sh scripts/package.sh VERSION}
[ "$(cat VERSION)" = "$ver" ] || { echo "VERSION says $(cat VERSION), not $ver" >&2; exit 1; }

name="betterframe-xp-$ver.tar.gz"
mkdir -p dist
git archive --format=tar.gz -o "dist/$name" HEAD -- VERSION LICENSE THIRD_PARTY.md README.md CHANGELOG.md \
    layer panel contrib ':(exclude)panel/tests'
(cd dist && sha256sum "$name" >SHA256SUMS)
echo "dist/$name"
echo "dist/SHA256SUMS"
