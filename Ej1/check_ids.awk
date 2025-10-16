#!/usr/bin/awk -f
# Uso: awk -f check_ids.awk datos.csv
BEGIN {
    FS=";"
    first=1
}
NR==1 { next } # saltar header
{
    id=$1+0
    if(id in seen) {
        print "DUPLICADO", id > "/dev/stderr"
        dup=1
    }
    seen[id]=1
    if (prev_id=="" ) {
        prev_id=id
    } else {
        if (id != prev_id+1) {
            print "SALTO detectado: prev=" prev_id " cur=" id > "/dev/stderr"
            gap=1
        }
        prev_id=id
    }
}
END {
    if (!dup && !gap) {
        print "OK: IDs correlativos y sin duplicados"
        exit 0
    } else {
        print "ERROR: revisa duplicados/saltos"
        exit 2
    }
}
