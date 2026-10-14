#!/usr/bin/env -S uv run --script
"""Screenshot DIR/index.html to DIR/leaderboard.jpg with headless Chromium.
Usage: render.py DIR   (needs `pip install playwright` + `playwright install chromium`)"""
import sys, pathlib, http.server, threading, functools
from playwright.sync_api import sync_playwright

d = pathlib.Path(sys.argv[1]).resolve()
h = functools.partial(http.server.SimpleHTTPRequestHandler, directory=str(d))
h.log_message = lambda *a, **k: None
srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), h)
threading.Thread(target=srv.serve_forever, daemon=True).start()
with sync_playwright() as p:
    b = p.chromium.launch()
    pg = b.new_page(viewport={"width": 1000, "height": 800}, device_scale_factor=1.5)
    pg.goto(f"http://127.0.0.1:{srv.server_port}/index.html")
    pg.wait_for_selector("body[data-ready]")
    pg.wait_for_load_state("networkidle")
    pg.wait_for_timeout(1500)  # entrance animations + avatars
    pg.screenshot(path=str(d / "leaderboard.jpg"), type="jpeg", quality=88, full_page=True)
    b.close()
srv.shutdown()
