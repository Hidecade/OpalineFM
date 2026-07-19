#!/usr/bin/env bash
set -euo pipefail
export COPYFILE_DISABLE=1

configuration="Release"
build_directory=""
skip_build=0
application_sign_identity="${OPALINE_APPLICATION_SIGN_IDENTITY:-}"
installer_sign_identity="${OPALINE_INSTALLER_SIGN_IDENTITY:-}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --configuration)
            configuration="$2"
            shift 2
            ;;
        --build-directory)
            build_directory="$2"
            shift 2
            ;;
        --skip-build)
            skip_build=1
            shift
            ;;
        --application-sign-identity)
            application_sign_identity="$2"
            shift 2
            ;;
        --installer-sign-identity)
            installer_sign_identity="$2"
            shift 2
            ;;
        -h|--help)
            echo "Usage: $0 [--configuration Release] [--build-directory build/macos-clt-release] [--skip-build] [--application-sign-identity IDENTITY] [--installer-sign-identity IDENTITY]"
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 2
            ;;
    esac
done

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
version_file="$repo_root/cmake/OpalineVersion.cmake"

major="$(sed -nE 's/.*OPALINE_VERSION_MAJOR[[:space:]]+([0-9]+).*/\1/p' "$version_file")"
minor="$(sed -nE 's/.*OPALINE_VERSION_MINOR[[:space:]]+([0-9]+).*/\1/p' "$version_file")"
patch="$(sed -nE 's/.*OPALINE_VERSION_PATCH[[:space:]]+([0-9]+).*/\1/p' "$version_file")"
tweak="$(sed -nE 's/.*OPALINE_VERSION_TWEAK[[:space:]]+([0-9]+).*/\1/p' "$version_file")"
version="$major.$minor.$patch"
if [[ -n "$tweak" && "$tweak" != "0" ]]; then
    version="$version.$tweak"
fi

if [[ -z "$build_directory" ]]; then
    build_directory="build/macos-clt-release"
fi

build_root="$repo_root/$build_directory"
dist_directory="$repo_root/dist"
stage_root="$build_root/package-stage"
plugin_artifact_root="$build_root/OpalineFM_Plugin_artefacts/$configuration"
standalone_artifact="$plugin_artifact_root/Standalone/Opaline FM.app"
vst3_artifact="$plugin_artifact_root/VST3/Opaline FM.vst3"
au_artifact="$plugin_artifact_root/AU/Opaline FM.component"

if [[ "$skip_build" -eq 0 ]]; then
    cmake -S "$repo_root" -B "$build_root" \
        -DCMAKE_BUILD_TYPE="$configuration" \
        -DOPALINE_AUTO_INCREMENT_VERSION=OFF \
        -DOPALINE_BUILD_STANDALONE=ON \
        -DOPALINE_BUILD_PLUGIN=ON \
        -DOPALINE_BUILD_AU=ON

    cmake --build "$build_root" --config "$configuration" --target \
        OpalineFM_Plugin_Standalone \
        OpalineFM_Plugin_VST3 \
        OpalineFM_Plugin_AU
fi

for required in "$standalone_artifact" "$vst3_artifact" "$au_artifact"; do
    if [[ ! -e "$required" ]]; then
        echo "Required artifact was not found: $required" >&2
        exit 1
    fi
done

mkdir -p "$dist_directory"
rm -rf "$stage_root"

app_stage="$stage_root/standalone/Applications"
vst3_stage="$stage_root/vst3/Library/Audio/Plug-Ins/VST3"
au_stage="$stage_root/au/Library/Audio/Plug-Ins/Components"

mkdir -p "$app_stage" "$vst3_stage" "$au_stage"
ditto --norsrc --noextattr "$standalone_artifact" "$app_stage/Opaline FM.app"
ditto --norsrc --noextattr "$vst3_artifact" "$vst3_stage/Opaline FM.vst3"
ditto --norsrc --noextattr "$au_artifact" "$au_stage/Opaline FM.component"

if command -v xattr >/dev/null 2>&1; then
    xattr -cr "$stage_root"
fi

if command -v codesign >/dev/null 2>&1; then
    if [[ -n "$application_sign_identity" ]]; then
        codesign --force --deep --options runtime --timestamp --sign "$application_sign_identity" "$app_stage/Opaline FM.app"
        codesign --force --deep --options runtime --timestamp --sign "$application_sign_identity" "$vst3_stage/Opaline FM.vst3"
        codesign --force --deep --options runtime --timestamp --sign "$application_sign_identity" "$au_stage/Opaline FM.component"
    else
        codesign --force --deep --sign - "$app_stage/Opaline FM.app"
        codesign --force --deep --sign - "$vst3_stage/Opaline FM.vst3"
        codesign --force --deep --sign - "$au_stage/Opaline FM.component"
    fi
fi

app_components="$stage_root/standalone-components.plist"
vst3_components="$stage_root/vst3-components.plist"
au_components="$stage_root/au-components.plist"

pkgbuild --analyze --root "$stage_root/standalone" "$app_components"
pkgbuild --analyze --root "$stage_root/vst3" "$vst3_components"
pkgbuild --analyze --root "$stage_root/au" "$au_components"

set_non_relocatable() {
    local component_plist="$1"
    if ! /usr/libexec/PlistBuddy -c "Set :0:BundleIsRelocatable false" "$component_plist" 2>/dev/null; then
        /usr/libexec/PlistBuddy -c "Add :0:BundleIsRelocatable bool false" "$component_plist"
    fi
}

set_non_relocatable "$app_components"
set_non_relocatable "$vst3_components"
set_non_relocatable "$au_components"

standalone_pkg="$dist_directory/OpalineFM-Standalone-$version-macOS.pkg"
vst3_pkg="$dist_directory/OpalineFM-VST3-$version-macOS.pkg"
au_pkg="$dist_directory/OpalineFM-AU-$version-macOS.pkg"

pkgbuild \
    --root "$stage_root/standalone" \
    --component-plist "$app_components" \
    --identifier "jp.hidecade.opalinefm.standalone" \
    --version "$version" \
    --install-location "/" \
    "$standalone_pkg"

pkgbuild \
    --root "$stage_root/vst3" \
    --component-plist "$vst3_components" \
    --identifier "jp.hidecade.opalinefm.vst3" \
    --version "$version" \
    --install-location "/" \
    "$vst3_pkg"

pkgbuild \
    --root "$stage_root/au" \
    --component-plist "$au_components" \
    --identifier "jp.hidecade.opalinefm.au" \
    --version "$version" \
    --install-location "/" \
    "$au_pkg"

if [[ -n "$installer_sign_identity" ]]; then
    signed_directory="$stage_root/signed-packages"
    mkdir -p "$signed_directory"
    productsign --sign "$installer_sign_identity" "$standalone_pkg" "$signed_directory/$(basename "$standalone_pkg")"
    productsign --sign "$installer_sign_identity" "$vst3_pkg" "$signed_directory/$(basename "$vst3_pkg")"
    productsign --sign "$installer_sign_identity" "$au_pkg" "$signed_directory/$(basename "$au_pkg")"
    mv "$signed_directory/$(basename "$standalone_pkg")" "$standalone_pkg"
    mv "$signed_directory/$(basename "$vst3_pkg")" "$vst3_pkg"
    mv "$signed_directory/$(basename "$au_pkg")" "$au_pkg"
fi

echo "Installers created in $dist_directory"
