#!/bin/bash
#
# release.sh - Release management for basika
#
# Usage:
#   ./release.sh              Show current version and status
#   ./release.sh tag          Tag the current commit and push
#   ./release.sh package      Build the distribution zip
#   ./release.sh release      Create GitHub release and upload assets
#   ./release.sh formula      Update the Homebrew formula with the correct version and SHA256
#   ./release.sh app          Build Burrow-VERSION.zip, the Burrow Mac app (macOS only)
#   ./release.sh cask         Update the Burrow Homebrew cask with the correct version and SHA256
#   ./release.sh all          Run all steps: tag, package, release, formula and cask update
#
# Requires: gh (GitHub CLI), authenticated via `gh auth login`
#

set -euo pipefail

VERSION=$(grep 'BASIKA_VERSION' src/common.h | head -1 | sed 's/.*"\(.*\)".*/\1/')
TAG="v${VERSION}"
FORMULA="Formula/basika.rb"
PACKAGE_NAME="basika-${VERSION}"
ARCHIVE_URL="https://github.com/kdekorte/basika/releases/download/${TAG}/${PACKAGE_NAME}.zip"
CASK="Casks/burrow.rb"
BURROW_ZIP="Burrow-${VERSION}.zip"
BURROW_URL="https://github.com/kdekorte/basika/releases/download/${TAG}/${BURROW_ZIP}"

info()    { printf "\033[1;34m==>\033[0m \033[1m%s\033[0m\n" "$1"; }
success() { printf "\033[1;32m==>\033[0m \033[1m%s\033[0m\n" "$1"; }
error()   { printf "\033[1;31mError:\033[0m %s\n" "$1" >&2; exit 1; }

show_status() {
    info "Basika release status"
    echo "  Version:      ${VERSION}"
    echo "  Tag:          ${TAG}"
    echo "  Formula:      ${FORMULA}"
    echo "  Cask:         ${CASK}"
    echo "  Archive URL:  ${ARCHIVE_URL}"
    echo ""

    if git rev-parse "${TAG}" >/dev/null 2>&1; then
        success "Tag ${TAG} exists locally"
    else
        echo "  Tag ${TAG} has not been created yet"
    fi

    if command -v gh >/dev/null 2>&1; then
        if gh release view "${TAG}" >/dev/null 2>&1; then
            success "GitHub release ${TAG} exists"
        else
            echo "  GitHub release ${TAG} has not been created yet"
        fi
    fi
}

do_tag() {
    info "Tagging release ${TAG}"

    if git rev-parse "${TAG}" >/dev/null 2>&1; then
        error "Tag ${TAG} already exists. Bump BASIKA_VERSION in src/common.h first."
    fi

    if ! git diff --quiet || ! git diff --cached --quiet; then
        error "Working tree is dirty. Commit all changes before tagging."
    fi

    git tag -a "${TAG}" -m "Release ${VERSION}"
    info "Pushing tag ${TAG} to origin"
    git push origin "${TAG}"
    success "Tag ${TAG} created and pushed"
}

do_package() {
    info "Building distribution package"
    make package
    success "Created ${PACKAGE_NAME}.zip"
}

# The Burrow Mac app, zipped with ditto so its signature survives.
do_app() {
    info "Building the Burrow Mac app"
    [ "$(uname)" = "Darwin" ] || error "The Burrow app can only be built on macOS."
    make burrow-app
    rm -f "${BURROW_ZIP}"
    ditto -c -k --keepParent dist/Burrow.app "${BURROW_ZIP}"
    success "Created ${BURROW_ZIP}"
}

do_release() {
    info "Creating GitHub release ${TAG}"

    command -v gh >/dev/null 2>&1 || error "GitHub CLI (gh) is required. Install with: brew install gh"
    gh auth status >/dev/null 2>&1 || error "Not authenticated. Run: gh auth login"

    if ! git rev-parse "${TAG}" >/dev/null 2>&1; then
        error "Tag ${TAG} does not exist. Run './release.sh tag' first."
    fi

    # Build the binary
    info "Building basika binary"
    make clean
    make
    if [ ! -f basika ]; then
        error "Binary 'basika' not found after build."
    fi

    # Build the source package
    info "Building source package"
    make package
    if [ ! -f "${PACKAGE_NAME}.zip" ]; then
        error "Package '${PACKAGE_NAME}.zip' not found after build."
    fi

    # Build the Burrow app for the cask
    do_app

    # Create the release (or skip if it already exists)
    if gh release view "${TAG}" >/dev/null 2>&1; then
        info "GitHub release ${TAG} already exists, uploading assets (overwriting if present)"
        gh release upload "${TAG}" \
            "${PACKAGE_NAME}.zip" \
            "${BURROW_ZIP}" \
            --clobber
    else
        info "Creating new GitHub release ${TAG}"
        gh release create "${TAG}" \
            "${PACKAGE_NAME}.zip" \
            "${BURROW_ZIP}" \
            --title "Basika ${VERSION}" \
            --generate-notes
    fi

    success "GitHub release ${TAG} created with assets:"
    echo "  - ${PACKAGE_NAME}.zip  (source package)"
    echo "  - ${BURROW_ZIP}  (Burrow Mac app)"

    # Clean up build artifacts
    info "Cleaning up build artifacts"
    make clean
    rm -f "${PACKAGE_NAME}.zip" "${BURROW_ZIP}"
}

