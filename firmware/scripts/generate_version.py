from pathlib import Path
import subprocess

PROJECT_ROOT = Path("__file__").resolve().parents[0]
SRC_DIR = PROJECT_ROOT / "src"
VERSION_HEADER = SRC_DIR / "version.h"
HEADER_GUARD = "AURAMON_VERSION_H"


def run_git(args):
    try:
        output = subprocess.check_output([
            "git",
            "-C",
            str(PROJECT_ROOT),
            *args,
        ], stderr=subprocess.STDOUT)
    except (subprocess.CalledProcessError, FileNotFoundError):
        return None
    return output.decode().strip()


def detect_version():
    exact_tag = run_git(["describe", "--tags", "--exact-match"])
    if exact_tag:
        version = exact_tag
    else:
        last_tag = run_git(["describe", "--tags", "--abbrev=0"])
        short_hash = run_git(["rev-parse", "--short", "HEAD"])
        if last_tag and short_hash:
            version = f"{last_tag}+{short_hash}"
        elif short_hash:
            version = f"untagged+{short_hash}"
        else:
            version = "unknown"
    dirty_state = run_git(["status", "--porcelain"])
    if dirty_state:
        version = f"{version}-dirty"
    return version


def write_header(version):
    header = (
        f"#ifndef {HEADER_GUARD}\n"
        f"#define {HEADER_GUARD}\n\n"
        f"constexpr const char AURAMON_VERSION[] = \"{version}\";\n\n"
        f"#endif // {HEADER_GUARD}\n"
    )
    SRC_DIR.mkdir(parents=True, exist_ok=True)
    if VERSION_HEADER.exists() and VERSION_HEADER.read_text() == header:
        return
    VERSION_HEADER.write_text(header)
    print(f"Generated version header: {version}")


def main():
    write_header(detect_version())


main()
