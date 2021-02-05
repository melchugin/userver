#pragma once

#include <atomic>
#include <chrono>
#include <string>

#include <engine/task/task_processor.hpp>
#include <error_injection/settings.hpp>
#include <testsuite/postgres_control.hpp>
#include <utils/size_guard.hpp>
#include <utils/statistics/min_max_avg.hpp>
#include <utils/strong_typedef.hpp>

#include <storages/postgres/detail/query_parameters.hpp>
#include <storages/postgres/detail/time_types.hpp>
#include <storages/postgres/dsn.hpp>
#include <storages/postgres/options.hpp>
#include <storages/postgres/result_set.hpp>
#include <storages/postgres/transaction.hpp>

namespace storages::postgres {

enum class ConnectionState {
  kOffline,     //!< Not connected
  kIdle,        //!< Connected, not in transaction
  kTranIdle,    //!< In a valid transaction block, idle
  kTranActive,  //!< In a transaction, processing a SQL statement
  kTranError    //!< In a failed transaction block, idle
};

namespace detail {

/// @brief PostreSQL connection class
/// Handles connecting to Postgres, sending commands, processing command results
/// and closing Postgres connection.
/// Responsible for all asynchronous operations
class Connection {
 public:
  enum class ParameterScope {
    kSession,     //!< Parameter is set for the duration of the whole session
    kTransaction  //!< Parameter will be in effect until the transaction is
                  //!< finished
  };

  /// Strong typedef for IDs assigned to prepared statements
  using StatementId =
      ::utils::StrongTypedef<struct StatementIdTag, std::size_t>;

  /// @brief Statistics storage
  /// @note Should be reset after every transaction execution
  struct Statistics {
    using SmallCounter = uint8_t;
    using Counter = uint16_t;
    using CurrentValue = uint32_t;

    /// Number of transactions started
    SmallCounter trx_total : 1 {0};
    /// Number of transactions committed
    SmallCounter commit_total : 1 {0};
    /// Number of transactions rolled back
    SmallCounter rollback_total : 1 {0};
    /// Number of out-of-transaction executions
    SmallCounter out_of_trx : 1 {0};
    /// Number of parsed queries
    Counter parse_total{0};
    /// Number of query executions (calls to `Execute`)
    Counter execute_total{0};
    /// Total number of replies
    Counter reply_total{0};
    /// Error during query execution
    Counter error_execute_total{0};
    /// Timeout while executing
    Counter execute_timeout{0};
    /// Number of duplicate prepared statements errors,
    /// probably caused by timeout while preparing
    Counter duplicate_prepared_statements{0};

    /// Current number of prepared statements
    CurrentValue prepared_statements_current{0};

    /// Transaction initiation time (includes wait in pool)
    SteadyClock::time_point trx_start_time;
    /// Actual work start time (doesn't include pool wait time)
    SteadyClock::time_point work_start_time;
    /// Transaction end time (user called commit/rollback/finish)
    SteadyClock::time_point trx_end_time;
    /// Time of last statement executed, to calculate times between statement
    /// processing finish and user letting go of the connection.
    SteadyClock::time_point last_execute_finish;
    /// Sum of all query durations
    SteadyClock::duration sum_query_duration{0};
  };

  using SizeGuard = ::utils::SizeGuard<std::shared_ptr<std::atomic<size_t>>>;

 public:
  Connection(const Connection&) = delete;
  Connection(Connection&&) = delete;
  ~Connection();
  // clang-format off
  /// Connect to database using DSN
  ///
  /// Will suspend current coroutine
  ///
  /// @param dsn DSN, @see https://www.postgresql.org/docs/12/static/libpq-connect.html#LIBPQ-CONNSTRING
  /// @param bg_task_processor task processor for blocking operations
  /// @param id host-wide unique id for connection identification in logs
  /// @param settings the connection settings
  /// @param default_cmd_ctls default parameters for operations
  /// @param testsuite_pg_ctl operation parameters customizer for testsuite
  /// @param ei_settings error injection settings
  /// @param size_guard structure to track the size of owning connection pool
  /// @throws ConnectionFailed, ConnectionTimeoutError
  // clang-format on
  static std::unique_ptr<Connection> Connect(
      const Dsn& dsn, engine::TaskProcessor& bg_task_processor, uint32_t id,
      ConnectionSettings settings,
      const DefaultCommandControls& default_cmd_ctls,
      const testsuite::PostgresControl& testsuite_pg_ctl,
      const error_injection::Settings& ei_settings,
      SizeGuard&& size_guard = SizeGuard{});

