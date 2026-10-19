#pragma once

#include <idtxflow/utils/Logger.h>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/string.hpp>

namespace idtxflow
{
namespace utils
{

/**
 * @brief Godot engine logger adapter
 *
 * Routes fully formatted log lines to Godot's console using
 * level-appropriate output functions:
 * - Error:   print_error()           — print as error
 * - Warn:    push_warning()          — print as warning
 * - Info:    print_rich()            — cyan colored output
 * - Debug:   print_rich()            — orange colored, only when verbose enabled
 */
class IDTXFlowGodotLogger : public ILogger
{
  public:
    IDTXFlowGodotLogger() = default;
    ~IDTXFlowGodotLogger() override = default;

    void log(std::string_view formatted_line, LogLevel level) override
    {
        using namespace godot;

        const String line(std::string(formatted_line).c_str());

        switch (level)
        {
        case LogLevel::Error:
            print_error(line);
            break;
        case LogLevel::Warn:
            UtilityFunctions::push_warning(line);
            break;
        case LogLevel::Info:
            UtilityFunctions::print_rich(String("[color=cyan]") + line + String("[/color]"));
            break;
        case LogLevel::Debug:
            if (is_print_verbose_enabled())
            {
                UtilityFunctions::print_rich(String("[color=orange]") + line + String("[/color]"));
            }
            break;
        }
    }
};

} // namespace utils
} // namespace idtxflow