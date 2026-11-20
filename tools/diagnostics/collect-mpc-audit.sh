#!/bin/sh
# MPC Master Mixer — diagnostic réseau et interfaces (lecture seule)
# Compatible BusyBox ash. Écrit uniquement le rapport sur le stockage interne.
OUT="/media/az01-internal/mpc-mixer-audit-auto.txt"
TMP="${OUT}.tmp"
umask 077

if [ ! -d /media/az01-internal ] || [ ! -w /media/az01-internal ]; then
  echo "ERREUR: /media/az01-internal absent ou non inscriptible."
  exit 1
fi

{
  echo "MPC MASTER MIXER — RAPPORT AUTOMATIQUE"
  echo "Date: $(date 2>/dev/null)"
  echo "Appareil: $(uname -a 2>/dev/null)"
  echo

  echo "===== PROCESSUS PRINCIPAUX ====="
  for p in /proc/[0-9]*; do
    [ -r "$p/cmdline" ] || continue
    cmd=$(tr '\000' ' ' < "$p/cmdline" 2>/dev/null)
    case "$cmd" in
      *MPC*|*DeviceControlServer*|*az01*)
        echo "PID ${p##*/}: $cmd"
        ;;
    esac
  done
  echo

  echo "===== ECOUTE TCP ====="
  netstat -lnt 2>&1
  echo
  echo "===== TABLE TCP / UDP ====="
  cat /proc/net/tcp /proc/net/udp /proc/net/tcp6 /proc/net/udp6 2>/dev/null
  echo

  echo "===== PROPRIETAIRES DES SOCKETS TCP EN ECOUTE ====="
  for entry in "$(grep ' 0A ' /proc/net/tcp 2>/dev/null)"; do :; done
  for inode in $(awk '$4 == "0A" {print $10}' /proc/net/tcp 2>/dev/null); do
    echo "TCP inode=$inode"
    for p in /proc/[0-9]*; do
      [ -d "$p/fd" ] || continue
      for f in "$p"/fd/*; do
        link=$(readlink "$f" 2>/dev/null)
        if [ "$link" = "socket:[$inode]" ]; then
          echo "  PID=${p##*/} FD=${f##*/}"
          tr '\000' ' ' < "$p/cmdline" 2>/dev/null
          echo
        fi
      done
    done
  done
  echo

  echo "===== SOCKETS OUVERTS PAR MPC (PID 312 SI PRESENT) ====="
  if [ -d /proc/312/fd ]; then
    for f in /proc/312/fd/*; do
      link=$(readlink "$f" 2>/dev/null)
      case "$link" in socket:*) echo "$f -> $link";; esac
    done
    echo "-- Bibliotheques chargees --"
    awk '$6 ~ /^\// {print $6}' /proc/312/maps 2>/dev/null | sort -u
  else
    echo "PID 312 absent; rechercher le PID MPC dans la section processus."
  fi
  echo

  echo "===== SOCKETS UNIX ====="
  cat /proc/net/unix 2>/dev/null
  echo

  echo "===== DBUS : NOMS DE SERVICES (SI DISPONIBLE) ====="
  if command -v busctl >/dev/null 2>&1; then
    busctl --system list --no-pager 2>&1 | head -100
  else
    echo "busctl indisponible"
  fi
  echo

  echo "===== INDICES PROTOCOLES DANS MPC ====="
  if command -v strings >/dev/null 2>&1 && [ -r /usr/bin/MPC ]; then
    strings /usr/bin/MPC 2>/dev/null |
      grep -i -E 'json|osc|tcp|http|websocket|subscribe|unsubscribe|request|response|notification|command|41401|asio|zmq' |
      head -160
  else
    echo "strings ou /usr/bin/MPC indisponible"
  fi
  echo

  echo "===== INDICES DANS LIBAZ01 ====="
  if command -v strings >/dev/null 2>&1 && [ -r /usr/lib/libaz01-prog.so ]; then
    strings /usr/lib/libaz01-prog.so 2>/dev/null |
      grep -i -E 'json|osc|tcp|http|socket|mixer|channel|volume|parameter|control|send|receive' |
      head -120
  else
    echo "libaz01-prog.so ou strings indisponible"
  fi
  echo

  echo "===== OUTILS DISPONIBLES ====="
  for tool in curl wget nc netcat socat python3 nm readelf objdump strings busctl; do
    command -v "$tool" 2>/dev/null
  done
  echo
  echo "===== FIN DU RAPPORT ====="
} > "$TMP" 2>&1

if mv "$TMP" "$OUT"; then
  chmod 600 "$OUT" 2>/dev/null
  echo "Rapport créé : $OUT"
  wc -c "$OUT"
  echo "Pour l'afficher : cat $OUT"
else
  echo "ERREUR: impossible d'enregistrer le rapport."
  exit 1
fi
