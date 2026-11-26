#!/bin/bash
# Abnahme der iPad-Fernbedienung mit curl (wie make check): startet gearbox-render --serve und prüft Seite,
# PIN abgelehnt/akzeptiert, Schritt per POST setzen, Zustand per SSE, Anfrage aus nicht-privatem Netz abgelehnt.
set -u
BIN="$(dirname "$0")/../bin/gearbox-render"
PORT=${PORT:-18080}
failed=0
ok() { if [ "$2" = 0 ]; then echo "  [OK ] $1"; else echo "  [FEHLER] $1"; failed=$((failed + 1)); fi; }
start() { "$BIN" --serve --port $PORT --pin 4321 "$@" > /dev/null & PID=$!; for i in $(seq 1 50); do curl -s -o /dev/null "http://127.0.0.1:$PORT/" && return; sleep 0.1; done; }
stop() { kill $PID 2>/dev/null; wait $PID 2>/dev/null; }
code() { curl -s -o /dev/null -w "%{http_code}" "$@"; }
U="http://127.0.0.1:$PORT"

echo "== Fernbedienung (curl, Port $PORT)"
GEARBOX_SONG_DIR="$(mktemp -d)"; export GEARBOX_SONG_DIR
start
curl -s "$U/" | grep -q "GEARBOX REMOTE"; ok "Seite wird ausgeliefert" $?
curl -s "$U/manifest.webmanifest" | grep -q '"standalone"'; ok "Manifest (Web-App)" $?
[ "$(code "$U/state")" = 403 ]; ok "ohne PIN abgelehnt (403)" $?
[ "$(code "$U/state?pin=1111")" = 403 ]; ok "falsche PIN abgelehnt (403)" $?
curl -s "$U/state?pin=4321" | grep -q '"gearbox":1'; ok "richtige PIN: Zustand als JSON" $?
curl -s "$U/live?pin=4321" | grep -q "\"queued\":"; ok "Laufzustand für Clutch (/live)" $?
[ "$(code "$U/live?pin=1111")" = 403 ]; ok "/live nur mit PIN" $?
[ "$(code "$U/../../etc/passwd?pin=4321")" = 404 ]; ok "kein Dateizugriff (404)" $?
[ "$(code -X POST --data-binary "step 0 7 5 3" "$U/cmd?pin=4321")" = 204 ]; ok "Schritt per POST gesetzt (204)" $?
curl -s "$U/state?pin=4321" | grep -q '"steps":".....X'; ok "Schritt im Zustand sichtbar (Spur 8, Schritt 6 laut)" $?
[ "$(code -X POST --data-binary "song save Test 1" "$U/cmd?pin=4321")" = 204 ]; ok "Song speichern (204)" $?
[ -f "$GEARBOX_SONG_DIR/Test 1.json" ]; ok "Song-Datei existiert" $?
curl -s "$U/state?pin=4321" | grep -q '"songs":\["Test 1"\]'; ok "Songliste im Zustand" $?
curl -s -X POST --data-binary "step 0 0 5 3" "$U/cmd?pin=4321" > /dev/null
curl -s -X POST --data-binary "song load Test 1" "$U/cmd?pin=4321" > /dev/null
[ "$(curl -s "$U/state?pin=4321" | grep -o '"steps":"[^"]*"' | head -1 | cut -c10-15)" = 'X...X.' ]; ok "Song laden macht Schritt rückgängig" $?
[ "$(code -X POST --data-binary "song save ../x" "$U/cmd?pin=4321")" = 400 ]; ok "ungültiger Songname abgelehnt (400)" $?
curl -s -X POST --data-binary "chain add 1 2" "$U/cmd?pin=4321" > /dev/null
curl -s "$U/state?pin=4321" | grep -q '"chain":\[\[1,2\]\]'; ok "Ablauf-Eintrag im Zustand" $?
[ "$(code -X POST --data-binary "quatsch 1" "$U/cmd?pin=4321")" = 400 ]; ok "unbekannter Befehl abgelehnt (400)" $?
head -c 70000 /dev/zero | tr '\0' 'a' > "${TMPDIR:-/tmp}/gearbox-big.txt"
[ "$(code -X POST --data-binary @"${TMPDIR:-/tmp}/gearbox-big.txt" "$U/cmd?pin=4321")" = 413 ]; ok "zu große Anfrage abgelehnt (413)" $?
rm -f "${TMPDIR:-/tmp}/gearbox-big.txt"
curl -s -N --max-time 1 "$U/events?pin=4321" > "${TMPDIR:-/tmp}/gearbox-sse.txt"
grep -q "^event: state" "${TMPDIR:-/tmp}/gearbox-sse.txt"; ok "SSE: Zustand empfangen" $?
[ "$(grep -c "^event: live" "${TMPDIR:-/tmp}/gearbox-sse.txt")" -ge 15 ]; ok "SSE: Live-Daten ~30/s ($(grep -c "^event: live" "${TMPDIR:-/tmp}/gearbox-sse.txt") in 1 s)" $?
curl -s -X POST --data-binary "play 1" "$U/cmd?pin=4321" > /dev/null; sleep 0.3
curl -s -N --max-time 0.5 "$U/events?pin=4321" | grep -q '"playing":true'; ok "Play per POST, Live zeigt playing" $?
rm -f "${TMPDIR:-/tmp}/gearbox-sse.txt"
stop

start --fake-peer 8.8.8.8
[ "$(code "$U/")" = 403 ]; ok "Anfrage aus nicht-privatem Netz abgelehnt (simuliert 8.8.8.8)" $?
stop

echo "== Port belegt: nächster freier"
start; P1=$PID
"$BIN" --serve --port $PORT --pin 4321 > "${TMPDIR:-/tmp}/gearbox-port.txt" & P2=$!; sleep 0.8
grep -q "port $((PORT + 1))" "${TMPDIR:-/tmp}/gearbox-port.txt"; ok "zweite Instanz auf Port $((PORT + 1))" $?
kill $P2 2>/dev/null; wait $P2 2>/dev/null; PID=$P1; stop; rm -f "${TMPDIR:-/tmp}/gearbox-port.txt"

[ $failed = 0 ] && echo BESTANDEN || echo "NICHT BESTANDEN"
exit $failed
