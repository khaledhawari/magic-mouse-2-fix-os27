#!/bin/zsh
# Installs a per-user background helper that wakes the Magic Mouse automatically
# whenever it is connected without Apple's native driver (after a screen lock,
# a reconnect, or a restart). No sudo. Undo with "Uninstall Auto Wake.command".

set -u

script_dir="${0:A:h}"
source_path="$script_dir/magicmouse-wake.c"

label="io.github.magic-mouse-2-fix.keeper"
app_dir="$HOME/Applications/Magic Mouse Keeper.app"
exec_path="$app_dir/Contents/MacOS/magicmouse-keeper"
agent_path="$HOME/Library/LaunchAgents/$label.plist"
log_path="$HOME/Library/Logs/MagicMouseKeeper.log"
domain="gui/$(id -u)"

pause_before_close() {
    [[ -t 0 ]] || return 0
    printf "\nPress any key to close this window."
    read -r -k 1
    printf "\n"
}

fail() {
    printf "\n%s\n" "$1"
    pause_before_close
    exit 1
}

[[ -f "$source_path" ]] || fail "Missing source file: $source_path"

# Prefer the Command Line Tools: no Xcode license needed, matching linker and SDK.
clt_dir="/Library/Developer/CommandLineTools"
if [[ -x "$clt_dir/usr/bin/clang" && -d "$clt_dir/SDKs/MacOSX.sdk" ]]; then
    export DEVELOPER_DIR="$clt_dir"
fi

if ! xcrun --sdk macosx --find clang >/dev/null 2>&1; then
    printf "Apple Command Line Tools are required. Starting Apple's installer now.\n"
    printf "After installation finishes, run this installer again.\n\n"
    xcode-select --install
    pause_before_close
    exit 6
fi

work_dir="$(mktemp -d /tmp/magicmouse-keeper.XXXXXX)" || fail "Could not create a temporary build folder."
trap 'rm -rf -- "$work_dir"' EXIT INT TERM

printf "Building the helper from the readable source beside this installer…\n"
xcrun --sdk macosx clang -O2 -Wall -Wextra \
    -o "$work_dir/magicmouse-keeper" \
    "$source_path" \
    -framework IOKit \
    -framework CoreFoundation || fail "Compilation failed. Nothing was installed."

printf "Stopping any previous copy…\n"
launchctl bootout "$domain/$label" 2>/dev/null

printf "Installing %s…\n" "$app_dir"
rm -rf -- "$app_dir"
mkdir -p "$app_dir/Contents/MacOS" "$HOME/Library/LaunchAgents" "$HOME/Library/Logs" \
    || fail "Could not create the install folders."
cp "$work_dir/magicmouse-keeper" "$exec_path" || fail "Could not copy the helper."

cat > "$app_dir/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleIdentifier</key><string>$label</string>
    <key>CFBundleName</key><string>Magic Mouse Keeper</string>
    <key>CFBundleExecutable</key><string>magicmouse-keeper</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>1.0</string>
    <key>CFBundleVersion</key><string>1</string>
    <key>LSUIElement</key><true/>
</dict>
</plist>
PLIST

# Ad-hoc signature: gives macOS a stable identity to attach the permission to.
codesign --force --sign - --identifier "$label" "$app_dir" || fail "Could not sign the helper."

cat > "$agent_path" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key><string>$label</string>
    <key>ProgramArguments</key>
    <array>
        <string>$exec_path</string>
        <string>--watch</string>
    </array>
    <key>AssociatedBundleIdentifiers</key><string>$label</string>
    <key>RunAtLoad</key><true/>
    <key>KeepAlive</key><true/>
    <key>ThrottleInterval</key><integer>15</integer>
    <key>StandardOutPath</key><string>$log_path</string>
    <key>StandardErrorPath</key><string>$log_path</string>
</dict>
</plist>
PLIST

launchctl bootstrap "$domain" "$agent_path" || fail "launchd refused to load the agent."

printf "\nInstalled and started.\n\n"
printf "ONE MANUAL STEP: macOS must allow the helper to talk to the mouse.\n"
printf "  System Settings > Privacy & Security > Input Monitoring > enable \"Magic Mouse Keeper\"\n"
printf "The helper picks the permission up by itself within about 15 seconds.\n"
printf "Reinstalling rebuilds the helper, so the permission must be enabled again afterwards.\n\n"
printf "Log: %s\n" "$log_path"

open "x-apple.systempreferences:com.apple.preference.security?Privacy_ListenEvent" 2>/dev/null

pause_before_close
exit 0
