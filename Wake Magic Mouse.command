#!/bin/zsh

set -u

script_dir="${0:A:h}"
source_path="$script_dir/magicmouse-wake.c"

pause_before_close() {
    printf "\nPress any key to close this window."
    read -r -k 1
    printf "\n"
}

if [[ ! -f "$source_path" ]]; then
    printf "The source file is missing:\n%s\n" "$source_path"
    printf "Keep this launcher and magicmouse-wake.c in the same folder.\n"
    pause_before_close
    exit 1
fi

if ! xcrun --find clang >/dev/null 2>&1; then
    printf "Apple Command Line Tools are required to compile the transparent helper.\n"
    printf "Starting Apple's installer now. After installation finishes, run this launcher again.\n\n"
    xcode-select --install
    pause_before_close
    exit 6
fi

work_dir="$(mktemp -d /tmp/magicmouse-wake.XXXXXX)"
if [[ -z "$work_dir" || ! -d "$work_dir" ]]; then
    printf "Could not create a temporary build folder.\n"
    pause_before_close
    exit 1
fi

cleanup() {
    rm -rf -- "$work_dir"
}
trap cleanup EXIT INT TERM

binary_path="$work_dir/magicmouse-wake"

printf "Building the one-shot helper from the readable source beside this launcher…\n"
if ! xcrun clang -O2 -Wall -Wextra \
    -o "$binary_path" \
    "$source_path" \
    -framework IOKit \
    -framework CoreFoundation; then
    printf "\nCompilation failed. No changes were made.\n"
    pause_before_close
    exit 1
fi

printf "Checking the connected Magic Mouse…\n\n"
"$binary_path"
result=$?

if [[ $result -eq 3 ]]; then
    printf "\nPermission is required only because macOS groups writing to a mouse under Input Monitoring.\n"
    printf "Open System Settings > Privacy & Security > Input Monitoring, enable Terminal,\n"
    printf "quit Terminal completely, then double-click this launcher again.\n"
fi

pause_before_close
exit $result
