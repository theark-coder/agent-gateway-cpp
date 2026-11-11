#include "agent/mysql_task_repository.hpp"

#include <mysql.h>
#include <mysqld_error.h>

#include <array>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace agent {
namespace {

void exec_sql(MYSQL* db, const char* sql) {
    if (mysql_query(db, sql) != 0) {
        throw std::runtime_error(std::string("MySQL SQL failed: ") + mysql_error(db));
    }
}

class Transaction {
public:
    explicit Transaction(MYSQL* db) : db_(db) { exec_sql(db_, "START TRANSACTION"); }
    ~Transaction() { if (!done_) mysql_rollback(db_); }
    void commit() {
        if (mysql_commit(db_) != 0)
            throw std::runtime_error(std::string("MySQL commit failed: ") + mysql_error(db_));
        done_ = true;
    }
    void rollback() {
        if (mysql_rollback(db_) != 0)
            throw std::runtime_error(std::string("MySQL rollback failed: ") + mysql_error(db_));
        done_ = true;
    }
private:
    MYSQL* db_;
    bool done_ = false;
};

class Statement {
public:
    Statement(MYSQL* db, const char* sql) : stmt_(mysql_stmt_init(db)) {
        if (!stmt_) throw std::runtime_error("mysql_stmt_init failed");
        if (mysql_stmt_prepare(stmt_, sql, static_cast<unsigned long>(std::char_traits<char>::length(sql))) != 0) {
            const std::string error = mysql_stmt_error(stmt_);
            mysql_stmt_close(stmt_);
            stmt_ = nullptr;
            throw std::runtime_error("MySQL prepare failed: " + error);
        }
    }
    ~Statement() { if (stmt_) mysql_stmt_close(stmt_); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    void bind_and_execute(const std::vector<std::string>& params) {
        if (mysql_stmt_param_count(stmt_) != params.size())
            throw std::logic_error("MySQL parameter count mismatch");
        std::vector<MYSQL_BIND> binds(params.size());
        std::vector<unsigned long> lengths(params.size());
        for (std::size_t i = 0; i < params.size(); ++i) {
            lengths[i] = static_cast<unsigned long>(params[i].size());
            binds[i].buffer_type = MYSQL_TYPE_STRING;
            binds[i].buffer = const_cast<char*>(params[i].data());
            binds[i].buffer_length = lengths[i];
            binds[i].length = &lengths[i];
        }
        if (!binds.empty() && mysql_stmt_bind_param(stmt_, binds.data()) != 0)
            throw std::runtime_error(std::string("MySQL bind failed: ") + mysql_stmt_error(stmt_));
        if (mysql_stmt_execute(stmt_) != 0)
            throw DbError(mysql_stmt_errno(stmt_), mysql_stmt_error(stmt_));
    }

    struct DbError : std::runtime_error {
        unsigned int code;
        DbError(unsigned int number, const char* message)
            : std::runtime_error(message), code(number) {}
    };
    MYSQL_STMT* get() const { return stmt_; }
private:
    MYSQL_STMT* stmt_;
};

struct DbTask {
    Task task;
    std::string fingerprint;
};

struct RowBuffer {
    int id = 0;
    std::array<char, 1024> title{};
    std::array<char, 64> status{};
    std::array<char, 1024> fingerprint{};
    unsigned long length[4]{};
    MYSQL_BIND binds[4]{};