  CommandControl GetDefaultCommandControl() const;

  void UpdateDefaultCommandControl();

  /// Close the connection
  /// TODO When called from another thread/coroutine will wait for current
  /// transaction to finish.
  void Close();

  /// Get currently accumulated statistics and reset counters
  /// @note May only be called when connection is not in transaction
  Statistics GetStatsAndReset();

  bool IsInRecovery() const;
  bool IsReadOnly() const;
  void RefreshReplicaState(engine::Deadline) const;

  /// Get current connection state
  ConnectionState GetState() const;
  /// Check if the connection is active
  bool IsConnected() const;
  /// Check if the connection is currently idle (IsConnected &&
  /// !IsInTransaction)
  bool IsIdle() const;

  //@{
  /// Check if connection is currently in transaction
  bool IsInTransaction() const;
  /// Begin a transaction in Postgres with specific start time point
  /// Suspends coroutine for execution
  /// @throws AlreadyInTransaction
  void Begin(const TransactionOptions& options,
             SteadyClock::time_point trx_start_time,
             OptionalCommandControl trx_cmd_ctl = {});
  /// Commit current transaction
  /// Suspends coroutine for execution
  /// @throws NotInTransaction
  void Commit();
  /// Rollback current transaction
  /// Suspends coroutine for execution
  /// @throws NotInTransaction
  void Rollback();

  /// Mark start time of non-transaction execution, for stats
  void Start(SteadyClock::time_point start_time);
  /// Mark non-transaction execution finished, for stats
  void Finish();
  //@}

  //@{
  /** @name Command sending interface */
  /// Cancel current operation
  void Cancel();
  ResultSet Execute(const std::string& statement,
                    const detail::QueryParameters& = {},
                    OptionalCommandControl statement_cmd_ctl = {});

  template <typename... T>
  ResultSet Execute(const std::string& statement, const T&... args) {
    detail::QueryParameters params;
    params.Write(GetUserTypes(), args...);
    return Execute(statement, params);
  }

  template <typename... T>
  ResultSet Execute(CommandControl statement_cmd_ctl,
                    const std::string& statement, const T&... args) {
    detail::QueryParameters params;
    params.Write(GetUserTypes(), args...);
    return Execute(statement, params,
                   OptionalCommandControl{statement_cmd_ctl});
  }

  StatementId PortalBind(const std::string& statement,
                         const std::string& portal_name,
                         const detail::QueryParameters& params,
                         OptionalCommandControl);
  ResultSet PortalExecute(StatementId, const std::string& portal_name,
                          std::uint32_t n_rows, OptionalCommandControl);

  /// Send cancel to the database backend
  /// Try to return connection to idle state discarding all results.
  /// If there is a transaction in progress - roll it back.
  /// For usage in connection pools.
  /// Will do nothing if connection failed, it's responsibility of the pool
  /// to destroy the connection.
  void CancelAndCleanup(TimeoutDuration timeout);

  /// Wait while database connection is busy
  /// For usage in transaction pools, before an attempt to cancel
  /// If the connection is still busy, return false
  /// If the connection is in TranActive state return false
  /// If the connection is in TranIdle or TranError - rollback transaction and
  /// return true
  bool Cleanup(TimeoutDuration timeout);

  /// @brief Set session parameter
  /// Parameters documentation
  /// https://www.postgresql.org/docs/current/sql-set.html
  void SetParameter(const std::string& param, const std::string& value,
                    ParameterScope scope);
  /// @brief Reload user types after creating a type
  void ReloadUserTypes();
  const UserTypes& GetUserTypes() const;

  //@{
  /** @name Command sending interface for experimenting */
  /// Separate method for experimenting with PostgreSQL protocol and parsing
  /// Not visible to users of PostgreSQL driver
  ResultSet ExperimentalExecute(const std::string& statement,
                                const detail::QueryParameters& = {});
  template <typename... T>
  ResultSet ExperimentalExecute(const std::string& statement,
                                const T&... args) {
    detail::QueryParameters params;
    params.Write(GetUserTypes(), args...);
    return ExperimentalExecute(statement, params);
  }
  //@}
  //@}

  /// Get duration since last network operation
  TimeoutDuration GetIdleDuration() const;
  /// Ping the connection.
  /// The function will do a query roundtrip to the database
  void Ping();

  void MarkAsBroken();

  /// Used in tests.
  const OptionalCommandControl& GetTransactionCommandControl() const;

 private:
  Connection();

  struct Impl;
  std::unique_ptr<Impl> pimpl_;
};

}  // namespace detail
}  // namespace storages::postgres
