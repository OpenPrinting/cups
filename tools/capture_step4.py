import subprocess
import os

CHROME = r"C:\Program Files\Google\Chrome\Application\chrome.exe"
ARTIFACTS = r"C:\Users\Targino\.gemini\antigravity-ide\brain\d2091585-ee85-46a8-aa49-603aaf3d8b75"

SCREENS = [
    ("http://127.0.0.1:8631/", "step4_animated_home.png"),
    ("http://127.0.0.1:8631/printers/Zebra-GK420t-Etiquetas", "step4_animated_printer_zebra.png"),
    ("http://127.0.0.1:8631/jobs", "step4_animated_jobs.png"),
    ("http://127.0.0.1:8631/admin", "step4_animated_admin.png"),
]

for url, filename in SCREENS:
    out_file = os.path.join(ARTIFACTS, filename)
    print(f"Capturing {filename} from {url}...")
    subprocess.run([
        CHROME,
        "--headless=new",
        "--hide-scrollbars",
        "--window-size=1440,900",
        f"--screenshot={out_file}",
        url
    ])
    if os.path.exists(out_file):
        print(f"  OK: {os.path.getsize(out_file)} bytes")

print("Done!")