    RowBuffer() {
        binds[0].buffer_type = MYSQL_TYPE_LONG;
        binds[0].buffer = &id;
        binds[1].buffer_type = MYSQL_TYPE_STRING;
        binds[1].buffer = title.data();
        binds[1].buffer_length = title.size();
        binds[2].buffer_type = MYSQL_TYPE_STRING;
        binds[2].buffer = status.data();
        binds[2].buffer_length = status.size();
        binds[3].buffer_type = MYSQL_TYPE_STRING;
        binds[3].buffer = fingerprint.data();
        binds[3].buffer_length = fingerprint.size();
        for (int i = 0; i < 4; ++i) binds[i].length = &length[i];
    }
};

TaskStatus parse_status(const std::string& s) {
    if (s == "pending") return TaskStatus::pending;
    if (s == "running") return TaskStatus::running;
    if (s == "completed") return TaskStatus::completed;
    if (s == "failed") return TaskStatus::failed;
    if (s == "cancelled") return TaskStatus::cancelled;
    if (s == "rejected") return TaskStatus::rejected;
    if (s == "timed_out") return TaskStatus::timed_out;
    throw std::runtime_error("Unexpected task status from MySQL");
}

const char* status_string(TaskStatus s) {
    switch (s) {
        case TaskStatus::pending: return "pending";
        case TaskStatus::running: return "running";
        case TaskStatus::completed: return "completed";
        case TaskStatus::failed: return "failed";
        case TaskStatus::cancelled: return "cancelled";
        case TaskStatus::rejected: return "rejected";
        case TaskStatus::timed_out: return "timed_out";
    }
    throw std::logic_error("Unknown task status");
}

std::vector<DbTask> select_tasks(MYSQL* db, const char* sql,
                                const std::vector<std::string>& params) {
    Statement stmt(db, sql);
    stmt.bind_and_execute(params);
    RowBuffer row;
    if (mysql_stmt_bind_result(stmt.get(), row.binds) != 0)
        throw std::runtime_error(std::string("MySQL result bind failed: ") + mysql_stmt_error(stmt.get()));

    std::vector<DbTask> out;
    for (;;) {
        int rc = mysql_stmt_fetch(stmt.get());
        if (rc == MYSQL_NO_DATA) break;
        if (rc == MYSQL_DATA_TRUNCATED)
            throw std::runtime_error("MySQL task result truncated");
        if (rc != 0)
            throw std::runtime_error(std::string("MySQL fetch failed: ") + mysql_stmt_error(stmt.get()));
        out.push_back({
            Task{row.id,
                 std::string(row.title.data(), row.length[1]),
                 parse_status(std::string(row.status.data(), row.length[2]))},
            std::string(row.fingerprint.data(), row.length[3])
        });
    }
    return out;
}

} // namespace

MySqlTaskRepository::MySqlTaskRepository(const MySqlConfig& config) {
    MYSQL* db = mysql_init(nullptr);
    if (!db) throw std::runtime_error("mysql_init failed");
    try {
        unsigned int connect_timeout = 5;
        mysql_options(db, MYSQL_OPT_CONNECT_TIMEOUT, &connect_timeout);
        my_bool enforce_tls = 1;
        mysql_options(db, MYSQL_OPT_SSL_ENFORCE, &enforce_tls);
        my_bool verify = config.insecure_skip_verify ? 0 : 1;
        mysql_options(db, MYSQL_OPT_SSL_VERIFY_SERVER_CERT, &verify);
        if (!config.ssl_ca.empty()) {
            if (mysql_ssl_set(db, nullptr, nullptr, config.ssl_ca.c_str(), nullptr, nullptr) != 0)
                throw std::runtime_error("MySQL SSL CA configuration failed");
        }
        if (!mysql_real_connect(db, config.host.c_str(), config.user.c_str(),
                                config.password.c_str(), config.database.c_str(),
                                config.port, nullptr, 0))
            throw std::runtime_error(std::string("MySQL connect failed: ") + mysql_error(db));
        const char* cipher = mysql_get_ssl_cipher(db);
        if (!cipher || !*cipher)
            throw std::runtime_error("MySQL connection is not using TLS");
        db_ = db;
    } catch (...) {
        mysql_close(db);
        throw;
    }
}

MySqlTaskRepository::~MySqlTaskRepository() {
    if (db_) mysql_close(db_);
}

Result MySqlTaskRepository::create_task(const std::string& title,
                                        const std::string& actor_id,
                                        const std::string& key,
                                        const std::string& fingerprint) {
    std::lock_guard<std::mutex> lock(mutex_);
    Transaction tx(db_);
    const char* sql_with_key =
        "INSERT INTO tasks (title, actor_id, status, idempotency_key, request_fingerprint) "
        "VALUES (?, ?, 'pending', ?, ?)";
    const char* sql_no_key =
        "INSERT INTO tasks (title, actor_id, status, idempotency_key, request_fingerprint) "
        "VALUES (?, ?, 'pending', NULL, ?)";
    try {
        Statement insert(db_, key.empty() ? sql_no_key : sql_with_key);
        if (key.empty()) insert.bind_and_execute({title, actor_id, fingerprint});
        else insert.bind_and_execute({title, actor_id, key, fingerprint});
        const auto inserted = mysql_stmt_insert_id(insert.get());
        if (inserted > static_cast<unsigned long long>(std::numeric_limits<int>::max()))
            throw std::overflow_error("Task ID out of range");
        tx.commit();
        return {true, "task created", static_cast<int>(inserted)};
    } catch (const Statement::DbError& e) {
        if (e.code != ER_DUP_ENTRY || key.empty()) throw;
        tx.rollback();
        const auto existing = select_tasks(db_,
            "SELECT id,title,status,request_fingerprint FROM tasks "
            "WHERE actor_id=? AND idempotency_key=? LIMIT 1",
            {actor_id, key});
        if (existing.empty()) throw std::runtime_error("Idempotency key conflict but row not found");
        if (existing[0].fingerprint != fingerprint)
            return {false, "idempotency_conflict", existing[0].task.id, false, true};
        return {true, "idempotent replay", existing[0].task.id, true, false};
    }
}

std::optional<Task> MySqlTaskRepository::find_task(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = select_tasks(db_,
        "SELECT id,title,status,request_fingerprint FROM tasks WHERE id=? LIMIT 1",
        {std::to_string(id)});
    if (found.empty()) return std::nullopt;
    return found[0].task;
}

std::vector<Task> MySqlTaskRepository::list_tasks() {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = select_tasks(db_,
        "SELECT id,title,status,request_fingerprint FROM tasks ORDER BY id DESC LIMIT 1000", {});
    std::vector<Task> tasks;
    tasks.reserve(found.size());
    for (auto& row : found) tasks.push_back(std::move(row.task));
    return tasks;
}

bool MySqlTaskRepository::transition_task(int id, TaskStatus expected, TaskStatus next) {
    std::lock_guard<std::mutex> lock(mutex_);
    Statement update(db_,
        "UPDATE tasks SET status=?, version=version+1 WHERE id=? AND status=?");
    update.bind_and_execute({status_string(next), std::to_string(id), status_string(expected)});
    return mysql_stmt_affected_rows(update.get()) == 1;
}

void MySqlTaskRepository::recover_unfinished() {
    std::lock_guard<std::mutex> lock(mutex_);
    exec_sql(db_, "UPDATE tasks SET status='failed', version=version+1 "
                 "WHERE status IN ('pending','running')");
}

} // namespace agent
