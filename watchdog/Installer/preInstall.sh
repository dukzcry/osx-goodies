#!/bin/sh

/usr/bin/killall 'Watchdog Feeder' 2>&1 >/dev/null
sleep 2

/bin/rm -rf '/System/Library/Extensions/iTCOWatchdog.kext' \
            '/usr/local/libexec/Watchdog Feeder' \
            '/Library/LaunchDaemons/cc.dukzcry.WatchdogFeeder.plist'
