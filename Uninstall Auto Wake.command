#!/bin/zsh
# Removes everything "Install Auto Wake.command" installed.

set -u

label="io.github.magic-mouse-2-fix.keeper"
app_dir="$HOME/Applications/Magic Mouse Keeper.app"
agent_path="$HOME/Library/LaunchAgents/$label.plist"
log_path="$HOME/Library/Logs/MagicMouseKeeper.log"

launchctl bootout "gui/$(id -u)/$label" 2>/dev/null
rm -f -- "$agent_path" "$log_path"
rm -rf -- "$app_dir"
tccutil reset ListenEvent "$label" >/dev/null 2>&1

printf "Magic Mouse Keeper was removed.\n"

if [[ -t 0 ]]; then
    printf "\nPress any key to close this window."
    read -r -k 1
    printf "\n"
fi
