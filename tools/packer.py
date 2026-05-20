import sys
import os
import lzma
import shutil
import struct
import subprocess

# ── Paths ────────────────────────────────────────────────────────────────────

SCRIPT_DIR  = os.path.dirname(os.path.abspath(__file__))
STUB_DIR    = os.path.join(SCRIPT_DIR, "..", "stub")
PAYLOAD_H   = os.path.join(STUB_DIR, "payload.h")
STUB_C      = os.path.join(STUB_DIR, "stub.c")
LZMADEC_C   = os.path.join(STUB_DIR, "lzmadec.c")
STUB_EXE    = os.path.join(STUB_DIR, "stub_out.exe" if sys.platform == "win32" else "stub_out")

GCC_CANDIDATES = [
    "C:/msys64/mingw32/bin/gcc.exe",
    "C:/msys64/ucrt64/bin/gcc.exe",
]

# ── Helpers ──────────────────────────────────────────────────────────────────

def find_gcc() -> str | None:
    """Return a usable GCC."""
    if sys.platform != "win32":
        return shutil.which("gcc")
    for path in GCC_CANDIDATES:
        if os.path.isfile(path):
            return path
    return shutil.which("i686-w64-mingw32-gcc")


def get_elf_arch(exe_path: str) -> int:
    """Return 32 or 64 based on the ELF e_machine field. Returns 64 if not an ELF."""
    try:
        with open(exe_path, "rb") as f:
            magic = f.read(4)
            if magic != b'\x7fELF':
                return 32  # PE/Windows binary, keep existing -m32 default
            f.seek(4)
            ei_class = f.read(1)[0]  # 1 = 32-bit, 2 = 64-bit
            return 32 if ei_class == 1 else 64
    except Exception:
        return 64


def get_pe_subsystem(exe_path: str) -> int:
    """Read the Subsystem field from the PE optional header."""
    if sys.platform != "win32":
        return 3 # Default to console for Linux ELF
        
    try:
        with open(exe_path, "rb") as f:
            f.seek(0x3c)
            pe_offset = struct.unpack("<I", f.read(4))[0]
            f.seek(pe_offset + 4 + 20 + 68)
            return struct.unpack("<H", f.read(2))[0]
    except Exception:
        return 2  # default to GUI


def write_payload_header(original: bytes, compressed: bytes) -> None:
    with open(PAYLOAD_H, "w") as f:
        f.write(f"#define PAYLOAD_ORIGINAL_SIZE    {len(original)}U\n")
        f.write(f"#define PAYLOAD_COMPRESSED_SIZE  {len(compressed)}U\n")
        f.write("static const unsigned char payload[] = {\n")
        for i in range(0, len(compressed), 16):
            chunk = compressed[i:i + 16]
            f.write("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",\n")
        f.write("};\n")


def compile_stub(gcc: str, subsystem: str, arch: int = 32) -> bool:
    """Compile stub.c -> stub_out. Returns True on success."""

    m_flag = f"-m{arch}"
    cmd = [
        gcc, m_flag, "-Os", "-flto", "-s",
        "-fwhole-program",
        "-nostdlib", "-nostartfiles",
        "-fno-ident", "-fomit-frame-pointer",
        "-ffunction-sections", "-fdata-sections",
        "-fno-stack-protector", "-fno-plt",
        "-fno-unwind-tables", "-fno-asynchronous-unwind-tables",
        "-Wl,--gc-sections",
        "-I", STUB_DIR, STUB_C, LZMADEC_C,
        "-o", STUB_EXE,
    ]
    
    if sys.platform == "win32":
        cmd += [
            "-Wl,--print-gc-sections",
            "-Wl,--section-alignment,4096",
            f"-Wl,--subsystem,{subsystem}",
            "-lkernel32", "-luser32",
        ]
    else:
        # Linux specific tweaks
        cmd += ["-static"]

    env = os.environ.copy()
    if sys.platform == "win32":
        gcc_bin_dir = os.path.dirname(os.path.abspath(gcc))
        env["PATH"] = gcc_bin_dir + os.pathsep + env.get("PATH", "")

    print(f"[*] GCC: {gcc}")
    print(f"[*] Stub arch: {arch}-bit ({m_flag})")
    if sys.platform == "win32":
        print(f"[*] Subsystem: {subsystem}")
    print(f"[*] Compiling stub...")

    result = subprocess.run(cmd, capture_output=True, text=True, env=env)

    if result.returncode != 0:
        print("\n[!] Compilation failed")
        print("─" * 60)
        if result.stdout.strip():
            print(result.stdout)
        if result.stderr.strip():
            print(result.stderr)
        print("─" * 60)
        print(f"[!] Full command:\n    {' '.join(cmd)}")
        return False

    return True


def cleanup() -> None:
    if os.path.isfile(STUB_EXE):
        os.remove(STUB_EXE)

# ── Main ─────────────────────────────────────────────────────────────────────

def pack(exe_path: str) -> None:
    exe_path = os.path.abspath(exe_path)

    if not os.path.isfile(exe_path):
        print(f"[!] File not found: {exe_path}")
        sys.exit(1)

    # ── 1. Locate GCC ────────────────────────────────────────────────────────
    gcc = find_gcc()
    if not gcc:
        print("[!] No 32-bit GCC found.")
        if sys.platform == "win32":
            print("    Tried:")
            for c in GCC_CANDIDATES:
                print(f"      {c}")
            print("    Also tried: i686-w64-mingw32-gcc (PATH)")
        else:
            print("    Ensure 'gcc' is in your PATH.")
        sys.exit(1)

    # ── 2. Detect subsystem ──────────────────────────────────────────────────
    subsystem_str = "console"
    if sys.platform == "win32":
        subsystem_id  = get_pe_subsystem(exe_path)
        subsystem_str = "console" if subsystem_id == 3 else "windows"
        print(f"[*] Detected subsystem: {subsystem_str} ({subsystem_id})")

    # ── 3. Detect architecture ───────────────────────────────────────────────
    arch = get_elf_arch(exe_path) if sys.platform != "win32" else 32
    print(f"[*] Payload arch: {arch}-bit")

    # ── 4. Compress payload ──────────────────────────────────────────────────
    print(f"[*] Packing: {exe_path}")
    original   = open(exe_path, "rb").read()
    compressed = lzma.compress(original, format=lzma.FORMAT_ALONE, preset=9)

    ratio = len(compressed) / len(original) * 100
    print(f"[*] Compressed: {len(original):,} -> {len(compressed):,} bytes ({ratio:.1f}%)")

    # ── 5. Write header ──────────────────────────────────────────────────────
    write_payload_header(original, compressed)
    print(f"[*] Wrote payload header: {PAYLOAD_H}")

    # ── 6. Compile ───────────────────────────────────────────────────────────
    if not compile_stub(gcc, subsystem_str, arch):
        cleanup()
        sys.exit(1)

    # ── 7. Replace original ──────────────────────────────────────────────────
    shutil.copy2(STUB_EXE, exe_path)
    cleanup()

    print(f"[+] Done: {exe_path} ({os.path.getsize(exe_path):,} bytes)")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: packer.py <exe>")
        sys.exit(1)
    pack(sys.argv[1])
