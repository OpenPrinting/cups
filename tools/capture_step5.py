import subprocess
import os
import time

CHROME = r"C:\Program Files\Google\Chrome\Application\chrome.exe"
ARTIFACTS = r"C:\Users\Targino\.gemini\antigravity-ide\brain\d2091585-ee85-46a8-aa49-603aaf3d8b75"

TESTS = [
    ("http://127.0.0.1:8631/?mode=beginner", "step5_desktop_home_beginner.png", (1440, 900)),
    ("http://127.0.0.1:8631/?mode=advanced", "step5_desktop_home_advanced.png", (1440, 900)),
    ("http://127.0.0.1:8631/?palette=1", "step5_desktop_cmd_palette.png", (1440, 900)),
    ("http://127.0.0.1:8631/?tour=1", "step5_desktop_tour_modal.png", (1440, 900)),
    ("http://127.0.0.1:8631/?mode=beginner", "step5_tablet_home_beginner.png", (768, 1024)),
    ("http://127.0.0.1:8631/?mode=beginner", "step5_mobile_home_beginner.png", (360, 800)),
    ("http://127.0.0.1:8631/?palette=1", "step5_mobile_cmd_palette.png", (360, 800)),
]

for url, filename, (width, height) in TESTS:
    out_file = os.path.join(ARTIFACTS, filename)
    print(f"Capturing {filename} at {width}x{height} from {url}...")
    subprocess.run([
        CHROME,
        "--headless=new",
        "--hide-scrollbars",
        f"--window-size={width},{height}",
        "--virtual-time-budget=2000",
        f"--screenshot={out_file}",
        url
    ])
    if os.path.exists(out_file):
        print(f"  OK: {os.path.getsize(out_file)} bytes")
    else:
        print(f"  FAILED to generate {out_file}")

print("All captures completed.")
