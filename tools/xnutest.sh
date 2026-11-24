#!/bin/bash
# Build with mkxnu.py (args passed through), run on the device, dump the debug state.
# usage: xnutest.sh [mkxnu.py args...]
set -eu
cd "$(dirname "$0")/.."
info=$(python3 tools/mkxnu.py "$@" | tail -1)
echo "$info"
eval "$(tr ' ' '\n' <<< "$info" | grep -E '^(mailbox|fiqs|dbg|log|aic)=')"
tools/xnurun.sh out/xnu.itb "$mailbox"
m() { timeout 3 fastboot getvar mem:"$(printf '%#x' "$1")" 2>&1 | head -1; }
echo "mailbox +80 (--vec: r2 r3 lr_svc sp_svc):"; m $((mailbox + 80))
echo "fiq sample (lr count G_CNT_L spsr | cpu_data+4 cpu_data+0xc - ring_index | last 16 pcs):"
for o in 0 16 32 48 64 80; do m $((fiqs + o)); done
echo "fiq backtrace (r7, return addresses):"; for o in 96 112 128; do m $((fiqs + o)); done
echo "dbg (sets v deadline cnt tcon cstat comp signals | active pending rpr hppir tb_calls tb_last dec_calls irqs):"
m "$dbg"; m $((dbg + 16)); m $((dbg + 32)); m $((dbg + 48))
echo "irq_handler cpu_signal sctlr ttbr0 | par(text) par(data) actlr (at last set_decrementer):"; m $((dbg + 64)); m $((dbg + 80))
echo "aic (reads writes iacks last_iack | unk_rd unk_wr unk_val hw):"; m "$aic"; m $((aic + 16))
python3 tools/xnulog.py "$log" | tail -40 || true
