import re
import sys
import os

# Anchor symbol used to calculate the ASLR slide offset
ANCHOR_SYMBOL_PATTERN = r"runtime_init|raw_heap_alloc_tracked|mainCRTStartup"

def parse_map_file(map_path):
    symbols = []
    current_symbol = None
    
    # Matches GCC/MinGW .text sections: .text$_ZN... or .text$main
    symbol_pat = re.compile(r"^\s+\.text\$([^\s]+)")
    # Matches address definitions in map files
    addr_pat = re.compile(r"^\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+|[0-9a-fA-F]+)\s+build")
    # Matches standard symbol entries: 0x00428ee0 runtime_init
    classic_pat = re.compile(r"^\s+(0x[0-9a-fA-F]+)\s+([^\s]+)$")

    try:
        with open(map_path, 'r', encoding='utf-8', errors='ignore') as f:
            for line in f:
                s_match = symbol_pat.match(line)
                if s_match:
                    current_symbol = s_match.group(1)
                    continue
                
                a_match = addr_pat.match(line)
                if a_match and current_symbol:
                    addr = int(a_match.group(1), 16)
                    symbols.append((addr, current_symbol))
                    current_symbol = None
                    continue
                
                c_match = classic_pat.match(line)
                if c_match:
                    addr = int(c_match.group(1), 16)
                    name = c_match.group(2)
                    symbols.append((addr, name))
                    
    except FileNotFoundError:
        print(f"Error: Map file not found at '{map_path}'")
        return None
        
    symbols.sort(key=lambda x: x[0])
    return symbols

def find_symbol(symbols, addr):
    if not symbols:
        return "Unknown"
    
    best_symbol = "Unknown"
    best_addr = 0
    
    for s_addr, s_name in symbols:
        if s_addr <= addr:
            best_symbol = s_name
            best_addr = s_addr
        else:
            break
            
    if best_addr == 0:
        return "Unknown"
        
    offset = addr - best_addr
    return f"{best_symbol} + 0x{offset:X}"

def resolve_memory_report(log_path, map_path):
    print("=" * 72)
    print("               MEMORY LEAK MAP RESOLVER")
    print("=" * 72)
    
    if not os.path.exists(log_path):
        print(f"Error: Memory log not found at '{log_path}'")
        return
        
    symbols = parse_map_file(map_path)
    if not symbols:
        return

    # Extract all hex addresses from the memory log
    with open(log_path, 'r', encoding='utf-8', errors='ignore') as f:
        lines = f.readlines()

    leak_entries = []
    current_leak = None

    # Parse leak lines: "[#001 | ID:12] 1024 bytes" or "[LEAK #001] 1024 bytes"
    header_pat = re.compile(r"\[(?:#(\d+)\s*\|\s*ID:(\d+)|LEAK\s*#(\d+))\]\s+(\d+)\s+bytes")
    # Parse frame address lines: "    [00] 0x00007FF7B1243A10"
    frame_pat  = re.compile(r"^\s+\[(\d+)\]\s+(0x[0-9a-fA-F]+)")

    for line in lines:
        h_match = header_pat.search(line)
        if h_match:
            idx = h_match.group(1) or h_match.group(3)
            alloc_id = h_match.group(2) or "0"
            size = h_match.group(4)
            current_leak = {
                "index": int(idx),
                "alloc_id": int(alloc_id),
                "size": int(size),
                "frames": []
            }
            leak_entries.append(current_leak)
            continue

        f_match = frame_pat.search(line)
        if f_match and current_leak is not None:
            frame_idx = int(f_match.group(1))
            addr = int(f_match.group(2), 16)
            current_leak["frames"].append((frame_idx, addr))

    if not leak_entries:
        print(f"No unresolved memory leaks found in '{log_path}'.")
        return

    # Calculate ASLR base offset using lowest map address vs lowest frame address
    # Find minimum text section base in map
    min_pref_addr = min(addr for addr, _ in symbols if addr > 0x1000)
    
    # Collect all frame addresses across leaks
    all_addrs = [addr for leak in leak_entries for _, addr in leak["frames"] if addr > 0]
    if not all_addrs:
        print("No stack addresses found to resolve.")
        return

    # Align to 64KB boundary to determine module load base
    min_actual_addr = min(all_addrs)
    aslr_offset = (min_actual_addr & ~0xFFFF) - (min_pref_addr & ~0xFFFF)

    print(f"Map File       : {map_path}")
    print(f"Log File       : {log_path}")
    print(f"Active Leaks   : {len(leak_entries)}")
    print(f"ASLR Offset    : 0x{aslr_offset:X}")
    print("-" * 72)

    total_bytes = 0
    for leak in leak_entries:
        total_bytes += leak["size"]
        print(f"\n[LEAK #{leak['index']:03d} | ID:{leak['alloc_id']}] {leak['size']} bytes:")

        for f_idx, addr in leak["frames"]:
            pref_addr = addr - aslr_offset
            sym = find_symbol(symbols, pref_addr)
            print(f"    [{f_idx:02d}] 0x{addr:016X} -> {sym}")

    print("\n" + "=" * 72)
    print(f" TOTAL RESOLVED LEAKS: {total_bytes} bytes across {len(leak_entries)} allocation(s)")
    print("=" * 72)

if __name__ == "__main__":
    log_file = "memory_live.txt"
    map_file = "build/terminal-retro.map"
    
    if len(sys.argv) > 1:
        log_file = sys.argv[1]
    if len(sys.argv) > 2:
        map_file = sys.argv[2]
        
    resolve_memory_report(log_file, map_file)