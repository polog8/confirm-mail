#!/usr/bin/env python3
"""Obtains a Spotify refresh token for the desk station (standard library only).

    python3 tools/spotify_auth.py            # reads the client id/secret from include/config.h
    python3 tools/spotify_auth.py --write    # ... and writes the token back into include/config.h

Prerequisite: in https://developer.spotify.com/dashboard, the app must list the
redirect URI  http://127.0.0.1:8888/callback  (Spotify rejects "localhost").
"""
import argparse
import base64
import http.server
import json
import os
import re
import secrets
import sys
import urllib.parse
import urllib.request
import webbrowser

REDIRECT_URI = "http://127.0.0.1:8888/callback"
SCOPES = ("user-read-playback-state user-modify-playback-state user-read-currently-playing "
          "user-library-read user-library-modify")
CONFIG = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "include", "config.h")


def read_config_value(name):
    try:
        with open(CONFIG, encoding="utf-8") as f:
            m = re.search(rf'{name}\s*=\s*"([^"]*)"', f.read())
            return m.group(1) if m else ""
    except FileNotFoundError:
        return ""


def write_refresh_token(token):
    with open(CONFIG, encoding="utf-8") as f:
        text = f.read()
    new, n = re.subn(r'(kSpotifyRefreshToken\s*=\s*)"[^"]*"', lambda m: f'{m.group(1)}"{token}"', text)
    if n != 1:
        raise SystemExit("kSpotifyRefreshToken introuvable dans include/config.h")
    with open(CONFIG, "w", encoding="utf-8") as f:
        f.write(new)


def exchange(code, client_id, client_secret):
    data = urllib.parse.urlencode({
        "grant_type": "authorization_code",
        "code": code,
        "redirect_uri": REDIRECT_URI,
    }).encode()
    auth = base64.b64encode(f"{client_id}:{client_secret}".encode()).decode()
    req = urllib.request.Request("https://accounts.spotify.com/api/token", data=data, headers={
        "Authorization": f"Basic {auth}",
        "Content-Type": "application/x-www-form-urlencoded",
    })
    with urllib.request.urlopen(req, timeout=20) as resp:
        return json.load(resp)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--client-id", default=read_config_value("kSpotifyClientId"))
    ap.add_argument("--client-secret", default=read_config_value("kSpotifyClientSecret"))
    ap.add_argument("--write", action="store_true", help="écrit le jeton dans include/config.h")
    args = ap.parse_args()
    if not args.client_id or not args.client_secret:
        raise SystemExit("Client ID / secret manquants (include/config.h ou --client-id/--client-secret).")

    state = secrets.token_hex(8)
    url = "https://accounts.spotify.com/authorize?" + urllib.parse.urlencode({
        "response_type": "code",
        "client_id": args.client_id,
        "scope": SCOPES,
        "redirect_uri": REDIRECT_URI,
        "state": state,
        "show_dialog": "true",
    })
    result = {}

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            query = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
            if not self.path.startswith("/callback"):
                self.send_error(404)
                return
            if query.get("state", [""])[0] != state:
                result["error"] = "state invalide"
            elif "error" in query:
                result["error"] = query["error"][0]
            else:
                try:
                    result["token"] = exchange(query["code"][0], args.client_id, args.client_secret)
                except Exception as exc:  # noqa: BLE001 - reported to the user
                    result["error"] = str(exc)
            ok = "token" in result
            body = ("<h2>C'est bon ✓</h2><p>Vous pouvez fermer cet onglet.</p>" if ok
                    else f"<h2>Échec</h2><p>{result.get('error')}</p>")
            self.send_response(200 if ok else 400)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.end_headers()
            self.wfile.write(f"<meta charset=utf-8><body style='font-family:sans-serif'>{body}".encode())

        def log_message(self, *_):
            pass

    server = http.server.HTTPServer(("127.0.0.1", 8888), Handler)
    print("Ouverture du navigateur pour autoriser l'application…")
    print("Si rien ne s'ouvre, copiez ce lien :\n" + url + "\n")
    webbrowser.open(url)
    while not result:
        server.handle_request()
    server.server_close()

    if "error" in result:
        raise SystemExit("Erreur : " + result["error"])
    refresh = result["token"].get("refresh_token", "")
    print("Refresh token :\n\n  " + refresh + "\n")
    if args.write:
        write_refresh_token(refresh)
        print("Écrit dans include/config.h (kSpotifyRefreshToken). Recompilez et flashez.")
    else:
        print("Collez-le dans include/config.h -> kSpotifyRefreshToken, ou relancez avec --write.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
