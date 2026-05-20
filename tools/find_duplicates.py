import os
import sys
import hashlib
from collections import defaultdict

def strip_line(line):
    line = line.strip()
    if line.startswith('//'): return ''
    return line

def get_files(directory):
    files = []
    for root, _, filenames in os.walk(directory):
        for f in filenames:
            if f.endswith('.cpp') or f.endswith('.h'):
                files.append(os.path.join(root, f))
    return files

def find_dups():
    files = get_files('src')
    
    # window size (number of significant lines)
    WINDOW = 10
    
    windows = defaultdict(list)
    
    for fpath in files:
        with open(fpath, 'r', encoding='utf-8', errors='ignore') as f:
            lines = f.readlines()
            
        significant_lines = []
        for i, line in enumerate(lines):
            stripped = strip_line(line)
            if stripped and len(stripped) > 3: # ignore very short lines like "{" or "};"
                significant_lines.append((i+1, stripped, line))
                
        if len(significant_lines) < WINDOW:
            continue
            
        for i in range(len(significant_lines) - WINDOW + 1):
            window_lines = significant_lines[i:i+WINDOW]
            # concat stripped lines for hash
            block_text = "".join(x[1] for x in window_lines)
            
            # create hash
            h = hashlib.md5(block_text.encode('utf-8')).hexdigest()
            
            # store the file and start line
            windows[h].append((fpath, window_lines[0][0], window_lines[-1][0]))

    # filter to only those appearing in > 1 distinct locations
    # a location is distinct if it's in a different file or far away in same file
    dups = []
    for h, locs in windows.items():
        if len(locs) > 1:
            # check if they are truly distinct (not just overlapping sliding windows)
            distinct_locs = []
            for loc in locs:
                is_overlap = False
                for d_loc in distinct_locs:
                    if loc[0] == d_loc[0] and abs(loc[1] - d_loc[1]) < WINDOW:
                        is_overlap = True
                        break
                if not is_overlap:
                    distinct_locs.append(loc)
            
            if len(distinct_locs) > 1:
                dups.append((h, distinct_locs))
                
    total_dup_blocks = len(dups)
    print(f"Found {total_dup_blocks} duplicated blocks of code ({WINDOW}+ consecutive significant lines).")
    
    # print top 10
    dups.sort(key=lambda x: len(x[1]), reverse=True)
    for h, locs in dups[:10]:
        print("\n--- Duplicated Block found in %d places ---" % len(locs))
        for loc in locs:
            print(f"  {loc[0]}:{loc[1]}-{loc[2]}")

if __name__ == "__main__":
    find_dups()
