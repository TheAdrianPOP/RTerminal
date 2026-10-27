import sys
import os
import lzma
import shutil
import struct
import subprocess
from concurrent.futures import ProcessPoolExecutor, as_completed

# ── Paths ────────────────────────────────────────────────────────────────────

SCRIPT_DIR  = os.path.dirname(os.path.abspath(__file__))
STUB_DIR    = os.path.join(SCRIPT_DIR, "..", "stub")
RES_DIR     = os.path.join(SCRIPT_DIR, "..", "res")
PAYLOAD_H   = os.path.join(STUB_DIR, "payload.h")
PAYLOAD_BIN = os.path.join(STUB_DIR, "payload.bin")
STUB_C      = os.path.join(STUB_DIR, "stub.c")
MERGE_LD    = os.path.join(STUB_DIR, "merge.ld")
STUB_EXE    = os.path.join(STUB_DIR, "stub_out.exe" if sys.platform == "win32" else "stub_out")

# ── Automatic BCJ Engine ─────────────────────────────────────────────────────

try:
    import bcj  # pip install pybcj (compiled native C extension)
    def x86_encode(data: bytes) -> bytes:
        return bcj.x86_filter(data)
    _BCJ_ENGINE = "native C (pybcj)"
except ImportError:
    def x86_encode(data: bytes) -> bytes:
        buf = bytearray(data)
        size = len(buf)
        if size < 5:
            return bytes(buf)

        mask_to_allowed = (1, 1, 1, 0, 1, 0, 0, 0)
        mask_to_bit     = (0, 1, 2, 2, 3, 3, 3, 3)

        buffer_pos = 0
        prev_pos_t = -1
        prev_mask = 0
        ip = 5

        while True:
            limit = size - 4
            p = buffer_pos
            while p < limit:
                if (buf[p] & 0xFE) == 0xE8:
                    break
                p += 1
            buffer_pos = p
            if p >= limit:
                break

            diff = buffer_pos - prev_pos_t
            if diff > 3:
                prev_mask = 0
            else:
                prev_mask = (prev_mask << (diff - 1)) & 0x7
                if prev_mask != 0:
                    b = buf[buffer_pos + 4 - mask_to_bit[prev_mask]]
                    if not mask_to_allowed[prev_mask] or b == 0 or b == 0xFF:
                        prev_pos_t = buffer_pos
                        prev_mask = ((prev_mask << 1) & 0x7) | 1
                        buffer_pos += 1
                        continue

            prev_pos_t = buffer_pos
            b4 = buf[buffer_pos + 4]
            if b4 == 0 or b4 == 0xFF:
                src = (b4 << 24) | (buf[buffer_pos + 3] << 16) | (buf[buffer_pos + 2] << 8) | buf[buffer_pos + 1]
                while True:
                    dest = (src + ip + buffer_pos) & 0xFFFFFFFF
                    if prev_mask == 0:
                        break
                    b_index = mask_to_bit[prev_mask] * 8
                    b = (dest >> (24 - b_index)) & 0xFF
                    if b != 0 and b != 0xFF:
                        break
                    src = dest ^ ((1 << (32 - b_index)) - 1)

                buf[buffer_pos + 4] = (~(((dest >> 24) & 1) - 1)) & 0xFF
                buf[buffer_pos + 3] = (dest >> 16) & 0xFF
                buf[buffer_pos + 2] = (dest >> 8) & 0xFF
                buf[buffer_pos + 1] = dest & 0xFF
                buffer_pos += 5
            else:
                prev_mask = ((prev_mask << 1) & 0x7) | 1
                buffer_pos += 1

        return bytes(buf)
    _BCJ_ENGINE = "built-in fallback"

# ── Helpers ──────────────────────────────────────────────────────────────────

def find_gcc(arch: int) -> str | None:
    if sys.platform != "win32":
        if arch == 32:
            return shutil.which("i686-linux-gnu-gcc") or shutil.which("gcc")
        return shutil.which("gcc")

    if arch == 64:
        candidates = [
            "C:/msys64/ucrt64/bin/gcc.exe",
            "C:/msys64/mingw64/bin/gcc.exe",
        ]
        fallback = "x86_64-w64-mingw32-gcc"
    else:
        candidates = [
            "C:/msys64/mingw32/bin/gcc.exe",
        ]
        fallback = "i686-w64-mingw32-gcc"

    for path in candidates:
        if os.path.isfile(path):
            return path

    return shutil.which(fallback) or shutil.which("gcc")


