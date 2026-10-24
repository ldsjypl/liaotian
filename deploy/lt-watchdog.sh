#!/bin/sh
# lt-watchdog: keep the public service alive after power cuts, wifi drops and
# crashes. Runs from a systemd timer every 2 minutes.
set -u

log() { logger -t lt-watchdog "$*"; }

# 1. chat server answers?
if ! curl -fsS --max-time 5 http://127.0.0.1:8081/api/health >/dev/null 2>&1; then
    if [ "$(systemctl is-active liaotian)" != "activating" ]; then
        log "liaotian is not answering, restarting"
        systemctl restart liaotian
    fi
fi

# 2. nginx up?
if ! systemctl is-active --quiet nginx; then
    log "nginx is down, restarting"
    systemctl restart nginx
fi

# 3. cloudflare tunnel up? this is the only public ingress since NS moved to CF
if ! systemctl is-active --quiet cloudflared; then
    log "cloudflared is down, restarting"
    systemctl restart cloudflared
fi

# 4. 记录 IPv6 状态即可。隧道走 IPv4 出网，没有 IPv6 也能正常工作，
#    不要每两分钟去重连网卡（那会打断连接）。
if ! ip -6 addr show dev wlan0 2>/dev/null | grep -q "scope global"; then
    if [ ! -f /run/lt-watchdog.ipv6-warned ]; then
        log "wlan0 has no global IPv6 (tunnel runs over IPv4, no action taken)"
        touch /run/lt-watchdog.ipv6-warned
    fi
fi

exit 0