do_formula() {
    info "Updating Homebrew formula for ${TAG}"

    if ! git rev-parse "${TAG}" >/dev/null 2>&1; then
        error "Tag ${TAG} does not exist. Run './release.sh tag' first."
    fi

    # Download the archive and compute SHA256
    info "Downloading archive to compute SHA256..."
    SHA256=$(curl -sL "${ARCHIVE_URL}" | shasum -a 256 | awk '{print $1}')

    if [ -z "${SHA256}" ] || [ "${#SHA256}" -ne 64 ]; then
        error "Failed to compute SHA256. Has the release been published? Run './release.sh release' first."
    fi

    info "SHA256: ${SHA256}"

    # Update the URL version
    sed -i '' "s|url \"https://github.com/kdekorte/basika/releases/download/v[^/]*/basika-.*\.zip\"|url \"${ARCHIVE_URL}\"|" "${FORMULA}"

    # Update or insert sha256 line
    if grep -q '# sha256' "${FORMULA}"; then
        # Replace commented-out placeholder
        sed -i '' "s|# sha256 .*|sha256 \"${SHA256}\"|" "${FORMULA}"
    elif grep -q 'sha256' "${FORMULA}"; then
        # Replace existing sha256
        sed -i '' "s|sha256 \".*\"|sha256 \"${SHA256}\"|" "${FORMULA}"
    else
        # Insert after the url line
        sed -i '' "/^  url /a\\
  sha256 \"${SHA256}\"" "${FORMULA}"
    fi

    success "Formula updated:"
    echo "  URL:    ${ARCHIVE_URL}"
    echo "  SHA256: ${SHA256}"
    echo ""
    echo "Don't forget to commit the updated formula:"
    echo "  git add ${FORMULA}"
    echo "  git commit -m \"Update formula for ${VERSION}\""
    echo "  git push"
}

do_cask() {
    info "Updating Homebrew cask for ${TAG}"

    if ! git rev-parse "${TAG}" >/dev/null 2>&1; then
        error "Tag ${TAG} does not exist. Run './release.sh tag' first."
    fi

    info "Downloading ${BURROW_ZIP} to compute SHA256..."
    SHA256=$(curl -sfL "${BURROW_URL}" | shasum -a 256 | awk '{print $1}')

    # An empty download hashes to e3b0c442...; treat it as missing.
    if [ -z "${SHA256}" ] || [ "${#SHA256}" -ne 64 ] || [ "${SHA256}" = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" ]; then
        error "Failed to download ${BURROW_URL}. Has the release been published? Run './release.sh release' first."
    fi

    info "SHA256: ${SHA256}"
    sed -i '' "s|^  version \".*\"|  version \"${VERSION}\"|" "${CASK}"
    sed -i '' "s|^  sha256 .*|  sha256 \"${SHA256}\"|" "${CASK}"

    success "Cask updated:"
    echo "  Version: ${VERSION}"
    echo "  SHA256:  ${SHA256}"
    echo ""
    echo "Don't forget to commit the updated cask:"
    echo "  git add ${CASK}"
    echo "  git commit -m \"Update Burrow cask for ${VERSION}\""
    echo "  git push"
}

do_all() {
    do_tag
    echo ""
    do_release
    echo ""
    do_formula
    echo ""
    do_cask
}

# --- Main ---

case "${1:-status}" in
    status)   show_status ;;
    tag)      do_tag ;;
    package)  do_package ;;
    release)  do_release ;;
    formula)  do_formula ;;
    app)      do_app ;;
    cask)     do_cask ;;
    all)      do_all ;;
    *)
        echo "Usage: $0 {status|tag|package|release|formula|app|cask|all}"
        echo ""
        echo "Commands:"
        echo "  status    Show current version and release status (default)"
        echo "  tag       Create and push a git tag for the current version"
        echo "  package   Build the basika-VERSION.zip distribution archive"
        echo "  release   Create GitHub release and upload the source package and Burrow app"
        echo "  formula   Update the Homebrew formula URL and SHA256"
        echo "  app       Build Burrow-VERSION.zip, the Burrow Mac app"
        echo "  cask      Update the Burrow cask version and SHA256"
        echo "  all       Run tag, release, formula and cask in sequence"
        exit 1
        ;;
esac