def get_arch(exe_path: str) -> int:
    try:
        with open(exe_path, "rb") as f:
            magic = f.read(4)
            if magic == b'\x7fELF':
                f.seek(4)
                ei_class = f.read(1)[0]
                return 32 if ei_class == 1 else 64
            elif magic[:2] == b'MZ':
                f.seek(0x3C)
                pe_offset = struct.unpack("<I", f.read(4))[0]
                f.seek(pe_offset)
                if f.read(4) == b'PE\x00\x00':
                    f.seek(pe_offset + 24)
                    opt_magic = struct.unpack("<H", f.read(2))[0]
                    if opt_magic == 0x20B:
                        return 64
                    elif opt_magic == 0x10B:
                        return 32
    except Exception:
        pass
    return 64


def get_pe_subsystem(exe_path: str) -> int:
    try:
        with open(exe_path, "rb") as f:
            if f.read(2) != b'MZ':
                return 3
            f.seek(0x3c)
            pe_offset = struct.unpack("<I", f.read(4))[0]
            f.seek(pe_offset + 4 + 20 + 68)
            return struct.unpack("<H", f.read(2))[0]
    except Exception:
        return 3


def write_merge_script(path: str) -> None:
    """Safely merge .rdata into .text while keeping .idata intact for valid PE headers."""
    content = (
        "SECTIONS\n"
        "{\n"
        "  .text : {\n"
        "    *(.text*)\n"
        "    *(.rdata*)\n"
        "    *(SORT(.rdata$*))\n"
        "    *(.data*)\n"
        "    *(.reloc)\n"
        "  }\n"
        "  /DISCARD/ : {\n"
        "    *(.pdata*)\n"
        "    *(.xdata*)\n"
        "    *(.comment*)\n"
        "    *(.note*)\n"
        "    *(.debug*)\n"
        "  }\n"
        "}\n"
    )
    with open(path, "w") as f:
        f.write(content)


def _strip_pe_tail(data: bytes) -> bytes:
    try:
        if len(data) < 0x40 or data[:2] != b'MZ':
            return data
        pe_offset = struct.unpack('<I', data[0x3C:0x40])[0]
        if data[pe_offset:pe_offset + 4] != b'PE\x00\x00':
            return data

        num_sec = struct.unpack('<H', data[pe_offset + 6:pe_offset + 8])[0]
        opt_hdr_size = struct.unpack('<H', data[pe_offset + 20:pe_offset + 22])[0]
        sec_table_off = pe_offset + 4 + 20 + opt_hdr_size

        last_raw_end = 0
        for i in range(num_sec):
            off = sec_table_off + i * 40
            raw_size = struct.unpack('<I', data[off + 16:off + 20])[0]
            raw_ptr  = struct.unpack('<I', data[off + 20:off + 24])[0]
            end = raw_ptr + raw_size
            if end > last_raw_end:
                last_raw_end = end

        if len(data) > last_raw_end:
            if any(b != 0 for b in data[last_raw_end:]):
                return data

        trimmed = len(data)
        while trimmed > sec_table_off + num_sec * 40 and data[trimmed - 1] == 0:
            trimmed -= 1

        if trimmed > len(data) * 0.10:
            return data[:trimmed]
    except Exception:
        pass
    return data


# ── Parallel Compression Sweeper ─────────────────────────────────────────────

def _compress_task(args):
    stream_data, is_bcj, tag, d_size, lc, lp, pb, depth = args
    f = {
        "id":        lzma.FILTER_LZMA1,
        "dict_size": d_size,
        "mode":      lzma.MODE_NORMAL,
        "mf":        lzma.MF_BT4,
        "nice_len":  273,
        "lc":        lc,
        "lp":        lp,
        "pb":        pb,
        "depth":     depth,
    }
    try:
        cand = lzma.compress(stream_data, format=lzma.FORMAT_ALONE, filters=[f])
        desc = f"[{tag}] dict={d_size // 1024}k lc={lc} lp={lp} pb={pb} depth={depth}"
        return (len(cand), cand, desc, is_bcj)
    except Exception:
        return None


