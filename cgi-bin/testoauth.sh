#!/bin/sh
# Callback state has to match CUPS_OAUTH_STATE before CUPS_BEARER is set.
set -eu
cd "$(dirname "$0")"
root=$(cd .. && pwd)
tmp=$(mktemp -d)
token_pid=
cleanup() {
  if [ -n "$token_pid" ]; then
    kill "$token_pid" 2>/dev/null || true
  fi
  rm -rf "$tmp"
}
trap cleanup EXIT

python3 - "$tmp" <<'PY' &
import socket, sys
from pathlib import Path
body = b'{"access_token":"attacker-token","expires_in":3600,"token_type":"bearer"}'
reply = (
    b"HTTP/1.1 200 OK\r\n"
    b"Content-Type: application/json\r\n"
    b"Content-Length: " + str(len(body)).encode() + b"\r\n"
    b"Connection: close\r\n\r\n" + body
)
s = socket.socket()
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("127.0.0.1", 0))
port = s.getsockname()[1]
Path(sys.argv[1], "port").write_text(str(port))
s.listen(8)
while True:
    c, _ = s.accept()
    data = b""
    while b"\r\n\r\n" not in data:
        chunk = c.recv(4096)
        if not chunk:
            break
        data += chunk
    c.sendall(reply)
    c.close()
PY
token_pid=$!

i=0
while [ ! -s "$tmp/port" ]; do
  i=$((i + 1))
  if [ "$i" -gt 50 ]; then
    echo "token server did not start" >&2
    exit 1
  fi
  sleep 0.1
done
port=$(cat "$tmp/port")

# finish_login sends http://127.0.0.1:631/ and the store maps that to
# http://127.0.0.1/, which is hashed as 127.0.0.1:80.
python3 - "$tmp" "$port" <<'PY'
import hashlib, json, sys
from pathlib import Path
tmp, port = Path(sys.argv[1]), sys.argv[2]
def h(s):
    return hashlib.sha256(s.encode()).hexdigest()
oauth = tmp / "oauth"
oauth.mkdir()
meta = {
    "issuer": "https://oauth.test:8639",
    "authorization_endpoint": "https://oauth.test:8639/auth",
    "token_endpoint": "http://127.0.0.1:%s/token" % port,
    "response_types_supported": ["code"],
}
(oauth / ("%s.meta" % h("oauth.test:8639"))).write_text(json.dumps(meta))
(oauth / ("%s+%s.clid" % (h("oauth.test:8639"), h("127.0.0.1:80")))).write_text("test-client")
PY

export LD_LIBRARY_PATH="$root/cups${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export DYLD_LIBRARY_PATH="$root/cups${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
export CUPS_DATADIR="$root"
export CUPS_USERCONFIG="$tmp"
export CUPS_SERVERROOT="$tmp"
export HOME="$tmp"
export LC_ALL=C
export LANG=C
export REQUEST_METHOD=GET
export SERVER_NAME=localhost
export SERVER_PORT=631
export CUPS_OAUTH_SERVER=https://oauth.test:8639
export HTTP_COOKIE="CUPS_SID=victim; CUPS_OAUTH_STATE=victim"

fail() {
  echo "$1" >&2
  echo "---- stdout ----" >&2
  cat "$2" >&2
  echo "---- stderr ----" >&2
  tail -c 600 "$3" >&2
  exit 1
}

run() {
  QUERY_STRING=$2 ./home.cgi >"$tmp/out-$1" 2>"$tmp/err-$1" || true
}

run foreign "code=abc&state=attacker"
run match "code=abc&state=victim"
run missing "code=abc"

if ! grep -q "does not match this browser" "$tmp/out-foreign" || grep -q "CUPS_BEARER=" "$tmp/out-foreign"; then
  fail "foreign state was accepted" "$tmp/out-foreign" "$tmp/err-foreign"
fi
if ! grep -q "CUPS_BEARER=attacker-token" "$tmp/out-match"; then
  fail "matching state did not store the bearer" "$tmp/out-match" "$tmp/err-match"
fi
if ! grep -q "does not match this browser" "$tmp/out-missing" || grep -q "CUPS_BEARER=" "$tmp/out-missing"; then
  fail "missing state was accepted" "$tmp/out-missing" "$tmp/err-missing"
fi

echo "OAuth callback state: PASS"
