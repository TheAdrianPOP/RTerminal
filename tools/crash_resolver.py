import re
import sys
import os

# Anchor symbol used to calculate ASLR offset
# This must match the name in the .map file (usually starts with _ZL for statics or just name)
ANCHOR_SYMBOL_PATTERN = r"engine_exception_filter"

def parse_map_file(map_path):
    symbols = []
    current_symbol = None
    
    # Matches: .text$_ZN... or .text$main
    symbol_pat = re.compile(r"^\s+\.text\$([^\s]+)")
    # Matches: 0x0041329c      0x1c4 build\...
    addr_pat = re.compile(r"^\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+|[0-9a-fA-F]+)\s+build")
    # Matches classic symbols: 0x00428ee0                runtime_init
    classic_pat = re.compile(r"^\s+(0x[0-9a-fA-F]+)\s+([^\s]+)$")

    try:
        with open(map_path, 'r') as f:
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
        print(f"Error: Map file not found at {map_path}")
        return None
        
    symbols.sort(key=lambda x: x[0])
    return symbols

def find_symbol(symbols, addr):
    if not symbols:
        return "Unknown"
    
    # Simple linear search for the closest symbol <= addr
    best_symbol = "Before first symbol"
    best_addr = 0
    
    for s_addr, s_name in symbols:
        if s_addr <= addr:
            best_symbol = s_name
            best_addr = s_addr
        else:
            break
            
    offset = addr - best_addr
    return f"{best_symbol} + 0x{offset:X}"

def resolve_crash(log_path, map_path):
    print(f"--- RTerminal Crash Resolver ---")
    
    if not os.path.exists(log_path):
        print(f"Error: Crash log not found: {log_path}")
        return
        
    symbols = parse_map_file(map_path)
    if not symbols:
        return

    # Find the preferred address of our anchor
    anchor_preferred = None
    for addr, name in symbols:
        if ANCHOR_SYMBOL_PATTERN in name:
            anchor_preferred = addr
            print(f"Found anchor symbol: {name} at preferred address 0x{addr:08X}")
            break
            
    if anchor_preferred is None:
        print(f"Error: Could not find anchor symbol '{ANCHOR_SYMBOL_PATTERN}' in map file.")
        return

    # Parse crash log
    crash_addr = None
    anchor_actual = None
    stack_frames = []
    
    with open(log_path, 'r') as f:
        content = f.read()
        
        # Extract crash address: "at 0x00B07EC8"
        crash_match = re.search(r"at (0x[0-9a-fA-F]+)", content)
        if crash_match:
            crash_addr = int(crash_match.group(1), 16)
            
        # Extract stack frames: "[0] 0x00AF32D5"
        frames = re.findall(r"\[(\d+)\] (0x[0-9a-fA-F]+)", content)
        for i, addr_str in frames:
            addr = int(addr_str, 16)
            stack_frames.append(addr)
            if i == '0':
                anchor_actual = addr

    if not crash_addr or not anchor_actual:
        print("Error: Could not parse crash address or stack frame 0 from log.")
        return

    # Calculate ASLR Offset
    # Note: Frame [0] is the caller of panic_abort, which is our exception filter.
    aslr_offset = anchor_actual - anchor_preferred
    print(f"Detected ASLR Offset: 0x{aslr_offset:X}")
    print(f"Actual Load Address: 0x{anchor_actual:08X}")
    print(f"Preferred Address:   0x{anchor_preferred:08X}")
    print("-" * 40)

    # Resolve Crash Site
    pref_crash = crash_addr - aslr_offset
    print(f"CRASH SITE: 0x{crash_addr:08X} -> 0x{pref_crash:08X}")
    print(f"Symbol: {find_symbol(symbols, pref_crash)}")
    print("-" * 40)
    
    # Resolve Stack Trace
    print("RESOLVED STACK TRACE:")
    for i, addr in enumerate(stack_frames):
        pref_addr = addr - aslr_offset
        symbol = find_symbol(symbols, pref_addr)
        print(f"  [{i}] 0x{addr:08X} -> 0x{pref_addr:08X} : {symbol}")

if __name__ == "__main__":
    log = "crash_log.txt"
    mapping = "RTerminal.map"
    
    if len(sys.argv) > 1: log = sys.argv[1]
    if len(sys.argv) > 2: mapping = sys.argv[2]
    
    resolve_crash(log, mapping)
