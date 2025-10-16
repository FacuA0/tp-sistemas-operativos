#!/bin/bash
OUTDIR=monitoreo_tp1_$(date +%Y%m%d_%H%M%S)
mkdir -p "$OUTDIR"
echo "PID_TIME: $(date)" > "$OUTDIR/meta.txt"

# lanzar generador en background (ejemplo)
./main -p=4 -r=6000000 &> "$OUTDIR/tp1_run.log" &
TP_PID=$!
sleep 0.5

echo "tp1 PID=$TP_PID" >> "$OUTDIR/meta.txt"
ps -ef | grep main | grep -v grep > "$OUTDIR/ps.txt"
ipcs -m > "$OUTDIR/ipcs_m.txt" 2>&1
ls -l /dev/shm > "$OUTDIR/dev_shm_list.txt" 2>&1
vmstat 1 5 > "$OUTDIR/vmstat.txt"
# capturar output del coordinador parcial
tail -n 200 "$OUTDIR/tp1_run.log" > "$OUTDIR/coordinador_tail.log"

# esperar a que termine
wait $TP_PID
echo "terminado $(date)" >> "$OUTDIR/meta.txt"

# comprobar limpieza
ipcs -m > "$OUTDIR/ipcs_m_after.txt" 2>&1
ls -l /dev/shm > "$OUTDIR/dev_shm_list_after.txt" 2>&1

echo "monitoreo guardado en $OUTDIR"
