import re
import os
import sys
from collections import defaultdict

class MapAnalyzer:
    def __init__(self, map_path):
        self.map_path = map_path
        self.sections = []
        self.discarded = []
        
    def parse(self):
        if not os.path.exists(self.map_path):
            print(f"Error: Map file not found at {self.map_path}")
            return False

        try:
            with open(self.map_path, 'r', encoding='utf-8', errors='ignore') as f:
                lines = f.readlines()
        except Exception as e:
            print(f"Error reading map file: {e}")
            return False

        current_block = None
        i = 0
        while i < len(lines):
            line = lines[i].rstrip()
            if not line:
                i += 1
                continue
            
            if "Discarded input sections" in line:
                current_block = "discarded"
                i += 1
                continue
            
            # Heuristic to find the start of section allocations
            if not current_block and line.startswith(" ."):
                current_block = "kept"

            # Case 1: Combined line (Section + Metric + Origin)
            match = re.match(r"^\s*(\.\S+)\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(.*)$", line, re.IGNORECASE)
            if match:
                entry = self._create_entry(match.group(1), match.group(2), match.group(3), match.group(4))
                # Look ahead for a symbol name (often on the next line at the same address)
                if i + 1 < len(lines):
                    sym_match = re.match(r"^\s*(0x[0-9a-f]+)\s+([^\s=]+)$", lines[i+1], re.IGNORECASE)
                    if sym_match and int(sym_match.group(1), 16) == entry["addr"]:
                        entry["symbol"] = sym_match.group(2)

                if entry["addr"] >= 0x400000: current_block = "kept"
                if current_block == "kept": self.sections.append(entry)
                elif current_block == "discarded": self.discarded.append(entry)
                i += 1
                continue

            # Case 2: Split line start
            match_start = re.match(r"^\s*(\.\S+)$", line)
            if match_start and i + 1 < len(lines):
                match_end = re.match(r"^\s*(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(.*)$", lines[i+1], re.IGNORECASE)
                if match_end:
                    entry = self._create_entry(match_start.group(1), match_end.group(1), match_end.group(2), match_end.group(3))
                    # Look ahead for symbol
                    if i + 2 < len(lines):
                        sym_match = re.match(r"^\s*(0x[0-9a-f]+)\s+([^\s=]+)$", lines[i+2], re.IGNORECASE)
                        if sym_match and int(sym_match.group(1), 16) == entry["addr"]:
                            entry["symbol"] = sym_match.group(2)
                    
                    if entry["addr"] >= 0x400000: current_block = "kept"
                    if current_block == "kept": self.sections.append(entry)
                    elif current_block == "discarded": self.discarded.append(entry)
                    i += 2
                    continue

            i += 1
        return True

    def _create_entry(self, section, addr, size, origin):
        return {
            "section": section,
            "addr": int(addr, 16),
            "size": int(size, 16),
            "origin": origin.strip(),
            "symbol": None
        }

    def print_report(self, f=sys.stdout):
        file_stats = defaultdict(lambda: {"code": 0, "data": 0, "total": 0})
        component_stats = defaultdict(lambda: {"code": 0, "data": 0, "total": 0})
        total_binary_size = 0
        section_groups = defaultdict(int)
        
        for s in self.sections:
            root_sec = s["section"].split('$')[0]
            if root_sec.startswith(".text"): root_sec = ".text"
            if root_sec.startswith(".data"): root_sec = ".data"
            if root_sec.startswith(".rdata"): root_sec = ".rdata"
            section_groups[root_sec] += s["size"]

            origin = s["origin"]
            origin = re.sub(r"^build\\.objs\\RTerminal\\windows\\x86\\release\\", "", origin)
            if "(" in origin and ")" in origin:
                parts = origin.split("(")
                lib_name = os.path.basename(parts[0])
                obj_name = parts[1].replace(")", "")
                origin = f"{lib_name} [{obj_name}]"

            size = s["size"]
            sec_low = s["section"].lower()
            is_code = ".text" in sec_low
            
            file_stats[origin]["code" if is_code else "data"] += size
            file_stats[origin]["total"] += size
            
            comp = "Other / Libraries"
            if "src\\engine" in origin or "src/engine" in origin:
                comp = "Engine"
            elif "kinetype_game" in origin or "main.cpp" in origin:
                comp = "Game"
            elif "libmsvcrt" in origin or "libmingw" in origin or "crt2.o" in origin:
                comp = "CRT / Runtime"
            elif ".rsrc" in sec_low:
                comp = "Resources (.rsrc)"
            elif ".idata" in sec_low:
                comp = "Imports (IAT)"
            elif ".reloc" in sec_low:
                comp = "Relocations (.reloc)"
            elif ".debug" in sec_low:
                comp = "Debug Info"
            elif ".tls" in sec_low:
                comp = "Thread Local Storage"
            
            component_stats[comp]["code" if is_code else "data"] += size
            component_stats[comp]["total"] += size
            total_binary_size += size

        if total_binary_size == 0:
            print("Error: Total binary size is 0. Check parser logic.")
            return

        print("\n" + "="*100, file=f)
        print(f"{'RTERMINAL COMPLETE BINARY BREAKDOWN':^100}", file=f)
        print("="*100, file=f)
        
        print(f"\n[ COMPONENT SUMMARY ]", file=f)
        print("-" * 100, file=f)
        print(f"{'Component':<30} | {'Code (Text)':>12} | {'Data/Other':>12} | {'Total':>12} | {'%':>6}", file=f)
        print("-" * 100, file=f)
        for comp, stats in sorted(component_stats.items(), key=lambda x: x[1]["total"], reverse=True):
            perc = (stats["total"] / total_binary_size) * 100
            print(f"{comp:<30} | {stats['code']:>10,} B | {stats['data']:>10,} B | {stats['total']:>10,} B | {perc:>5.1f}%", file=f)
        print("-" * 100, file=f)
        print(f"{'TOTAL UNCOMPRESSED':<30} | {'':>12} | {'':>12} | {total_binary_size:>10,} B | 100.0%", file=f)
        print(f"{'':<30} | {'':>12} | {'':>12} | {total_binary_size/1024:>10.2f} KB |", file=f)
        print("="*100, file=f)

        print(f"\n[ SECTION TYPE BREAKDOWN ]", file=f)
        print("-" * 100, file=f)
        print(f"{'Section Root':<30} | {'Total Size':>12} | {'%':>6}", file=f)
        print("-" * 100, file=f)
        for sec, size in sorted(section_groups.items(), key=lambda x: x[1], reverse=True):
            perc = (size / total_binary_size) * 100
            print(f"{sec:<30} | {size:>10,} B | {perc:>5.1f}%", file=f)
        print("="*100, file=f)

        print(f"\n[ ALL CONTRIBUTORS (BY FILE) ]", file=f)
        print("-" * 100, file=f)
        print(f"{'Origin File / Object':<60} | {'Total Size':>12} | {'%':>6}", file=f)
        print("-" * 100, file=f)
        sorted_files = sorted(file_stats.items(), key=lambda x: x[1]["total"], reverse=True)
        for origin, stats in sorted_files:
            if stats["total"] == 0: continue
            perc = (stats["total"] / total_binary_size) * 100
            print(f"{origin[:60]:<60} | {stats['total']:>10,} B | {perc:>5.1f}%", file=f)
        print("="*100, file=f)

        print(f"\n[ ALL SYMBOLS / SECTIONS (BLOAT) ]", file=f)
        print("-" * 100, file=f)
        print(f"{'Size':>12} | {'Section Type':<20} | {'Origin / Symbol':<60}", file=f)
        print("-" * 100, file=f)
        sorted_symbols = sorted(self.sections, key=lambda x: x["size"], reverse=True)
        for s in sorted_symbols:
            sec = s["section"][:18]
            name = s["symbol"] if s["symbol"] else s["section"]
            
            # Clean up name
            if name.startswith(".text$_"): name = name[7:]
            if name.startswith(".data$_"): name = name[7:]
            if name.startswith(".rdata$_"): name = name[8:]
            
            if len(name) < 5 or name.startswith("0x"): # Fallback to filename
                name = os.path.basename(s["origin"])
            
            if len(name) > 60: name = name[:57] + "..."
            print(f"{s['size']:>10,} B | {sec:<20} | {name}", file=f)
        print("-" * 100, file=f)

        # Global Statistics
        print(f"\n[ GLOBAL STATISTICS ]", file=f)
        print("-" * 100, file=f)
        avg_size = total_binary_size / len(self.sections) if self.sections else 0
        print(f"{'Total Unique Symbols:':<30} {len(self.sections):>10,}", file=f)
        print(f"{'Total Contributing Files:':<30} {len(file_stats):>10,}", file=f)
        print(f"{'Average Symbol Size:':<30} {avg_size:>10.1f} B", file=f)
        
        # Highlights
        print(f"\n[ SECTION HIGHLIGHTS ]", file=f)
        print("-" * 100, file=f)
        
        def find_max(filter_fn):
            filtered = [s for s in self.sections if filter_fn(s["section"].lower())]
            return max(filtered, key=lambda x: x["size"]) if filtered else None

        biggest_code = find_max(lambda x: ".text" in x)
        biggest_data = find_max(lambda x: ".data" in x and ".rdata" not in x)
        biggest_ro   = find_max(lambda x: ".rdata" in x)

        if biggest_code:
            name = biggest_code["symbol"] or biggest_code["section"]
            print(f"{'Largest Code Symbol:':<30} {biggest_code['size']:>10,} B | {name}", file=f)
        if biggest_data:
            name = biggest_data["symbol"] or biggest_data["section"]
            print(f"{'Largest Data Symbol:':<30} {biggest_data['size']:>10,} B | {name}", file=f)
        if biggest_ro:
            name = biggest_ro["symbol"] or biggest_ro["section"]
            print(f"{'Largest RO Data Symbol:':<30} {biggest_ro['size']:>10,} B | {name}", file=f)
        print("-" * 100, file=f)

if __name__ == "__main__":
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    default_path = os.path.join(project_root, "build", "RTerminal.map")
    
    path = sys.argv[1] if len(sys.argv) > 1 else default_path
    analyzer = MapAnalyzer(path)
    if analyzer.parse():
        report_path = os.path.join(os.path.dirname(path), "binary_report.txt")
        with open(report_path, "w", encoding="utf-8") as f:
            analyzer.print_report(f)
        print(f"[+] Full binary report saved to: {report_path}")
