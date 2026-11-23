#!/bin/sh
# MPC Master Mixer — diagnostic d'environnement en lecture seule.
# Compatible POSIX sh / BusyBox ash. Seul le rapport est écrit sur le stockage utilisateur.
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
  echo "Noyau: $(uname -a 2>/dev/null)"
  echo

  echo "===== IDENTITE SYSTEME ====="
  for f in /etc/os-release /etc/buildroot-release /etc/issue; do
    if [ -r "$f" ]; then
      echo "--- $f"
      cat "$f"
    fi
  done
  echo "--- libc"
  if command -v getconf >/dev/null 2>&1; then
    getconf GNU_LIBC_VERSION 2>&1
  else
    echo "getconf indisponible"
  fi
  for f in /lib/libc.so.6 /usr/lib/libc.so.6 /lib/ld-linux-armhf.so.3 /lib/ld-linux.so.3; do
    [ -e "$f" ] && ls -l "$f" 2>&1
  done
  echo

  echo "===== PROCESSUS MPC / INMUSIC ====="
  FOUND_MPC_PIDS=""
  for p in /proc/[0-9]*; do
    [ -r "$p/cmdline" ] || continue
    cmd=$(tr '\000' ' ' < "$p/cmdline" 2>/dev/null)
    case "$cmd" in
      *"/usr/bin/MPC"*|*"DeviceControlServer"*|*"az01-launch-MPC"*)
        pid=${p##*/}
        FOUND_MPC_PIDS="$FOUND_MPC_PIDS $pid"
        echo "PID $pid: $cmd"
        ;;
    esac
  done
  if [ -z "$FOUND_MPC_PIDS" ]; then
    echo "Aucun processus MPC correspondant détecté."
  fi
  echo

  echo "===== PROCESSUS ET BIBLIOTHEQUES MPC ====="
  for pid in $FOUND_MPC_PIDS; do
    p="/proc/$pid"
    echo "--- PID $pid : descripteurs socket"
    if [ -d "$p/fd" ]; then
      for f in "$p"/fd/*; do
        [ -e "$f" ] || continue
        link=$(readlink "$f" 2>/dev/null)
        case "$link" in socket:*) echo "$f -> $link";; esac
      done
    else
      echo "Descripteurs non lisibles."
    fi
    echo "--- PID $pid : bibliotheques / mappings"
    if [ -r "$p/maps" ]; then
      awk '$6 ~ /^\// {print $6}' "$p/maps" 2>/dev/null | sort -u
    else
      echo "Mappings non lisibles."
    fi
  done
  echo

  echo "===== CONFIGURATION KERNEL UNIX SOCKET ====="
  if [ -r /proc/config.gz ] && command -v zcat >/dev/null 2>&1; then
    zcat /proc/config.gz 2>/dev/null | grep '^CONFIG_UNIX='
  elif [ -r /boot/config-$(uname -r 2>/dev/null) ]; then
    grep '^CONFIG_UNIX=' "/boot/config-$(uname -r 2>/dev/null)" 2>/dev/null
  else
    echo "Configuration kernel non exposée dans les emplacements vérifiés."
  fi
  echo

  echo "===== ECOUTE TCP ET TABLES RESEAU (LECTURE PROCFS) ====="
  if command -v netstat >/dev/null 2>&1; then
    netstat -lnt 2>&1
  else
    echo "netstat indisponible"
  fi
  for f in /proc/net/tcp /proc/net/tcp6 /proc/net/udp /proc/net/udp6; do
    if [ -r "$f" ]; then
      echo "--- $f"
      cat "$f"
    fi
  done
  echo

  echo "===== SOCKETS UNIX ====="
  if [ -r /proc/net/unix ]; then
    cat /proc/net/unix
  else
    echo "/proc/net/unix indisponible"
  fi
  echo

  echo "===== SERVICES DBUS (SI DISPONIBLE) ====="
  if command -v busctl >/dev/null 2>&1; then
    busctl --system list --no-pager 2>&1 | head -100
  else
    echo "busctl indisponible"
  fi
  echo

  echo "===== INDICES DE PROTOCOLE DANS BINAIRES (LECTURE SEULE) ====="
  for bin in /usr/bin/MPC /usr/lib/libaz01-prog.so; do
    if [ -r "$bin" ] && command -v strings >/dev/null 2>&1; then
      echo "--- $bin"
      strings "$bin" 2>/dev/null |
        grep -i -E 'json|osc|tcp|http|websocket|subscribe|unsubscribe|request|response|notification|command|41401|asio|zmq|socket' |
        head -160
    else
      echo "$bin ou strings indisponible"
    fi
  done
  echo

  echo "===== OUTILS DISPONIBLES ====="
  for tool in getconf netstat curl wget nc netcat socat python3 nm readelf objdump strings busctl zcat; do
    if command -v "$tool" >/dev/null 2>&1; then
      command -v "$tool"
    fi
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
  rm -f "$TMP"
  echo "ERREUR: impossible d'enregistrer le rapport."
  exit 1
fi
