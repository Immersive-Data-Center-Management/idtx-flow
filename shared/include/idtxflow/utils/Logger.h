#pragma once

#include <string>
#include <string_view>
#include <format>

namespace idtxflow
{
namespace utils
{

/**
 * @brief Log severity levels
 */
enum class LogLevel
{
    Error = 0, // Highest severity
    Warn = 1,
    Info = 2,
    Debug = 3 // Lowest severity (verbose)
};

/**
 * @brief Convert LogLevel to string
 */
constexpr std::string_view to_string(LogLevel level) noexcept
{
    switch (level)
    {
    case LogLevel::Error:
        return "ERROR";
    case LogLevel::Warn:
        return "WARN";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Debug:
        return "DEBUG";
    default:
        return "UNKNOWN";
    }
}

/**
 * @brief Abstract logger interface for engine adapters
 *
 * Engine adapters (Godot, Unreal, Unity, etc.) provide a concrete implementation
 * that routes log output to the engine's console/logging system.
 *
 * The adapter receives a fully formatted log line and only needs to dispatch
 * it to the appropriate engine output function based on the log level.
 * All message formatting and line composition is done upstream in the Log class.
 */
class ILogger
{
  protected:
    std::string prefix_ = "IDTXFlow"; // Product-level prefix, always present in log output
    std::string module_name_;         // Optional sub-plugin name (e.g., "Core", "Client")

  public:
    virtual ~ILogger() = default;

    /**
     * @brief Set the product-level prefix for log output
     * @param prefix Prefix string (default: "IDTXFlow")
     */
    void set_prefix(const std::string& prefix)
    {
        prefix_ = prefix;
    }

    /**
     * @brief Get current prefix
     */
    const std::string& get_prefix() const
    {
        return prefix_;
    }

    /**
     * @brief Set optional module/plugin name for log prefix
     * @param name Module name (e.g., "Core", "Client", etc.)
     */
    void set_module_name(const std::string& name)
    {
        module_name_ = name;
    }

    /**
     * @brief Get current module name
     */
    const std::string& get_module_name() const
    {
        return module_name_;
    }

    /**
     * @brief Output a fully formatted log line
     *
     * The line is already composed as: [Prefix] [LEVEL] [Category] message
     * (or with module: [Prefix] [Module] [LEVEL] [Category] message)
     *
     * Implementations only need to route this string to the engine's output
     * based on the log level
     *
     * @param formatted_line The complete, ready-to-output log line
     * @param level Severity level (for routing to the appropriate output)
     */
    virtual void log(std::string_view formatted_line, LogLevel level) = 0;
};

/**
 * @brief Global logger instance manager
 *
 * Provides static access to the logger via a single Log::log() method.
 * Handles all message formatting via std::format and log line composition
 * before dispatching to the engine adapter.
 *
 * Usage via macros:
 *   IDTX_LOG(IDTX_INFO, "Simple message");
 *   IDTX_LOG(IDTX_ERROR, "Failed for '{}': {}", url, err);
 *   IDTX_LOGF(IDTX_INFO, "Message without class category");
 */
class Log
{
  public:
    /**
     * @brief Set the global logger instance
     * @param logger Pointer to logger implementation (must remain valid)
     */
    static void set_logger(ILogger* logger) noexcept
    {
        instance_ = logger;
    }

    /**
     * @brief Get the current logger instance
     * @return Pointer to logger (may be nullptr if not set)
     */
    static ILogger* get_logger() noexcept
    {
        return instance_;
    }

    /**
     * @brief Log with explicit level and category
     */
    template <typename... Args>
    static void log(LogLevel level, std::string_view category, std::format_string<Args...> fmt, Args&&... args)
    {
        if (instance_)
        {
            auto message = std::format(fmt, std::forward<Args>(args)...);
            instance_->log(format_line(category, message, level), level);
        }
    }

  private:
    static ILogger* instance_;

    /**
     * @brief Compose the full log line with prefix, level, category, and message
     *
     * Format without module: [Prefix] [LEVEL] [Category] message
     * Format with module:    [Prefix] [Module] [LEVEL] [Category] message
     */
    static std::string format_line(std::string_view category, std::string_view message, LogLevel level)
    {
        if (instance_->get_module_name().empty())
        {
            return std::format("[{}] [{}] [{}] {}", instance_->get_prefix(), to_string(level), category, message);
        }
        else
        {
            return std::format("[{}] [{}] [{}] [{}] {}", instance_->get_prefix(), instance_->get_module_name(),
                               to_string(level), category, message);
        }
    }
};

inline ILogger* Log::instance_ = nullptr;

} // namespace utils
} // namespace idtxflow

// ============================================================================
// CONVENIENCE MACROS
// ============================================================================

/**
 * @brief Define the log category for a class or file scope
 *
 * In a class, place in the private section. At file scope, place before usage.
 *
 * Usage:
 *   class UsdStageNode3D {
 *   private:
 *       IDTX_LOG_CATEGORY("UsdStageNode3D")
 *   public:
 *       void load_stage() {
 *           IDTX_LOG(IDTX_INFO, "Loading USD stage");
 *       }
 *   };
 */
#define IDTX_LOG_CATEGORY(category) static constexpr const char* LOG_CATEGORY = category;

/**
 * @brief Shorthand log level constants
 *
 * Avoids writing the full idtxflow::utils::LogLevel:: namespace at call sites.
 */
constexpr auto IDTX_ERROR = idtxflow::utils::LogLevel::Error;
constexpr auto IDTX_WARN = idtxflow::utils::LogLevel::Warn;
constexpr auto IDTX_INFO = idtxflow::utils::LogLevel::Info;
constexpr auto IDTX_DEBUG = idtxflow::utils::LogLevel::Debug;

/**
 * @brief Log with explicit level, using LOG_CATEGORY from class/file scope
 *
 * Requires IDTX_LOG_CATEGORY to be defined (in a class or at file scope).
 *
 * Usage:
 *   IDTX_LOG(IDTX_INFO, "Configured with dir: {}", dir);
 *   IDTX_LOG(IDTX_ERROR, "Failed for '{}': {}", url, err);
 */
#define IDTX_LOG(level, ...) idtxflow::utils::Log::log(level, LOG_CATEGORY, __VA_ARGS__)

/**
 * @brief Category-free log with explicit level, defaults to "General" category
 *
 * For use outside of classes where IDTX_LOG_CATEGORY is not available.
 *
 * Usage:
 *   IDTX_LOGF(IDTX_INFO, "GDExtension initialized");
 *   IDTX_LOGF(IDTX_DEBUG, "Processing {} items", count);
 */
#define IDTX_LOGF(level, ...) idtxflow::utils::Log::log(level, std::string_view{"General"}, __VA_ARGS__)