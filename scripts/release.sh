#!/bin/zsh
# Aamosphere release build: universal (Apple silicon + Intel), signed with
# Developer ID, hardened runtime, notarised and stapled, packed as a zip and
# an installer .pkg. Every plugin repo carries this same script.
#
#   scripts/release.sh --local   build + check only (no Apple account needed)
#   scripts/release.sh           full release
#
# One-time setup for a full release (needs the Apple Developer Program):
#   1. In Xcode > Settings > Accounts, create the certificates
#      "Developer ID Application" and "Developer ID Installer".
#   2. Make an app-specific password at account.apple.com, then:
#        xcrun notarytool store-credentials AAMO_NOTARY \
#            --apple-id <your apple id> --team-id <TEAMID> --password <app password>
#   3. export AAMO_TEAM=<TEAMID>   (e.g. in ~/.zshrc)
#
# Output goes to dist/<Product>-<version>/.

set -euo pipefail
cd "${0:A:h}/.."

LOCAL=0
[[ "${1:-}" == "--local" ]] && LOCAL=1

NAME=$(sed -n 's/^project(\([A-Za-z0-9_]*\) VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt | head -1)
VERSION=$(sed -n 's/^project([A-Za-z0-9_]* VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt | head -1)
[[ -n "$NAME" && -n "$VERSION" ]] || { echo "project(<Name> VERSION x.y.z) not found in CMakeLists.txt"; exit 1; }

say() { print -P "%F{yellow}==>%f $*"; }
fail() { print -P "%F{red}FAILED:%f $*"; exit 1; }

# ---------------------------------------------------------------- identities
if (( ! LOCAL )); then
    TEAM=${AAMO_TEAM:?"set AAMO_TEAM to your Apple team ID (or use --local)"}
    PROFILE=${AAMO_NOTARY_PROFILE:-AAMO_NOTARY}
    APP_ID=$(security find-identity -v -p codesigning | grep "Developer ID Application" | grep "($TEAM)" | head -1 | awk '{print $2}')
    INST_ID=$(security find-identity -v | grep "Developer ID Installer" | grep "($TEAM)" | head -1 | sed 's/.*"\(.*\)"/\1/')
    [[ -n "$APP_ID" ]] || fail "no 'Developer ID Application' certificate for team $TEAM in the keychain"
    xcrun notarytool history --keychain-profile "$PROFILE" >/dev/null 2>&1 \
        || fail "notary profile '$PROFILE' missing: run xcrun notarytool store-credentials (see top of this script)"
fi

# ---------------------------------------------------------------- build
BUILD=build-release
say "Building $NAME $VERSION (Release, arm64 + x86_64)"
cmake -S . -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" >/dev/null
cmake --build "$BUILD" --config Release --target "${NAME}_AU" "${NAME}_VST3" -j "$(sysctl -n hw.ncpu)" >/dev/null \
    || fail "build failed (run: cmake --build $BUILD --config Release to see why)"

BUNDLES=("${(@f)$(find "$BUILD/${NAME}_artefacts/Release" -maxdepth 2 \( -name '*.component' -o -name '*.vst3' \))}")
(( ${#BUNDLES} == 2 )) || fail "expected one AU and one VST3 bundle, found: ${BUNDLES[*]}"

# ---------------------------------------------------------------- checks
for b in $BUNDLES; do
    exe=("$b"/Contents/MacOS/*)
    archs=$(lipo -archs "$exe[1]")
    [[ "$archs" == *arm64* && "$archs" == *x86_64* ]] || fail "$b is not universal ($archs)"
    minos=$(vtool -show-build "$exe[1]" | awk '/minos/{print $2; exit}')
    [[ "$minos" == "11.0" ]] || fail "$b targets macOS $minos, expected 11.0"
    say "$(basename "$b"): $archs, macOS $minos+"
done

if (( LOCAL )); then
    say "Local check OK. Bundles are in $BUILD/${NAME}_artefacts/Release"
    exit 0
fi

# ---------------------------------------------------------------- sign
for b in $BUNDLES; do
    say "Signing $(basename "$b")"
    codesign --force --sign "$APP_ID" --options runtime --timestamp "$b"
    codesign --verify --strict --verbose=1 "$b" || fail "signature check failed for $b"
done

# ---------------------------------------------------------------- notarise
OUT="dist/${NAME}-${VERSION}"
rm -rf "$OUT" && mkdir -p "$OUT"
ZIP="$OUT/${NAME}-${VERSION}-notarise.zip"
STAGE=$(mktemp -d)
cp -R $BUNDLES "$STAGE/"
ditto -c -k "$STAGE" "$ZIP"
say "Notarising (this takes a few minutes)"
xcrun notarytool submit "$ZIP" --keychain-profile "$PROFILE" --wait | tee "$OUT/notary.log"
grep -q "status: Accepted" "$OUT/notary.log" || fail "notarisation rejected, see $OUT/notary.log and: xcrun notarytool log <id> --keychain-profile $PROFILE"
rm "$ZIP"
for b in $BUNDLES; do xcrun stapler staple "$b" >/dev/null && xcrun stapler validate "$b" >/dev/null || fail "stapling failed for $b"; done

# ---------------------------------------------------------------- package
rm -rf "$STAGE"; mkdir -p "$STAGE/Components" "$STAGE/VST3"
for b in $BUNDLES; do
    [[ "$b" == *.component ]] && cp -R "$b" "$STAGE/Components/" || cp -R "$b" "$STAGE/VST3/"
done
ditto -c -k "$STAGE" "$OUT/${NAME}-${VERSION}-mac.zip"

if [[ -n "$INST_ID" ]]; then
    ID="com.aamosphere.$(echo "$NAME" | tr 'A-Z' 'a-z')"
    pkgbuild --root "$STAGE/Components" --install-location "/Library/Audio/Plug-Ins/Components" --identifier "$ID.au" --version "$VERSION" "$STAGE/au.pkg" >/dev/null
    pkgbuild --root "$STAGE/VST3" --install-location "/Library/Audio/Plug-Ins/VST3" --identifier "$ID.vst3" --version "$VERSION" "$STAGE/vst3.pkg" >/dev/null
    PKG="$OUT/${NAME}-${VERSION}-mac.pkg"
    productbuild --package "$STAGE/au.pkg" --package "$STAGE/vst3.pkg" --sign "$INST_ID" "$PKG" >/dev/null
    say "Notarising the installer"
    xcrun notarytool submit "$PKG" --keychain-profile "$PROFILE" --wait | tee "$OUT/notary-pkg.log"
    grep -q "status: Accepted" "$OUT/notary-pkg.log" || fail "installer notarisation rejected, see $OUT/notary-pkg.log"
    xcrun stapler staple "$PKG" >/dev/null
    spctl --assess --type install --verbose "$PKG" || fail "Gatekeeper rejects $PKG"
else
    say "No 'Developer ID Installer' certificate: skipping the .pkg (zip is still signed and notarised)"
fi
rm -rf "$STAGE"
say "Done: $OUT"
