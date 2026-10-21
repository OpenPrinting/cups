import os
import subprocess
import time

CHROME_PATH = r"C:\Program Files\Google\Chrome\Application\chrome.exe"
ARTIFACT_DIR = r"C:\Users\Targino\.gemini\antigravity-ide\brain\d2091585-ee85-46a8-aa49-603aaf3d8b75"

SCENARIOS = [
    # Desktop 1440x900
    ("http://127.0.0.1:8631/", 1440, 900, "step3_desktop_home.png"),
    ("http://127.0.0.1:8631/printers", 1440, 900, "step3_desktop_printers.png"),
    ("http://127.0.0.1:8631/printers/LaserJet-Pro-M404dn", 1440, 900, "step3_desktop_printer_laserjet.png"),
    ("http://127.0.0.1:8631/printers/Zebra-GK420t-Etiquetas", 1440, 900, "step3_desktop_printer_zebra.png"),
    ("http://127.0.0.1:8631/jobs", 1440, 900, "step3_desktop_jobs.png"),
    ("http://127.0.0.1:8631/admin", 1440, 900, "step3_desktop_admin.png"),
    ("http://127.0.0.1:8631/error", 1440, 900, "step3_desktop_error.png"),
    
    # Tablet 768x1024
    ("http://127.0.0.1:8631/", 768, 1024, "step3_tablet_home.png"),
    ("http://127.0.0.1:8631/printers", 768, 1024, "step3_tablet_printers.png"),
    ("http://127.0.0.1:8631/printers/LaserJet-Pro-M404dn", 768, 1024, "step3_tablet_printer_detail.png"),
    
    # Mobile 360x800
    ("http://127.0.0.1:8631/", 360, 800, "step3_mobile_home.png"),
    ("http://127.0.0.1:8631/printers", 360, 800, "step3_mobile_printers.png"),
    ("http://127.0.0.1:8631/printers/LaserJet-Pro-M404dn", 360, 800, "step3_mobile_printer_detail.png"),
    ("http://127.0.0.1:8631/jobs", 360, 800, "step3_mobile_jobs.png"),
]

for url, width, height, filename in SCENARIOS:
    out_path = os.path.join(ARTIFACT_DIR, filename)
    cmd = [
        CHROME_PATH,
        "--headless=new",
        "--hide-scrollbars",
        f"--window-size={width},{height}",
        f"--screenshot={out_path}",
        url
    ]
    print(f"Capturing {filename} ({width}x{height}) from {url}...")
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode == 0 and os.path.exists(out_path):
        size = os.path.getsize(out_path)
        print(f"  OK: {size} bytes")
    else:
        print(f"  FAILED: {res.stderr}")

print("Screenshot matrix capture completed!")
