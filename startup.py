#!/usr/bin/env python3
"""
startup.py

Fixes the common ESP-IDF/Ninja issue:
    ninja: error: manifest 'build.ninja' still dirty after 100 tries

Run this from the ESP-IDF project root, the folder that contains CMakeLists.txt.
Example:
    cd ~/esp_projects/esp32_cam_rtos_firmware
    python3 startup.py --flash

This script:
  1. Verifies it is running inside an ESP-IDF project.
  2. Finds idf.py from the active ESP-IDF environment.
  3. Stops leftover monitor/serial processes that may keep the port busy.
  4. Removes build cache files that trigger repeated CMake/Ninja regeneration.
  5. Normalizes project file timestamps using touch.
  6. Runs idf.py set-target esp32.
  7. Runs idf.py build.
  8. Optionally flashes or flash-monitors the board.
"""

from __future__ import annotations

import argparse
import os
import shutil
import signal
import subprocess
import re
import sys
from pathlib import Path


PROJECT_MARKERS = ["CMakeLists.txt", "main"]
DEFAULT_TARGET = "esp32"


def run(cmd: list[str], *, check: bool = True) -> subprocess.CompletedProcess[str]:
    print("\n$ " + " ".join(cmd), flush=True)
    return subprocess.run(cmd, text=True, check=check)


def require_project_root(project_dir: Path) -> None:
    missing = [name for name in PROJECT_MARKERS if not (project_dir / name).exists()]
    if missing:
        print("ERROR: This does not look like the ESP-IDF project root.")
        print(f"Current folder: {project_dir}")
        print(f"Missing: {', '.join(missing)}")
        print("Run this script from the folder that contains CMakeLists.txt and main/.")
        sys.exit(2)


def find_idf_py() -> str:
    idf_path = os.environ.get("IDF_PATH")
    candidates: list[Path] = []
    if idf_path:
        candidates.append(Path(idf_path) / "tools" / "idf.py")
    which_idf = shutil.which("idf.py")
    if which_idf:
        candidates.append(Path(which_idf))

    for candidate in candidates:
        if candidate.exists():
            return str(candidate)

    print("ERROR: Could not find idf.py.")
    print("First activate ESP-IDF, for example:")
    print("  . ~/esp/esp-idf-v5.5.2/export.sh")
    sys.exit(2)



def read_usb_serial_port(project_dir: Path) -> str:
    config_path = project_dir / "main" / "config.h"
    if not config_path.exists():
        return ""
    text = config_path.read_text(errors="ignore")
    match = re.search(r'^\s*#define\s+USB_SERIAL_PORT\s+"([^"]+)"', text, re.MULTILINE)
    return match.group(1).strip() if match else ""

def stop_leftover_processes() -> None:
    # This avoids killing random Python tasks. It only targets common ESP-IDF monitor/esptool processes.
    patterns = ["idf_monitor.py", "esptool.py"]
    try:
        ps = subprocess.check_output(["ps", "-ax", "-o", "pid=,command="], text=True)
    except Exception as exc:
        print(f"WARNING: Could not list processes: {exc}")
        return

    my_pid = os.getpid()
    for line in ps.splitlines():
        line = line.strip()
        if not line:
            continue
        parts = line.split(maxsplit=1)
        if len(parts) != 2:
            continue
        pid_text, command = parts
        try:
            pid = int(pid_text)
        except ValueError:
            continue
        if pid == my_pid:
            continue
        if any(pattern in command for pattern in patterns):
            print(f"Stopping leftover ESP-IDF process {pid}: {command}")
            try:
                os.kill(pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            except PermissionError:
                print(f"WARNING: No permission to stop process {pid}")


def remove_path(path: Path) -> None:
    if path.is_dir():
        print(f"Removing directory: {path}")
        shutil.rmtree(path, ignore_errors=True)
    elif path.exists():
        print(f"Removing file: {path}")
        try:
            path.unlink()
        except FileNotFoundError:
            pass


def clean_problem_files(project_dir: Path, deep: bool) -> None:
    # Safe cleanup for Ninja/CMake dirty-manifest loops.
    paths = [
        project_dir / "build",
        project_dir / "sdkconfig.old",
        project_dir / "dependencies.lock",
        project_dir / ".pytest_cache",
    ]

    # Keep managed_components by default so dependency downloads do not happen every time.
    # Deep mode removes it too.
    if deep:
        paths.append(project_dir / "managed_components")

    for path in paths:
        remove_path(path)


def normalize_timestamps(project_dir: Path) -> None:
    # Ninja dirty-manifest loops are often caused by files with odd timestamps.
    # Touch important project files and directories without changing contents.
    touch_targets = [
        project_dir / "CMakeLists.txt",
        project_dir / "sdkconfig.defaults",
        project_dir / "partitions.csv",
        project_dir / "idf_component.yml",
        project_dir / "main" / "CMakeLists.txt",
    ]

    for root_name in ["main", "components"]:
        root = project_dir / root_name
        if root.exists():
            for path in root.rglob("*"):
                if path.is_file() and path.suffix.lower() in {".c", ".h", ".cpp", ".hpp", ".cmake", ".yml", ".yaml", ".txt"}:
                    touch_targets.append(path)

    for path in touch_targets:
        if path.exists():
            path.touch()

    print(f"Normalized timestamps for {len([p for p in touch_targets if p.exists()])} project files.")


def main() -> int:
    parser = argparse.ArgumentParser(description="Fix ESP-IDF Ninja dirty build issues and rebuild the project.")
    parser.add_argument("--target", default=DEFAULT_TARGET, help="ESP-IDF target. Default: esp32")
    parser.add_argument("--port", default="", help="Serial port override. Default comes from main/config.h USB_SERIAL_PORT")
    parser.add_argument("--deep", action="store_true", help="Also remove managed_components and re-resolve dependencies.")
    parser.add_argument("--flash", action="store_true", help="Flash after a successful build.")
    parser.add_argument("--monitor", action="store_true", help="Flash and open monitor after a successful build. Requires --port.")
    parser.add_argument("--skip-set-target", action="store_true", help="Skip idf.py set-target if sdkconfig is already correct.")
    args = parser.parse_args()

    project_dir = Path.cwd().resolve()
    require_project_root(project_dir)
    idf_py = find_idf_py()

    if not args.port:
        args.port = read_usb_serial_port(project_dir)

    print("ESP-IDF startup script")
    print(f"Project: {project_dir}")
    print(f"idf.py:  {idf_py}")

    if args.monitor and not args.port:
        print("ERROR: --monitor requires USB_SERIAL_PORT in main/config.h or --port")
        return 2
    if args.flash and not args.port:
        print("ERROR: --flash requires USB_SERIAL_PORT in main/config.h or --port")
        return 2

    stop_leftover_processes()
    clean_problem_files(project_dir, deep=args.deep)
    normalize_timestamps(project_dir)

    if not args.skip_set_target:
        run([sys.executable, idf_py, "set-target", args.target])

    run([sys.executable, idf_py, "build"])

    if args.monitor:
        run([sys.executable, idf_py, "-p", args.port, "flash", "monitor"])
    elif args.flash:
        run([sys.executable, idf_py, "-p", args.port, "flash"])

    print("\nDone. If the dirty-manifest error still appears, run again with --deep.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
