#include "store/db.hpp"

#include <sqlite3.h>

#include <utility>

#include "core/log.hpp"

namespace store {
namespace {

sqlite3* as_db(void* handle) { return static_cast<sqlite3*>(handle); }
sqlite3_stmt* as_stmt(void* handle) { return static_cast<sqlite3_stmt*>(handle); }

/// Settings applied to every connection. Kept in one place so a new caller cannot
/// forget them; the reasons are in the header.
bool apply_pragmas(sqlite3* db) {
    static constexpr char kPragmas[] =
        "PRAGMA journal_mode = WAL;"
        "PRAGMA synchronous = NORMAL;"
        "PRAGMA foreign_keys = ON;";
    char* error = nullptr;
    // journal_mode returns a row ("wal"), which sqlite3_exec is happy to discard.
    if (sqlite3_exec(db, kPragmas, nullptr, nullptr, &error) != SQLITE_OK) {
        LOG_ERROR("sqlite pragma setup failed: %s",
                  error != nullptr ? error : "unknown");
        sqlite3_free(error);
        return false;
    }
    // Five seconds: long enough that the editor saving while the server reads does
    // not fail, short enough that a real deadlock still surfaces as an error.
    sqlite3_busy_timeout(db, 5000);
    return true;
}

}  // namespace

// --- Stmt -------------------------------------------------------------------

Stmt::~Stmt() {
    if (handle_ != nullptr) {
        sqlite3_finalize(as_stmt(handle_));
    }
}

Stmt::Stmt(Stmt&& other) noexcept
    : handle_(other.handle_), owner_(other.owner_), failed_(other.failed_) {
    other.handle_ = nullptr;
    other.owner_ = nullptr;
}

Stmt& Stmt::operator=(Stmt&& other) noexcept {
    if (this != &other) {
        if (handle_ != nullptr) {
            sqlite3_finalize(as_stmt(handle_));
        }
        handle_ = other.handle_;
        owner_ = other.owner_;
        failed_ = other.failed_;
        other.handle_ = nullptr;
        other.owner_ = nullptr;
    }
    return *this;
}

void Stmt::bind_int(int index, std::int64_t value) {
    if (handle_ != nullptr) {
        sqlite3_bind_int64(as_stmt(handle_), index, value);
    }
}

void Stmt::bind_double(int index, double value) {
    if (handle_ != nullptr) {
        sqlite3_bind_double(as_stmt(handle_), index, value);
    }
}

void Stmt::bind_text(int index, std::string_view value) {
    if (handle_ != nullptr) {
        // SQLITE_TRANSIENT: sqlite copies the bytes, so a temporary string_view
        // argument is safe. SQLITE_STATIC here would be a dangling read.
        sqlite3_bind_text(as_stmt(handle_), index, value.data(),
                          static_cast<int>(value.size()), SQLITE_TRANSIENT);
    }
}

void Stmt::bind_null(int index) {
    if (handle_ != nullptr) {
        sqlite3_bind_null(as_stmt(handle_), index);
    }
}

void Stmt::bind_blob(int index, const void* data, std::size_t size) {
    if (handle_ != nullptr) {
        sqlite3_bind_blob(as_stmt(handle_), index, data, static_cast<int>(size),
                          SQLITE_TRANSIENT);
    }
}

bool Stmt::step() {
    if (handle_ == nullptr) {
        return false;
    }
    const int rc = sqlite3_step(as_stmt(handle_));
    if (rc == SQLITE_ROW) {
        return true;
    }
    if (rc != SQLITE_DONE) {
        failed_ = true;
        LOG_ERROR("sqlite step failed: %s",
                  owner_ != nullptr ? owner_->last_error().c_str()
                                    : sqlite3_errstr(rc));
    }
    return false;
}

bool Stmt::run() {
    if (handle_ == nullptr) {
        return false;
    }
    // A statement that unexpectedly returns rows is not an error here; draining
    // it is. Stop at the first false and report whether anything went wrong.
    while (step()) {
    }
    return !failed_;
}

void Stmt::reset() {
    if (handle_ != nullptr) {
        sqlite3_reset(as_stmt(handle_));
        sqlite3_clear_bindings(as_stmt(handle_));
        failed_ = false;
    }
}

std::int64_t Stmt::column_int(int index) const {
    return handle_ == nullptr ? 0 : sqlite3_column_int64(as_stmt(handle_), index);
}

double Stmt::column_double(int index) const {
    return handle_ == nullptr ? 0.0
                              : sqlite3_column_double(as_stmt(handle_), index);
}

std::string Stmt::column_text(int index) const {
    if (handle_ == nullptr) {
        return {};
    }
    const unsigned char* text = sqlite3_column_text(as_stmt(handle_), index);
    if (text == nullptr) {
        return {};
    }
    const int size = sqlite3_column_bytes(as_stmt(handle_), index);
    return std::string(reinterpret_cast<const char*>(text),
                       static_cast<std::size_t>(size));
}

bool Stmt::column_is_null(int index) const {
    return handle_ == nullptr ||
           sqlite3_column_type(as_stmt(handle_), index) == SQLITE_NULL;
}

std::string Stmt::column_blob(int index) const {
    if (handle_ == nullptr) {
        return {};
    }
    const void* data = sqlite3_column_blob(as_stmt(handle_), index);
    const int size = sqlite3_column_bytes(as_stmt(handle_), index);
    if (data == nullptr || size <= 0) {
        return {};
    }
    return std::string(static_cast<const char*>(data),
                       static_cast<std::size_t>(size));
}

// --- Db ---------------------------------------------------------------------

Db::~Db() {
    if (handle_ != nullptr) {
        sqlite3_close(as_db(handle_));
    }
}

Db::Db(Db&& other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }

Db& Db::operator=(Db&& other) noexcept {
    if (this != &other) {
        if (handle_ != nullptr) {
            sqlite3_close(as_db(handle_));
        }
        handle_ = other.handle_;
        other.handle_ = nullptr;
    }
    return *this;
}

std::optional<Db> Db::open_with_flags(const std::string& path, int flags) {
    sqlite3* handle = nullptr;
    const int rc = sqlite3_open_v2(path.c_str(), &handle, flags, nullptr);
    if (rc != SQLITE_OK) {
        // sqlite3_open_v2 hands back a handle even on failure, purely so the error
        // message can be read off it. It still has to be closed.
        LOG_ERROR("cannot open '%s': %s", path.c_str(),
                  handle != nullptr ? sqlite3_errmsg(handle)
                                    : sqlite3_errstr(rc));
        sqlite3_close(handle);
        return std::nullopt;
    }
    if (!apply_pragmas(handle)) {
        sqlite3_close(handle);
        return std::nullopt;
    }
    return Db(handle);
}

std::optional<Db> Db::open(const std::string& path) {
    return open_with_flags(path, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
}

std::optional<Db> Db::open_read_only(const std::string& path) {
    return open_with_flags(path, SQLITE_OPEN_READONLY);
}

bool Db::exec(std::string_view sql) {
    if (handle_ == nullptr) {
        return false;
    }
    // sqlite3_exec needs a terminated string and string_view is not guaranteed to
    // be one.
    const std::string owned(sql);
    char* error = nullptr;
    if (sqlite3_exec(as_db(handle_), owned.c_str(), nullptr, nullptr, &error) !=
        SQLITE_OK) {
        LOG_ERROR("sqlite exec failed: %s",
                  error != nullptr ? error : "unknown");
        sqlite3_free(error);
        return false;
    }
    return true;
}

Stmt Db::prepare(std::string_view sql) {
    if (handle_ == nullptr) {
        return {};
    }
    sqlite3_stmt* stmt = nullptr;
    const int rc = sqlite3_prepare_v2(as_db(handle_), sql.data(),
                                      static_cast<int>(sql.size()), &stmt,
                                      nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR("sqlite prepare failed: %s (%.*s)", last_error().c_str(),
                  static_cast<int>(sql.size()), sql.data());
        return {};
    }
    return Stmt(stmt, this);
}

std::optional<std::int64_t> Db::query_int(std::string_view sql) {
    Stmt stmt = prepare(sql);
    if (!stmt.valid() || !stmt.step()) {
        return std::nullopt;
    }
    return stmt.column_int(0);
}

std::optional<std::int64_t> Db::user_version() {
    return query_int("PRAGMA user_version");
}

bool Db::set_user_version(std::int64_t version) {
    // PRAGMA takes no bound parameters, so the value has to be formatted in. It
    // is an int64 we produced, not input, so there is nothing to inject.
    return exec("PRAGMA user_version = " + std::to_string(version));
}

std::int64_t Db::last_insert_rowid() const {
    return handle_ == nullptr ? 0 : sqlite3_last_insert_rowid(as_db(handle_));
}

std::string Db::last_error() const {
    if (handle_ == nullptr) {
        return "no database";
    }
    const char* message = sqlite3_errmsg(as_db(handle_));
    return message != nullptr ? message : "unknown";
}

// --- Db::Transaction --------------------------------------------------------

Db::Transaction::Transaction(Db& db) : db_(&db) {
    active_ = db.exec("BEGIN");
}

Db::Transaction::~Transaction() {
    if (active_ && db_ != nullptr) {
        // Not committed: something returned early or threw. Rolling back is the
        // only safe reading of that.
        db_->exec("ROLLBACK");
    }
}

bool Db::Transaction::commit() {
    if (!active_ || db_ == nullptr) {
        return false;
    }
    active_ = false;
    return db_->exec("COMMIT");
}

}  // namespace store
