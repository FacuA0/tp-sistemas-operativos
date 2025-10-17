#!/bin/bash
OUTDIR=monitoreo_tp2_$(date +%Y%m%d_%H%M%S)
mkdir -p "$OUTDIR"
echo "MONITOR TP2 $(date)" > "$OUTDIR/meta.txt"

# lanzar servidor en background
stdbuf -oL -eL ./servidor 0.0.0.0 9000 10 &> "$OUTDIR/server.log" &
SV_PID=$!
sleep 1

ps -ef | grep servidor | grep -v grep > "$OUTDIR/ps_server.txt"
ss -ltnp | grep :9000 > "$OUTDIR/ss.txt" 2>&1
lsof -i :9000 > "$OUTDIR/lsof_9000.txt" 2>&1
netstat -tlnp > "$OUTDIR/netstat.txt" 2>&1

# abrir dos clientes interactivos en background (ejemplo con netcat o con tu client)
# usando el cliente provisto: ./cliente 127.0.0.1 9000
(printf "BEGIN\n"; sleep 4; printf "EXIT\n") | ./cliente 127.0.0.1 9000 > "$OUTDIR/clientA.log" &
# now leave it open (simulate leaving transaction)
CLIENTA_PID=$!

# start second client and try QUERY (it should receive ERROR while A holds transaction)
sleep 1
( printf "QUERY 1\n"; sleep 0.5; printf "EXIT\n" ) | ./cliente 127.0.0.1 9000 > "$OUTDIR/clientB.log" 2>&1

# cleanup
sleep 5
kill $SV_PID || true
wait $SV_PID 2>/dev/null || true
echo "done" >> "$OUTDIR/meta.txt"
