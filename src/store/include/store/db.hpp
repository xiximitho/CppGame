#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// A thin RAII wrapper over SQLite. Deliberately small: open, exec, prepare, bind,
// step, read. No ORM, no query builder, no schema reflection — the schemas in this
// project are a handful of tables written as plain SQL, and every layer of
// abstraction over SQL is a layer between a bug and the statement that caused it.
//
// WHO MAY LINK THIS: the server (player persistence) and the offline tools
// (content authoring). NOT the client. On Android and iOS the client's data lives
// inside the application package and is not a file, while SQLite needs a real
// path to open and seek; the client reads baked content through platform::vfs
// instead. scripts/check-layering.sh enforces that.
//
// No sqlite3 type appears in this header, so the sqlite3.h include stays inside
// store/ and consumers do not inherit its 690 KB or its macros.

namespace store {

class Db;

/// A prepared statement. Move-only; finalised on destruction.
///
/// Binds are 1-BASED (SQLite's convention, kept rather than hidden so the SQL and
/// the code agree by eye); column reads are 0-based, also SQLite's.
class Stmt {
public:
    Stmt() = default;
    ~Stmt();
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;
    Stmt(Stmt&& other) noexcept;
    Stmt& operator=(Stmt&& other) noexcept;

    bool valid() const { return handle_ != nullptr; }

    void bind_int(int index, std::int64_t value);
    void bind_double(int index, double value);
    void bind_text(int index, std::string_view value);
    void bind_null(int index);
    /// Binds a blob. The bytes are copied, so `data` need not outlive the call.
    void bind_blob(int index, const void* data, std::size_t size);

    /// Advances to the next row. Returns true while a row is available, false at
    /// the end OR on error — check failed() to tell those apart, because treating
    /// an error as "no more rows" is how a half-loaded save file happens.
    bool step();

    /// Runs a statement expected to yield no rows. Returns false on error.
    bool run();

    /// Resets for re-execution with new bindings, keeping the compiled statement.
    /// This is the point of preparing once and looping.
    void reset();

    std::int64_t column_int(int index) const;
    double       column_double(int index) const;
    std::string  column_text(int index) const;
    bool         column_is_null(int index) const;
    /// Copies the blob at `index` out. Empty when null or not a blob.
    std::string  column_blob(int index) const;

    bool failed() const { return failed_; }

private:
    friend class Db;
    explicit Stmt(void* handle, Db* owner) : handle_(handle), owner_(owner) {}

    void* handle_ = nullptr;   ///< sqlite3_stmt*
    Db*   owner_ = nullptr;    ///< for error reporting; never owned
    bool  failed_ = false;
};

/// An open database. Move-only; closed on destruction.
class Db {
public:
    Db() = default;
    ~Db();
    Db(const Db&) = delete;
    Db& operator=(const Db&) = delete;
    Db(Db&& other) noexcept;
    Db& operator=(Db&& other) noexcept;

    /// Opens (creating if absent) a database at `path`. Returns nullopt and logs
    /// on failure.
    ///
    /// Applies the settings this project wants everywhere, so no caller has to
    /// remember them: WAL journalling (a crash mid-write loses the transaction,
    /// not the database), NORMAL synchronous (WAL makes that crash-safe while
    /// avoiding an fsync per commit), foreign keys ON (the schema depends on them
    /// and SQLite defaults them OFF), and a busy timeout so a concurrent writer
    /// waits instead of failing immediately.
    static std::optional<Db> open(const std::string& path);

    /// Opens an existing database read-only. Fails if it does not exist, which is
    /// what the server wants for content: silently creating an empty catalogue
    /// would start a world where nothing blocks and nothing can be equipped.
    static std::optional<Db> open_read_only(const std::string& path);

    bool valid() const { return handle_ != nullptr; }

    /// Runs one or more semicolon-separated statements with no results. For
    /// schema DDL and pragmas.
    bool exec(std::string_view sql);

    /// Compiles `sql`. Returns a Stmt whose valid() is false on error.
    Stmt prepare(std::string_view sql);

    /// Convenience for a single-row single-column integer query (COUNT, MAX,
    /// user_version...). Returns nullopt if the query fails or yields no row.
    std::optional<std::int64_t> query_int(std::string_view sql);

    /// Schema version, via PRAGMA user_version. Zero on a fresh database, which
    /// is what makes "has this ever been migrated?" answerable without a table.
    std::optional<std::int64_t> user_version();
    bool set_user_version(std::int64_t version);

    /// rowid the last INSERT produced.
    std::int64_t last_insert_rowid() const;

    /// Message for the most recent failure on this connection.
    std::string last_error() const;

    /// RAII transaction. Rolls back unless commit() was called, so an early
    /// return or a thrown exception cannot leave a half-written save behind.
    class Transaction {
    public:
        explicit Transaction(Db& db);
        ~Transaction();
        Transaction(const Transaction&) = delete;
        Transaction& operator=(const Transaction&) = delete;

        bool begun() const { return active_; }
        bool commit();

    private:
        Db*  db_ = nullptr;
        bool active_ = false;
    };

private:
    explicit Db(void* handle) : handle_(handle) {}

    /// Shared body of open()/open_read_only(): the flags are all that differ.
    static std::optional<Db> open_with_flags(const std::string& path, int flags);

    void* handle_ = nullptr;  ///< sqlite3*
};

}  // namespace store
