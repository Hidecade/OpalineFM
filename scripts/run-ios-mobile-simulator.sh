#!/usr/bin/env bash
set -euo pipefail

DEVICE_NAME="${1:-iPhone 17}"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_DIR="${ROOT_DIR}/iOS/OpalineFMMobile"
DERIVED_DATA="${ROOT_DIR}/build/ios-mobile"
APP_BUNDLE="${DERIVED_DATA}/Build/Products/Debug-iphonesimulator/Opaline FM.app"
BUNDLE_ID="jp.hidecade.opalinefm.mobile"

cd "${APP_DIR}"

xcodegen generate

xcodebuild \
  -project OpalineFMMobile.xcodeproj \
  -scheme OpalineFMMobile \
  -destination "platform=iOS Simulator,name=${DEVICE_NAME}" \
  -derivedDataPath ../../build/ios-mobile \
  CODE_SIGNING_ALLOWED=NO \
  build

if ! xcrun simctl list devices booted | grep -q "${DEVICE_NAME}"; then
  xcrun simctl boot "${DEVICE_NAME}" >/dev/null 2>&1 || true
fi

xcrun simctl bootstatus "${DEVICE_NAME}" -b
open -a Simulator
xcrun simctl install "${DEVICE_NAME}" "${APP_BUNDLE}"
xcrun simctl launch "${DEVICE_NAME}" "${BUNDLE_ID}"
