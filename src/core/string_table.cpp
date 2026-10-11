#include "string_table.h"
#include "runtime.h"

StringTable::StringTable() {
    language_[0] = '\0';
}

void StringTable::setLanguage(const char* lang) {
    if (!lang) return;
    size_t i = 0;
    for (; i < kMaxLangCodeLen - 1 && lang[i]; ++i) language_[i] = lang[i];
    language_[i] = '\0';
}

void StringTable::set(StringID id, const char* value) {
    if (!value) return;
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].id == id) {
            entries_[i].value = value;
            return;
        }
    }
    entries_.push_back({id, DynString(value)});
    sorted_ = false;
}

void StringTable::set(const char* key, const char* value) {
    set(SID_RT(key), value);
}

const char* StringTable::get(StringID id, const char* fallback) const {
    if (entries_.empty()) return fallback;
    ensureSorted();

    int low = 0, high = (int)entries_.size() - 1;
    while (low <= high) {
        int mid = low + (high - low) / 2;
        if (entries_[mid].id == id) return entries_[mid].value.c_str();
        if (entries_[mid].id < id)  low = mid + 1;
        else                        high = mid - 1;
    }
    return fallback;
}

const char* StringTable::get(const char* key, const char* fallback) const {
    return get(SID_RT(key), fallback);
}

bool StringTable::has(StringID id) const {
    return strcmp(get(id, "\01_NOT_FOUND_"), "\01_NOT_FOUND_") != 0;
}

bool StringTable::load(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) { LOG_WARN("StringTable: cannot open '%s'", path); return false; }
    char line[512];
    int loaded = 0;
    while (fgets(line, sizeof(line), f)) {
        for (char* p = line; *p; ++p) if (*p == '\n' || *p == '\r') { *p = '\0'; break; }
        char* sep = strchr(line, ',');
        if (!sep) continue;
        *sep = '\0';
        const char* key = line, *val = sep + 1;
        if (key[0] == '#' || key[0] == '\0') continue;
        set(key, val);
        ++loaded;
    }
    fclose(f);
    ensureSorted();
    LOG_INFO("StringTable: loaded %d strings from '%s'", loaded, path);
    return true;
}

bool StringTable::loadINI(const char* path) {
    FILE* f = fopen(path, "r"); if (!f) return false;
    char line[512]; int loaded = 0;
    while (fgets(line, sizeof(line), f)) {
        for (char* p = line; *p; ++p) if (*p == '\n' || *p == '\r') { *p = '\0'; break; }
        char* eq = strchr(line, '='); if (!eq) continue;
        *eq = '\0';
        if (line[0] == '#' || line[0] == ';' || line[0] == '\0') continue;
        set(line, eq + 1); ++loaded;
    }
    fclose(f);
    ensureSorted();
    LOG_INFO("StringTable: loaded %d strings from INI '%s'", loaded, path);
    return true;
}

bool StringTable::save(const char* path) const {
    FILE* f = fopen(path, "w");
    if (!f) return false;
    for (const auto& e : entries_) {
        fputs(SID_DBG(e.id), f);
        fputs(",", f);
        fputs(e.value.c_str(), f);
        fputs("\n", f);
    }
    fclose(f);
    return true;
}

void StringTable::clear() { entries_.clear(); sorted_ = true; }

void StringTable::ensureSorted() const {
    if (sorted_ || entries_.size() < 2) { sorted_ = true; return; }
    // Insertion sort to avoid std::sort overhead
    for (size_t i = 1; i < entries_.size(); ++i) {
        StringEntry key = entries_[i];
        int j = (int)i - 1;
        while (j >= 0 && key < entries_[j]) {
            entries_[j + 1] = entries_[j];
            j--;
        }
        entries_[j + 1] = key;
    }
    sorted_ = true;
}

StringTable& globalStringTable() {
    static StringTable st;
    return st;
}