def best_compress(data: bytes, *, arch: int = 64) -> tuple[bytes, str, bool]:
    high_yield_lc_lp_pb = [
        (3, 0, 0),  # Top candidate for x86/x64 binaries
        (3, 0, 2),  # 7-Zip & UPX standard
        (4, 0, 2),  # High-context model
        (2, 0, 2),
        (1, 0, 0),
        (2, 0, 0),
        (0, 0, 0),
        (3, 0, 3),  # 64-bit aligned
        (4, 0, 3),
        (1, 0, 1),
    ]
    depths = [128, 256]
    dict_sizes = [0x20000, max(0x10000, len(data))]

    candidates = [(x86_encode(data), True, "BCJ"), (data, False, "raw")]

    tasks = []
    for stream_data, is_bcj, tag in candidates:
        for d_size in dict_sizes:
            for lc, lp, pb in high_yield_lc_lp_pb:
                for depth in depths:
                    tasks.append((stream_data, is_bcj, tag, d_size, lc, lp, pb, depth))

    total = len(tasks)
    cpu_count = os.cpu_count() or 4
    print(f"[*] Sweeping {total} configurations across {cpu_count} CPU cores...")

    best_len = float("inf")
    best_cand = None
    best_desc = ""
    best_bcj = False

    done = 0
    with ProcessPoolExecutor(max_workers=cpu_count) as executor:
        futures = [executor.submit(_compress_task, t) for t in tasks]
        for f in as_completed(futures):
            res = f.result()
            done += 1
            if done % 5 == 0 or done == total:
                print(f"\r[*] Progress: {done}/{total} configurations tested...", end="", flush=True)
            if res is not None:
                size, cand, desc, is_bcj = res
                if size < best_len:
                    best_len = size
                    best_cand = cand
                    best_desc = desc
                    best_bcj = is_bcj

    print()

    if best_cand is None:
        best_cand = lzma.compress(data, format=lzma.FORMAT_ALONE, preset=lzma.PRESET_EXTREME | 9)
        best_desc = "[raw fallback] preset=9|EXTREME"
        best_bcj = False

    return best_cand, best_desc, best_bcj


def write_payload_header(original: bytes, compressed: bytes, use_bcj: bool) -> None:
    with open(PAYLOAD_H, "w") as f:
        f.write(f"#define PAYLOAD_ORIGINAL_SIZE    {len(original)}U\n")
        f.write(f"#define PAYLOAD_COMPRESSED_SIZE  {len(compressed)}U\n")
        f.write(f"#define PAYLOAD_USE_BCJ          {1 if use_bcj else 0}\n")
        f.write(
            "__attribute__((section(\".text\"), used))\n"
            "static const unsigned char payload[] = {\n"
        )
        for i in range(0, len(compressed), 16):
            chunk = compressed[i:i + 16]
            f.write("    " + ", ".join(str(b) for b in chunk) + ",\n")
        f.write("};\n")

    with open(PAYLOAD_BIN, "wb") as f:
        f.write(compressed)


# ── Compilation ──────────────────────────────────────────────────────────────

def _find_objcopy(gcc: str) -> str | None:
    gcc_dir = os.path.dirname(os.path.abspath(gcc))
    bin_name = "objcopy.exe" if sys.platform == "win32" else "objcopy"
    candidate = os.path.join(gcc_dir, bin_name)
    if os.path.isfile(candidate):
        return candidate
    return shutil.which("objcopy")


def find_icon_source(explicit_icon: str | None = None, exe_path: str | None = None) -> tuple[str | None, bool]:
    """
    Locates an icon resource (.rc or .ico file).
    Searches:
      1. Explicit icon path passed as argument
      2. Current working directory (project root during build)
      3. Ancestor directories of the target executable
      4. Fallback to SCRIPT_DIR/../res
    Returns (path_to_file, is_ico_file).
    """
    candidates = []

    if explicit_icon:
        if os.path.isdir(explicit_icon):
            candidates.extend([
                os.path.join(explicit_icon, "icon.rc"),
                os.path.join(explicit_icon, "res", "icon.rc"),
                os.path.join(explicit_icon, "icon.ico"),
                os.path.join(explicit_icon, "res", "icon.ico"),
            ])
        else:
            candidates.append(explicit_icon)

    # 1. Check current working directory
    cwd = os.getcwd()
    candidates.extend([
        os.path.join(cwd, "res", "icon.rc"),
        os.path.join(cwd, "res", "icon.ico"),
        os.path.join(cwd, "icon.rc"),
        os.path.join(cwd, "icon.ico"),
    ])

    # 2. Check parents of exe_path
    if exe_path:
        curr = os.path.abspath(exe_path)
        for _ in range(6):
            parent = os.path.dirname(curr)
            if parent == curr:
                break
            candidates.extend([
                os.path.join(parent, "res", "icon.rc"),
                os.path.join(parent, "res", "icon.ico"),
                os.path.join(parent, "icon.rc"),
                os.path.join(parent, "icon.ico"),
            ])
            curr = parent

    # 3. Fallback to SCRIPT_DIR
    candidates.extend([
        os.path.join(SCRIPT_DIR, "..", "res", "icon.rc"),
        os.path.join(SCRIPT_DIR, "..", "res", "icon.ico"),
    ])

    seen = set()
    for cand in candidates:
        cand_abs = os.path.abspath(cand)
        if cand_abs in seen:
            continue
        seen.add(cand_abs)
        if os.path.isfile(cand_abs):
            lower = cand_abs.lower()
            if lower.endswith(".rc"):
                return cand_abs, False
            if lower.endswith(".ico"):
                return cand_abs, True

    return None, False


