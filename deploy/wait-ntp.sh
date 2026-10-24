#!/bin/sh
# Block until chrony reports the clock is synchronised (max ~90s).
# Needed before ddns-go: Tencent's API rejects requests whose timestamp is far
# from real time, which is exactly what happens right after a power cycle when
# the board has no RTC battery and boots at the wrong date.
i=0
while [ "$i" -lt 90 ]; do
    if [ "$(timedatectl show -p NTPSynchronized --value 2>/dev/null)" = "yes" ]; then
        exit 0
    fi
    sleep 1
    i=$((i + 1))
done
exit 0
