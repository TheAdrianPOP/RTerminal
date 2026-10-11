import re
import os
import sys
import csv
import subprocess
from collections import defaultdict

class DirNode:
    def __init__(self, name):
        self.name = name
        self.rom = 0
        self.ram = 0
        self.children = {}

class MapAnalyzer:
    def __init__(self, map_path):
        self.map_path = map_path
        self.sections = []
        self.discarded = []
        self.demangle_cache = {}

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
            
            if not current_block and line.startswith(" ."):
                current_block = "kept"

            match = re.match(r"^\s*(\.\S+)\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(.*)$", line, re.IGNORECASE)
            if match:
                entry = self._create_entry(match.group(1), match.group(2), match.group(3), match.group(4))
                if i + 1 < len(lines):
                    sym_match = re.match(r"^\s*(0x[0-9a-f]+)\s+([^\s=]+)$", lines[i+1], re.IGNORECASE)
                    if sym_match and int(sym_match.group(1), 16) == entry["addr"]:
                        entry["symbol"] = sym_match.group(2)

                if entry["addr"] >= 0x400000: 
                    current_block = "kept"
                
                if current_block == "kept": 
                    self.sections.append(entry)
                elif current_block == "discarded": 
                    self.discarded.append(entry)
                i += 1
                continue

            match_start = re.match(r"^\s*(\.\S+)$", line)
            if match_start and i + 1 < len(lines):
                match_end = re.match(r"^\s*(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(.*)$", lines[i+1], re.IGNORECASE)
                if match_end:
                    entry = self._create_entry(match_start.group(1), match_end.group(1), match_end.group(2), match_end.group(3))
                    if i + 2 < len(lines):
                        sym_match = re.match(r"^\s*(0x[0-9a-f]+)\s+([^\s=]+)$", lines[i+2], re.IGNORECASE)
                        if sym_match and int(sym_match.group(1), 16) == entry["addr"]:
                            entry["symbol"] = sym_match.group(2)
                    
                    if entry["addr"] >= 0x400000: 
                        current_block = "kept"
                        
                    if current_block == "kept": 
                        self.sections.append(entry)
                    elif current_block == "discarded": 
                        self.discarded.append(entry)
                    i += 2
                    continue

            i += 1
        
        self._run_batch_demangle()
        return True

    def _create_entry(self, section, addr, size, origin):
        return {
            "section": section,
            "addr": int(addr, 16),
            "size": int(size, 16),
            "origin": origin.strip(),
            "symbol": None
        }

    def _fallback_demangle(self, mangled):
        match = re.match(r"^_ZN(\d+)([a-zA-Z0-9_]+)(\d+)([a-zA-Z0-9_]+)E.*", mangled)
        if match:
            class_len = int(match.group(1))
            class_name = match.group(2)[:class_len]
            method_name = match.group(4)
            return f"{class_name}::{method_name}"
        return mangled

    def _run_batch_demangle(self):
        mangled_symbols = set()
        for s in self.sections + self.discarded:
            if s["symbol"] and s["symbol"].startswith("_Z"):
                mangled_symbols.add(s["symbol"])
        
        if not mangled_symbols:
            return

        try:
            proc = subprocess.Popen(
                ["c++filt"],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True
            )
            stdout, _ = proc.communicate("\n".join(mangled_symbols))
            demangled_map = dict(zip(mangled_symbols, stdout.splitlines()))
            self.demangle_cache.update(demangled_map)
        except Exception:
            for sym in mangled_symbols:
                self.demangle_cache[sym] = self._fallback_demangle(sym)

    def _get_clean_name(self, s):
        name = s["symbol"] if s["symbol"] else s["section"]
        if name in self.demangle_cache:
            name = self.demangle_cache[name]
        
        if name.startswith(".text$_"): name = name[7:]
        elif name.startswith(".data$_"): name = name[7:]
        elif name.startswith(".rdata$_"): name = name[8:]
        elif name.startswith(".bss$_"): name = name[6:]
        
        if len(name) < 5 or name.startswith("0x"):
            name = os.path.basename(s["origin"])
        return name

    def _extract_scope(self, demangled):
        if "::" not in demangled:
            return "Global Scope / Unscoped"
        # Parse scope while safely ignoring templated brackets
        base = demangled.split('(')[0]
        parts = []
        current = []
        depth = 0
        i = 0
        while i < len(base):
            c = base[i]
            if c == '<':
                depth += 1
                current.append(c)
            elif c == '>':
                depth -= 1
                current.append(c)
            elif c == ':' and depth == 0:
                if i + 1 < len(base) and base[i+1] == ':':
                    parts.append("".join(current))
                    current = []
                    i += 2
                    continue
                current.append(c)
            else:
                current.append(c)
            i += 1
        
        if parts:
            return "::".join(parts)
        return "Global Scope / Unscoped"

    def _is_ram_only(self, section_name):
        sec_low = section_name.lower()
        return ".bss" in sec_low or ".tbss" in sec_low

    def _collapse_template(self, demangled):
        if '<' not in demangled or '>' not in demangled:
            return None
        result = []
        depth = 0
        for char in demangled:
            if char == '<':
                if depth == 0:
                    result.append('<...>')
                depth += 1
            elif char == '>':
                depth -= 1
                if depth < 0: depth = 0
            else:
                if depth == 0:
                    result.append(char)
        return "".join(result)

    def print_report(self, f=sys.stdout):
        total_rom_size = 0
        total_ram_size = 0
        
        file_stats = defaultdict(lambda: {"rom": 0, "ram": 0, "total": 0})
        component_stats = defaultdict(lambda: {"rom": 0, "ram": 0, "total": 0})
        section_groups = defaultdict(lambda: {"rom": 0, "ram": 0})
        
        dir_tree_root = DirNode("Project Root")
        
        # Scope and File tracking mappings
        scope_sizes = defaultdict(int)
        file_symbols = defaultdict(list)
        
        # C++ Infrastructure categories
        exception_overhead = 0
        vtable_overhead = 0
        rtti_overhead = 0
        static_init_overhead = 0

        section_ranges = defaultdict(lambda: {"start": 0xFFFFFFFFFFFFFFFF, "end": 0, "size": 0})
        
        size_bins = {
            "Tiny [1 - 32 B] (Inlines / Getters)": 0,
            "Small [33 - 128 B] (Short Helpers)": 0,
            "Medium [129 - 512 B] (Standard Logic)": 0,
            "Large [513 - 2 KB] (Complex Functions)": 0,
            "Heavy [2 KB - 8 KB] (Intense Routines)": 0,
            "Monolithic [8 KB+] (Large Modules / Static Data)": 0
        }

        template_groups = defaultdict(lambda: {"size": 0, "count": 0, "signature": ""})

        # Process segments
        for s in self.sections:
            sec_name = s["section"]
            root_sec = sec_name.split('$')[0]
            if root_sec.startswith(".text"): root_sec = ".text"
            elif root_sec.startswith(".data"): root_sec = ".data"
            elif root_sec.startswith(".rdata"): root_sec = ".rdata"
            elif root_sec.startswith(".bss"): root_sec = ".bss"

            size = s["size"]
            addr = s["addr"]
            ram_only = self._is_ram_only(sec_name)
            
            if size > 0:
                section_ranges[root_sec]["start"] = min(section_ranges[root_sec]["start"], addr)
                section_ranges[root_sec]["end"] = max(section_ranges[root_sec]["end"], addr + size)
                section_ranges[root_sec]["size"] += size

                if size <= 32: size_bins["Tiny [1 - 32 B] (Inlines / Getters)"] += 1
                elif size <= 128: size_bins["Small [33 - 128 B] (Short Helpers)"] += 1
                elif size <= 512: size_bins["Medium [129 - 512 B] (Standard Logic)"] += 1
                elif size <= 2048: size_bins["Large [513 - 2 KB] (Complex Functions)"] += 1
                elif size <= 8192: size_bins["Heavy [2 KB - 8 KB] (Intense Routines)"] += 1
                else: size_bins["Monolithic [8 KB+] (Large Modules / Static Data)"] += 1

            if ram_only:
                section_groups[root_sec]["ram"] += size
                total_ram_size += size
            else:
                section_groups[root_sec]["rom"] += size
                total_rom_size += size

            origin = s["origin"]
            origin = re.sub(r"^build\\.objs\\RTerminal\\windows\\x86\\release\\", "", origin)
            
            clean_origin = origin.replace("\\", "/")
            clean_origin = re.sub(r"^[a-zA-Z]:/", "", clean_origin)
            clean_origin = clean_origin.replace("(", "/").replace(")", "")
            if clean_origin.startswith("./"): clean_origin = clean_origin[2:]
            
            path_parts = [p for p in clean_origin.split("/") if p]
            self._add_to_directory_tree(dir_tree_root, path_parts, 0 if ram_only else size, size if ram_only else 0)

            if "(" in origin and ")" in origin:
                parts = origin.split("(")
                lib_name = os.path.basename(parts[0])
                obj_name = parts[1].replace(")", "")
                origin = f"{lib_name} [{obj_name}]"

            if ram_only:
                file_stats[origin]["ram"] += size
            else:
                file_stats[origin]["rom"] += size
            file_stats[origin]["total"] += size
            
            comp = "Other / Libraries"
            if "src\\engine" in origin or "src/engine" in origin or "audio.cpp" in origin or "video.cpp" in origin or "frontend.cpp" in origin:
                comp = "Engine"
            elif "main.cpp" in origin or "interface.cpp" in origin or "input.cpp" in origin:
                comp = "Frontend & Game UI"
            elif "libmsvcrt" in origin or "libmingw" in origin or "crt2.o" in origin:
                comp = "CRT / System Runtime"
            elif ".rsrc" in sec_name.lower(): comp = "Resources (.rsrc)"
            elif ".idata" in sec_name.lower(): comp = "Imports (IAT)"
            elif ".reloc" in sec_name.lower(): comp = "Relocations (.reloc)"
            elif ".debug" in sec_name.lower(): comp = "Debug Info"
            elif ".tls" in sec_name.lower(): comp = "Thread Local Storage"
            
            if ram_only:
                component_stats[comp]["ram"] += size
            else:
                component_stats[comp]["rom"] += size
            component_stats[comp]["total"] += size

            # Retain detailed symbol lists mapped back to source files
            file_symbols[origin].append(s)

            clean_sym = self._get_clean_name(s)
            
            # Attribute scope grouping sizes
            scope = self._extract_scope(clean_sym)
            if size > 0 and not ram_only:
                scope_sizes[scope] += size

            sec_low = sec_name.lower()
            if ".eh_frame" in sec_low or ".gcc_except_table" in sec_low:
                exception_overhead += size
            elif "vtable for " in clean_sym or ".rdata$_ZTV" in sec_low:
                vtable_overhead += size
            elif "typeinfo for " in clean_sym or "typeinfo name for " in clean_sym or ".rdata$_ZTI" in sec_low or ".rdata$_ZTS" in sec_low:
                rtti_overhead += size
            elif ".ctors" in sec_low or ".dtors" in sec_low or ".init_array" in sec_low or "static_initialization" in clean_sym:
                static_init_overhead += size

            collapsed = self._collapse_template(clean_sym)
            if collapsed:
                template_groups[collapsed]["size"] += size
                template_groups[collapsed]["count"] += 1
                template_groups[collapsed]["signature"] = collapsed

        total_binary_size = total_rom_size + total_ram_size
        if total_binary_size == 0:
            print("Error: Total binary size returned 0. Check analyzer configuration.")
            return

        print("\n" + "="*120, file=f)
        print(f"{'RTERMINAL IN-DEPTH OPTIMIZATION COMPANION REPORT':^120}", file=f)
        print("="*120, file=f)
        
        # High-level summary
        print(f"\n[ MEMORY MODEL FOOTPRINT ]", file=f)
        print("-" * 120, file=f)
        print(f"  On-Disk (ROM) Executable Size: {total_rom_size:>10,} B ({total_rom_size/1024:>8.2f} KB)", file=f)
        print(f"  Runtime (RAM) Uninitialized:   {total_ram_size:>10,} B ({total_ram_size/1024:>8.2f} KB)  <-- (.bss sections)", file=f)
        print(f"  Total Virtual Memory Offset:   {total_rom_size+total_ram_size:>10,} B ({(total_rom_size+total_ram_size)/1024:>8.2f} KB)", file=f)
        print("-" * 120, file=f)

        # Dynamic alerts scanner
        self._print_remediation_action_plan(f, template_groups)

        # Contiguous Virtual Memory Sections Mapping
        self._print_memory_section_ranges(f, section_ranges)

        # Class Scope Size Summary
        self._print_class_scope_breakdown(f, scope_sizes)

        # Directory / Folder Topology Tree
        print(f"\n[ PROJECT SOURCE TOPOLOGY TREE BREAKDOWN (ROM & RAM BY FOLDER) ]", file=f)
        print("-" * 120, file=f)
        print(f"  {'Folder Path / Object File':<64} | {'ROM size':>15} | {'RAM size':>15} | {'% of ROM':>13}", file=f)
        print("-" * 110, file=f)
        for comp, stats in sorted(component_stats.items(), key=lambda x: x[1]["total"], reverse=True):
            perc = (stats["total"] / total_binary_size) * 100
        # Call print of hierarchy
        archive_stats = defaultdict(int)
        for origin, stats in file_stats.items():
            lib_part = origin.split(" ")[0] if " [" in origin else origin
            archive_stats[lib_part] += stats["rom"]
        for lib, rom_size in sorted(archive_stats.items(), key=lambda x: x[1], reverse=True):
            if rom_size == 0: continue
            perc = (rom_size / total_rom_size) * 100
        self._print_directory_tree_node(dir_tree_root, "", f, total_rom_size)
        print("="*120, file=f)

        # Grouped Symbol Breakdown per Source File
        self._print_nested_source_file_breakdown(f, file_stats, file_symbols, total_rom_size)

        # C++ Runtime Infrastructure Metrics
        self._print_cpp_runtime_overhead(f, exception_overhead, vtable_overhead, rtti_overhead, static_init_overhead, total_rom_size)

        # Template Instantiation report
        self._print_template_instantiation_report(f, template_groups)

        # Histogram size profile
        self._print_symbol_size_distribution(f, size_bins, len(self.sections))

        # Dead-code elimination summary
        self._print_discarded_sections_report(f)

        # Overall Stats
        print(f"\n[ GLOBAL ANALYSIS STATISTICS ]", file=f)
        print("-" * 120, file=f)
        avg_size = total_rom_size / len(self.sections) if self.sections else 0
        print(f"  Total Retained Symbols:        {len(self.sections):>10,}", file=f)
        print(f"  Total Discarded Symbols:       {len(self.discarded):>10,}", file=f)
        print(f"  Total Contributing Files:      {len(file_stats):>10,}", file=f)
        print(f"  Average Symbol ROM Impact:     {avg_size:>10.1f} B", file=f)
        print("="*120, file=f)

    def _add_to_directory_tree(self, root, parts, rom, ram):
        curr = root
        curr.rom += rom
        curr.ram += ram
        for part in parts:
            if part not in curr.children:
                curr.children[part] = DirNode(part)
            curr = curr.children[part]
            curr.rom += rom
            curr.ram += ram

    def _print_directory_tree_node(self, node, indent, f, total_rom):
        if not node.children:
            return
        sorted_children = sorted(node.children.values(), key=lambda x: x.rom + x.ram, reverse=True)
        for idx, child in enumerate(sorted_children):
            perc = (child.rom / total_rom) * 100 if total_rom > 0 else 0
            is_last = (idx == len(sorted_children) - 1)
            connector = "└── " if is_last else "├── "
            
            line_name = f"{indent}{connector}{child.name}"
            if len(line_name) > 64: line_name = line_name[:61] + "..."
            
            print(f"  {line_name:<64} | {child.rom:>12,} B | {child.ram:>12,} B | {perc:>12.1f}%", file=f)
            
            new_indent = indent + ("    " if is_last else "│   ")
            self._print_directory_tree_node(child, new_indent, f, total_rom)

    def _print_memory_section_ranges(self, f, section_ranges):
        print(f"\n[ CONTIGUOUS VIRTUAL MEMORY ADDRESS LAYOUT ]", file=f)
        print("-" * 120, file=f)
        print(f"  {'Section Segment':<20} | {'Start Address':<18} | {'End Address':<18} | {'Total Section Size':>20}", file=f)
        print("-" * 120, file=f)
        
        sorted_ranges = sorted(section_ranges.items(), key=lambda x: x[1]["start"])
        for sec, ranges in sorted_ranges:
            if ranges["start"] == 0xFFFFFFFFFFFFFFFF: continue
            print(f"  {sec:<20} | 0x{ranges['start']:016x} | 0x{ranges['end']:016x} | {ranges['size']:>18,} B", file=f)
        print("-" * 120, file=f)

    def _print_class_scope_breakdown(self, f, scope_sizes):
        print(f"\n[ C++ CLASS & SCOPE ATTRIBUTION (ROM USAGE BY CLASS) ]", file=f)
        print("-" * 120, file=f)
        print(f"  {'Scope Namespace / Class Name':<65} | {'ROM Usage':>18} | {'% of ROM Code':>15}", file=f)
        print("-" * 120, file=f)
        
        total_scopes_size = sum(scope_sizes.values())
        sorted_scopes = sorted(scope_sizes.items(), key=lambda x: x[1], reverse=True)
        for scope, sz in sorted_scopes[:20]:
            perc = (sz / total_scopes_size) * 100 if total_scopes_size > 0 else 0
            clean_scope = scope
            if len(clean_scope) > 65: clean_scope = clean_scope[:62] + "..."
            print(f"  {clean_scope:<65} | {sz:>15,} B | {perc:>13.1f}%", file=f)
        print("-" * 120, file=f)

    def _print_nested_source_file_breakdown(self, f, file_stats, file_symbols, total_rom):
        print(f"\n[ TOP TRANSLATION UNITS & TOP EMBEDDED SYMBOLS ]", file=f)
        print("-" * 120, file=f)
        print(f"  Lists modules by compile footprint along with the largest internal routines inside each file.", file=f)
        print("-" * 120, file=f)
        
        sorted_files = sorted(file_stats.items(), key=lambda x: x[1]["rom"], reverse=True)
        for origin, stats in sorted_files[:15]:
            if stats["rom"] == 0: continue
            file_perc = (stats["rom"] / total_rom) * 100 if total_rom > 0 else 0
            print(f"\n  ■ {origin[:80]}  (Total ROM: {stats['rom']:,} B | {file_perc:.2f}%)", file=f)
            
            # Sort and list top 5 largest compiled symbols inside this file
            symbols_in_file = file_symbols[origin]
            sorted_syms = sorted(symbols_in_file, key=lambda x: x["size"], reverse=True)
            for s in sorted_syms[:5]:
                if s["size"] == 0: continue
                s_name = self._get_clean_name(s)
                if len(s_name) > 75: s_name = s_name[:72] + "..."
                print(f"    ├── {s_name:<75} | {s['size']:>10,} B", file=f)
        print("\n" + "="*120, file=f)

    def _print_remediation_action_plan(self, f, template_groups):
        print(f"\n[ AUTOMATED CODE REFACTORING ACTION PLAN ]", file=f)
        print("-" * 120, file=f)
        
        alerts = []
        
        # 1. Look for monolithic code routines
        for s in self.sections:
            if ".text" in s["section"].lower() and s["size"] >= 2048:
                alerts.append({
                    "type": "Monolithic Code Routine Size Alert",
                    "file": os.path.basename(s["origin"]),
                    "symbol": self._get_clean_name(s),
                    "size": s["size"],
                    "advice": "Splitting this monolithic function into smaller helpers allows compilers to optimize register usage and inline logical sub-components."
                })
        
        # 2. Look for large initialized static structures
        for s in self.sections:
            if not self._is_ram_only(s["section"]) and (".data" in s["section"].lower() or ".rdata" in s["section"].lower()):
                if s["size"] >= 1024:
                    alerts.append({
                        "type": "Global Static Variable Footprint Alert",
                        "file": os.path.basename(s["origin"]),
                        "symbol": self._get_clean_name(s),
                        "size": s["size"],
                        "advice": "Consider changing this initialized array to dynamic runtime allocation (e.g. heap arrays or unique_ptr) or loading from an external asset stream."
                    })
                    
        # 3. Look for bloated uninitialized RAM allocations
        for s in self.sections:
            if self._is_ram_only(s["section"]) and s["size"] >= 4096:
                alerts.append({
                    "type": "RAM Buffer Segment Allocation Warning",
                    "file": os.path.basename(s["origin"]),
                    "symbol": self._get_clean_name(s),
                    "size": s["size"],
                    "advice": "This buffer consumes critical RAM space on launch. Transitioning to dynamic heap-allocated std::vector or malloc releases startup RAM memory."
                })

        # 4. Look for template classes instantiated heavily
        for t_sig, t_stats in template_groups.items():
            if t_stats["count"] >= 4:
                alerts.append({
                    "type": "Template Instantiation Proliferation Warn",
                    "file": "Multiple Files",
                    "symbol": t_sig,
                    "size": t_stats["size"],
                    "advice": f"Instantiated {t_stats['count']} times. Moving template logic to type-erased dynamic pointers (void* / base base-class helpers) reduces code-bloat."
                })

        if not alerts:
            print("  No optimization anomalies detected! The project is compiled cleanly.", file=f)
            print("-" * 120, file=f)
            return

        sorted_alerts = sorted(alerts, key=lambda x: x["size"], reverse=True)
        for idx, alert in enumerate(sorted_alerts[:8]):
            print(f"  {idx+1:02d}. [{alert['type']}]", file=f)
            print(f"      Target: {alert['symbol']} in {alert['file']}", file=f)
            print(f"      Impact: {alert['size']:,} Bytes", file=f)
            print(f"      Advice: {alert['advice']}", file=f)
            print(file=f)
        print("-" * 120, file=f)

    def _print_cpp_runtime_overhead(self, f, eh, vtable, rtti, static_init, total_rom):
        cpp_total = eh + vtable + rtti + static_init
        cpp_perc = (cpp_total / total_rom) * 100 if total_rom > 0 else 0
        
        print(f"\n[ C++ RUNTIME & METADATA OVERHEAD ANALYSIS ]", file=f)
        print("-" * 120, file=f)
        print(f"  Total ROM space dedicated to class hierarchy, exception tables and runtime infrastructure:", file=f)
        print(f"  Allocated: {cpp_total:,} B ({cpp_total/1024:.2f} KB) | {cpp_perc:.2f}% of compiled code size", file=f)
        print("-" * 120, file=f)
        print(f"  - Exception Handling Layouts (.eh_frame / exceptions):  {eh:>10,} B | {(eh/total_rom)*100 if total_rom>0 else 0:>5.2f}%", file=f)
        print(f"  - Virtual Method Dispatch Tables (vtable layout):        {vtable:>10,} B | {(vtable/total_rom)*100 if total_rom>0 else 0:>5.2f}%", file=f)
        print(f"  - Run-Time Type Information (RTTI / typeinfo):          {rtti:>10,} B | {(rtti/total_rom)*100 if total_rom>0 else 0:>5.2f}%", file=f)
        print(f"  - Initializers & Destructors (ctors/static init):       {static_init:>10,} B | {(static_init/total_rom)*100 if total_rom>0 else 0:>5.2f}%", file=f)
        print("-" * 120, file=f)

    def _print_template_instantiation_report(self, f, template_groups):
        bloat_templates = [g for g in template_groups.values() if g["count"] > 1]
        if not bloat_templates:
            return

        total_bloat_size = sum(t["size"] for t in bloat_templates)
        print(f"\n[ C++ TEMPLATE INSTANTIATION OVERALLOCATION (TEMPLATES INSTANTIATED > 1) ]", file=f)
        print("-" * 120, file=f)
        print(f"  Linker detected template signatures instantiated across separate parameters multiple times.", file=f)
        print(f"  Cumulative Footprint: {total_bloat_size:,} B ({total_bloat_size/1024:.2f} KB) across {len(bloat_templates)} generic patterns", file=f)
        print("-" * 120, file=f)
        print(f"  {'Total Combined Size':>20} | {'Instances':<10} | {'Normalized Template Class / Method Signature':<83}", file=f)
        print("-" * 120, file=f)
        
        sorted_templates = sorted(bloat_templates, key=lambda x: x["size"], reverse=True)
        for t in sorted_templates[:15]:
            sig = t["signature"]
            if len(sig) > 83: sig = sig[:80] + "..."
            print(f"  {t['size']:>18,} B | {t['count']:<10} | {sig}", file=f)
        print("-" * 120, file=f)

    def _print_symbol_size_distribution(self, f, size_bins, total_symbols):
        print(f"\n[ SYMBOL SIZE DISTRIBUTION PROFILE ]", file=f)
        print("-" * 120, file=f)
        print(f"  Analyzes binary code and data allocation density.", file=f)
        print("-" * 120, file=f)
        
        max_symbols = max(size_bins.values()) if size_bins.values() else 1
        for label, count in size_bins.items():
            bar_len = int((count / max_symbols) * 35) if count > 0 else 0
            bar = "█" * bar_len
            perc = (count / total_symbols) * 100 if total_symbols > 0 else 0
            print(f"  {label:<50} : {count:>5,} symbols ({perc:>5.1f}%) {bar}", file=f)
        print("-" * 120, file=f)

    def _print_discarded_sections_report(self, f):
        if not self.discarded:
            return

        total_discarded_size = sum(d["size"] for d in self.discarded)
        print(f"\n[ DEAD CODE ELIMINATION (DISCARDED SYMBOLS) ]", file=f)
        print("-" * 120, file=f)
        print(f"  Total ROM space saved by Linker Optimization (-Wl,--gc-sections): {total_discarded_size:,} B ({total_discarded_size/1024:.2f} KB)", file=f)
        print(f"  Total Unused Symbols Pruned: {len(self.discarded):,}", file=f)
        print("-" * 120, file=f)
        print(f"  {'Size':>12} | {'Section Type':<20} | {'Symbol Name / Origin':<70}", file=f)
        print("-" * 120, file=f)
        
        sorted_discarded = sorted(self.discarded, key=lambda x: x["size"], reverse=True)
        for d in sorted_discarded[:10]:
            sec = d["section"][:18]
            name = self._get_clean_name(d)
            if len(name) > 70: name = name[:67] + "..."
            print(f"  {d['size']:>10,} B | {sec:<20} | {name}", file=f)
        print("="*120, file=f)

    def export_csv_database(self, csv_path):
        # Generate an easily filterable database for spreadsheet programs (Excel, Sheets)
        try:
            with open(csv_path, 'w', newline='', encoding='utf-8') as f:
                writer = csv.writer(f)
                writer.writerow(["Hex Address", "Size (Bytes)", "Section Name", "Source File Origin", "Retained", "Demangled Symbol / Signature"])
                
                # Kept symbols
                for s in self.sections:
                    writer.writerow([
                        f"0x{s['addr']:08X}",
                        s["size"],
                        s["section"],
                        s["origin"],
                        "TRUE",
                        self._get_clean_name(s)
                    ])
                
                # Discarded symbols
                for d in self.discarded:
                    writer.writerow([
                        "0x00000000",
                        d["size"],
                        d["section"],
                        d["origin"],
                        "FALSE",
                        self._get_clean_name(d)
                    ])
            return True
        except Exception as e:
            print(f"Warning: Failed to export CSV database: {e}")
            return False


if __name__ == "__main__":
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    default_path = os.path.join(project_root, "build", "RTerminal.map")
    
    path = sys.argv[1] if len(sys.argv) > 1 else default_path
    analyzer = MapAnalyzer(path)
    if analyzer.parse():
        report_dir = os.path.dirname(path)
        report_path = os.path.join(report_dir, "binary_report.txt")
        csv_path = os.path.join(report_dir, "symbol_database.csv")
        
        with open(report_path, "w", encoding="utf-8") as f:
            analyzer.print_report(f)
        
        analyzer.export_csv_database(csv_path)
        
        print(f"[+] Highly detailed optimization report saved to: {report_path}")
        print(f"[+] Structured search database exported to:       {csv_path}")