def compile_stub(
    gcc: str,
    subsystem: str,
    arch: int = 64,
    use_icon: bool = True,
    icon_path: str | None = None,
    exe_path: str | None = None,
) -> bool:
    icon_obj = None
    generated_rc = os.path.join(STUB_DIR, "generated_icon.rc")

    if sys.platform == "win32" and use_icon:
        windres = gcc.replace("gcc.exe", "windres.exe")
        if not os.path.isfile(windres):
            windres = shutil.which("windres") or "windres"

        icon_src, is_ico = find_icon_source(icon_path, exe_path)
        if icon_src:
            rc_file = icon_src
            if is_ico:
                ico_clean = os.path.abspath(icon_src).replace("\\", "/")
                with open(generated_rc, "w") as f:
                    f.write(f'IDI_ICON1 ICON DISCARDABLE "{ico_clean}"\n')
                rc_file = generated_rc

            icon_obj = os.path.join(STUB_DIR, "icon.o")
            rc_dir = os.path.dirname(os.path.abspath(icon_src))
            cmd_windres = [windres, "-I", rc_dir, "-i", rc_file, "-o", icon_obj]
            res = subprocess.run(cmd_windres, cwd=rc_dir, capture_output=True, text=True)
            if res.returncode == 0 and os.path.isfile(icon_obj):
                rel_name = os.path.basename(icon_src)
                print(f"[*] Icon resource compiled ({rel_name}): {os.path.getsize(icon_obj):,} bytes")
            else:
                icon_obj = None
                print(f"[!] Warning: Failed to compile icon ({icon_src}) with windres")
                if res.stderr.strip():
                    print(f"    {res.stderr.strip()}")
        elif icon_path:
            print(f"[!] Warning: Specified icon file not found: {icon_path}")
    elif not use_icon:
        print("[*] Icon: skipped (--no-icon)")

    entry_sym = ("_mainCRTStartup" if arch == 32 else "mainCRTStartup") if sys.platform == "win32" else "_start"
    m_flag = f"-m{arch}"

    cmd = [
        gcc, m_flag, "-Os", "-flto", "-s",
        "-fwhole-program",
        "-ffreestanding",
        "-nostdlib", "-nostartfiles",
        "-fno-ident", "-fomit-frame-pointer",
        "-ffunction-sections", "-fdata-sections",
        "-fno-stack-protector", "-fno-plt",
        "-fno-unwind-tables", "-fno-asynchronous-unwind-tables",
        "-fno-exceptions", "-fno-rtti",
        "-fmerge-all-constants",
        "-falign-functions=1", "-falign-jumps=1", "-falign-loops=1", "-falign-labels=1",
        "-Wl,--gc-sections",
        f"-Wl,-e,{entry_sym}",
        f"-Wl,-u,{entry_sym}",
        "-Wl,--allow-multiple-definition",
        "-Wl,--build-id=none",
        "-I", STUB_DIR, STUB_C,
        "-o", STUB_EXE,
    ]

    if sys.platform == "win32":
        stub_base = "0x200000000" if arch == 64 else "0x10000000"
        write_merge_script(MERGE_LD)

        cmd += [
            "-Wl,--file-alignment,512",
            "-Wl,--section-alignment,4096",
            f"-Wl,--subsystem,{subsystem}",
            "-Wl,--no-insert-timestamp",
            "-Wl,--disable-auto-import",
            "-Wl,-O2",
            "-Wl,--disable-reloc-section",
            f"-Wl,--image-base,{stub_base}",
            "-Wl,--dynamicbase",
        ]
        if arch == 64:
            cmd.append("-Wl,--high-entropy-va")
        cmd += [
            MERGE_LD,
            "-lkernel32",
        ]
        if icon_obj and os.path.isfile(icon_obj):
            cmd.append(icon_obj)
    else:
        cmd += [
            "-static",
            "-Wl,-z,noseparate-code",
            "-Wl,-z,norelro",
            "-Wl,--hash-style=sysv",
        ]

    env = os.environ.copy()
    if sys.platform == "win32":
        gcc_bin_dir = os.path.dirname(os.path.abspath(gcc))
        env["PATH"] = gcc_bin_dir + os.pathsep + env.get("PATH", "")

    result = subprocess.run(cmd, capture_output=True, text=True, env=env)
    if result.returncode != 0:
        print("\n[!] Compilation failed")
        if result.stderr.strip():
            print(result.stderr)
        return False

    objcopy = _find_objcopy(gcc)
    if objcopy:
        if sys.platform == "win32":
            subprocess.run(
                [objcopy, "--strip-all",
                 "--remove-section=.debug_info",
                 "--remove-section=.debug_abbrev",
                 "--remove-section=.debug_line",
                 "--remove-section=.debug_str",
                 "--remove-section=.pdata",
                 "--remove-section=.xdata",
                 "--remove-section=.comment",
                 STUB_EXE],
                capture_output=True,
            )
        else:
            subprocess.run(
                [objcopy,
                 "--remove-section=.note.gnu.property",
                 "--remove-section=.note.ABI-tag",
                 STUB_EXE],
                capture_output=True,
            )

    return True


