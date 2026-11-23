// A prepared SQLite statement that finalizes itself. Shared by the store and
// the station book (ops.cpp).
#pragma once

#include <sqlite3.h>

#include <cstdint>
#include <stdexcept>
#include <string>

namespace pna {

class Stmt {
public:
    Stmt(sqlite3* db, const char* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &s_, nullptr) != SQLITE_OK)
            throw std::runtime_error(std::string("sqlite prepare: ") + sqlite3_errmsg(db));
    }
    ~Stmt() { sqlite3_finalize(s_); }
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;
    Stmt& bind(int i, const std::string& v) {
        sqlite3_bind_text(s_, i, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
        return *this;
    }
    Stmt& bind_blob(int i, const std::string& v) {
        sqlite3_bind_blob(s_, i, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
        return *this;
    }
    Stmt& bind(int i, int64_t v) {
        sqlite3_bind_int64(s_, i, v);
        return *this;
    }
    bool step() {
        int rc = sqlite3_step(s_);
        if (rc == SQLITE_ROW) return true;
        if (rc == SQLITE_DONE) return false;
        throw std::runtime_error(std::string("sqlite step: ") + sqlite3_errmsg(sqlite3_db_handle(s_)));
    }
    std::string text(int c) {
        const unsigned char* t = sqlite3_column_text(s_, c);
        return t ? std::string(reinterpret_cast<const char*>(t), sqlite3_column_bytes(s_, c)) : "";
    }
    std::string blob(int c) {
        const void* b = sqlite3_column_blob(s_, c);
        return b ? std::string(static_cast<const char*>(b), sqlite3_column_bytes(s_, c)) : "";
    }
    int64_t i64(int c) { return sqlite3_column_int64(s_, c); }

private:
    sqlite3_stmt* s_ = nullptr;
};

}  // namespace pna
