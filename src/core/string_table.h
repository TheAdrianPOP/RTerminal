#pragma once
#ifndef STRING_TABLE_H
#define STRING_TABLE_H

// Localization string table.
// Maps StringIDs (or string keys) to translated strings for the active language.
//
// Usage:
//   StringTable& loc = globalStringTable();
//   loc.setLanguage("fr");
//   loc.load("data/strings_fr.csv");  // key,value CSV
//   const char* msg = loc.get(SID("ui.start_game"), "Start Game");
//   // Returns French text if found, fallback otherwise

#include "string_id.h"
#include "dyn_string.h"
#include "log.h"
#include "../math/vector.h"
#include <cstdio>
#include <cstring>

static constexpr int kMaxStringEntries = 1024;
static constexpr int kMaxLangCodeLen   = 8;

struct StringEntry {
    StringID   id    = 0;
    DynString  value;
};

class StringTable {
public:
    StringTable() { language_[0] = '\0'; }

    void setLanguage(const char* lang) {
        strncpy(language_, lang, kMaxLangCodeLen-1);
        language_[kMaxLangCodeLen-1] = '\0';
    }
    const char* language() const { return language_; }

    // Add or overwrite an entry directly
    void set(StringID id, const char* value) {
        for (auto& e : entries_) if (e.id == id) { e.value = value; return; }
        entries_.push_back({id, DynString(value)});
    }
    void set(const char* key, const char* value) { set(SID_RT(key), value); }

    // Get translated string — returns fallback if not found
    const char* get(StringID id, const char* fallback = "") const {
        for (const auto& e : entries_) if (e.id == id) return e.value.c_str();
        return fallback;
    }
    const char* get(const char* key, const char* fallback = "") const {
        return get(SID_RT(key), fallback);
    }

    bool has(StringID id) const {
        for (const auto& e : entries_) if (e.id == id) return true;
        return false;
    }

    // Load from a simple CSV: key,value\n  (no quotes, no escapes — extend as needed)
    bool load(const char* path) {
        FILE* f = fopen(path, "r"); if (!f) { LOG_WARN("StringTable: cannot open '%s'", path); return false; }
        char line[512];
        int loaded = 0;
        while (fgets(line, sizeof(line), f)) {
            // Strip newline
            for (char* p=line; *p; ++p) if (*p=='\n'||*p=='\r') { *p='\0'; break; }
            char* sep = strchr(line, ',');
            if (!sep) continue;
            *sep = '\0';
            const char* key = line, *val = sep+1;
            if (key[0]=='#'||key[0]=='\0') continue; // comment / blank
            set(key, val);
            ++loaded;
        }
        fclose(f);
        LOG_INFO("StringTable: loaded %d strings from '%s'", loaded, path);
        return true;
    }

    // Load from a simple INI-style file: key=value\n
    bool loadINI(const char* path) {
        FILE* f = fopen(path, "r"); if (!f) return false;
        char line[512]; int loaded=0;
        while (fgets(line, sizeof(line), f)) {
            for (char* p=line; *p; ++p) if (*p=='\n'||*p=='\r') { *p='\0'; break; }
            char* eq = strchr(line, '='); if (!eq) continue;
            *eq='\0';
            if (line[0]=='#'||line[0]==';'||line[0]=='\0') continue;
            set(line, eq+1); ++loaded;
        }
        fclose(f);
        LOG_INFO("StringTable: loaded %d strings from INI '%s'", loaded, path);
        return true;
    }

    // Export current table to CSV
    bool save(const char* path) const {
        FILE* f = fopen(path, "w"); if (!f) return false;
        for (const auto& e : entries_)
            fprintf(f, "%s,%s\n", SID_DBG(e.id), e.value.c_str());
        fclose(f); return true;
    }

    void clear() { entries_.clear(); }
    size_t size() const { return entries_.size(); }

private:
    PodVector<StringEntry> entries_;
    char language_[kMaxLangCodeLen] = {};
};

inline StringTable& globalStringTable() { static StringTable st; return st; }

// Convenience macro: _(SID("key")) → globalStringTable().get(SID("key"))
#define _(sid) globalStringTable().get(sid)
#define _K(key) globalStringTable().get(SID(key))

#endif
