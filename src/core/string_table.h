#pragma once
#ifndef STRING_TABLE_H
#define STRING_TABLE_H

#include "string_id.h"
#include "dyn_string.h"
#include "log.h"
#include "../math/vector.h"
#include "internal.h"

static constexpr int kMaxLangCodeLen = 8;

struct StringEntry {
    StringID   id = 0;
    DynString  value;

    bool operator<(const StringEntry& o) const { return id < o.id; }
};

class StringTable {
public:
    StringTable();

    void setLanguage(const char* lang);
    const char* language() const { return language_; }

    void set(StringID id, const char* value);
    void set(const char* key, const char* value);

    const char* get(StringID id, const char* fallback = "") const;
    const char* get(const char* key, const char* fallback = "") const;

    bool has(StringID id) const;

    bool load(const char* path);
    bool loadINI(const char* path);
    bool save(const char* path) const;

    void clear();
    size_t size() const { return entries_.size(); }

private:
    void ensureSorted() const;

    mutable PodVector<StringEntry> entries_;
    mutable bool sorted_ = true;
    char language_[kMaxLangCodeLen] = {};
};

StringTable& globalStringTable();

#define _(sid) globalStringTable().get(sid)
#define _K(key) globalStringTable().get(SID(key))

#endif // STRING_TABLE_H