#!/system/bin/sh
# Magisk runs this before late_start. Let Android finish its startup so
# Magisk can invoke service.sh; that script stops both runtimes before taking
# the display. Stopping them here can stall boot before service.sh runs.
MODDIR=${0%/*}
[ -e "$MODDIR/disable" ] && exit 0

# Rockbox accesses /data/media/0 directly; no Android FUSE mount is required.

true
