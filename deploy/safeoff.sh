#!/bin/sh
# 安全关机。板子没有电源键，直接拔线可能丢数据甚至坏卡。
# 用法：sudo safeoff   然后等板子指示灯灭/网络断开再断电。
sync
sync
logger -t safeoff "clean shutdown requested by $(id -un 2>/dev/null || echo user)"
systemctl poweroff