def cleanup() -> None:
    for f in [
        STUB_EXE,
        os.path.join(STUB_DIR, "icon.o"),
        os.path.join(STUB_DIR, "generated_icon.rc"),
        PAYLOAD_BIN,
        MERGE_LD,
    ]:
        if os.path.isfile(f):
            try:
                os.remove(f)
            except Exception:
                pass


# ── Main ─────────────────────────────────────────────────────────────────────

def pack(exe_path: str, use_icon: bool = True, icon_path: str | None = None) -> None:
    exe_path = os.path.abspath(exe_path)
    if not os.path.isfile(exe_path):
        print(f"[!] File not found: {exe_path}")
        sys.exit(1)

    arch = get_arch(exe_path)
    gcc = find_gcc(arch)
    if not gcc:
        print(f"[!] No {arch}-bit GCC found.")
        sys.exit(1)

    subsystem_str = "console"
    if sys.platform == "win32":
        subsystem_id  = get_pe_subsystem(exe_path)
        subsystem_str = "console" if subsystem_id == 3 else "windows"

    print(f"[*] Packing: {exe_path} ({arch}-bit)")
    print(f"[*] BCJ Engine: {_BCJ_ENGINE}")

    with open(exe_path, "rb") as f:
        original = f.read()

    # Prevent recursive packing of already-packed executables
    if b"RTPACK_V1" in original:
        print(f"[*] Executable is already packed: {exe_path} (skipping redundant pack)")
        return

    if sys.platform == "win32":
        stripped = _strip_pe_tail(original)
        if len(stripped) < len(original):
            saved = len(original) - len(stripped)
            print(f"[*] Pre-stripped PE tail: -{saved:,} bytes")
    else:
        stripped = original

    compressed, best_desc, use_bcj = best_compress(stripped, arch=arch)

    ratio = len(compressed) / len(original) * 100
    print(f"[*] Best config: {best_desc}")
    print(f"[*] Compressed payload: {len(original):,} -> {len(compressed):,} bytes ({ratio:.2f}%)")

    write_payload_header(original, compressed, use_bcj)

    if not compile_stub(gcc, subsystem_str, arch, use_icon=use_icon, icon_path=icon_path, exe_path=exe_path):
        cleanup()
        sys.exit(1)

    final_size = os.path.getsize(STUB_EXE)
    shutil.copy2(STUB_EXE, exe_path)
    if sys.platform != "win32":
        os.chmod(exe_path, os.stat(exe_path).st_mode | 0o755)
    cleanup()

    diff = final_size - len(compressed)
    print(f"[+] Final executable : {final_size:,} bytes (overall {final_size / len(original) * 100:.2f}%)")
    print(f"[*] Total stub overhead: +{diff:,} bytes (Headers + Runtime + Alignment)")


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="Custom PE Packer")
    parser.add_argument("exe", help="Path to executable to pack")
    parser.add_argument("--no-icon", action="store_true", help="Disable embedding icon")
    parser.add_argument("--icon", default=None, help="Path to .ico or .rc file for the icon resource")
    args = parser.parse_args()
    pack(args.exe, use_icon=not args.no_icon, icon_path=args.icon)