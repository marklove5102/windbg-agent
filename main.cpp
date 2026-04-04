#include <dbgeng.h>
#include <string>
#include <windows.h>

#include "http_server.hpp"
#include "mcp_server.hpp"
#include "version.h"
#include "dml_output.hpp"
#include "windbg_client.hpp"

namespace
{

// Helper to get IDebugControl for output
static IDebugControl* GetControl(PDEBUG_CLIENT Client)
{
    IDebugControl* control = nullptr;
    Client->QueryInterface(__uuidof(IDebugControl), (void**)&control);
    return control;
}

} // namespace

// Extension entry point
extern "C" HRESULT CALLBACK DebugExtensionInitialize(PULONG Version, PULONG Flags)
{
    *Version = DEBUG_EXTENSION_VERSION(WINDBG_AGENT_VERSION_MAJOR, WINDBG_AGENT_VERSION_MINOR);
    *Flags = 0;
    return S_OK;
}

// Extension cleanup
extern "C" void CALLBACK DebugExtensionUninitialize()
{
}

// Extension notification
extern "C" void CALLBACK DebugExtensionNotify(ULONG Notify, ULONG64 Argument)
{
}

// Implementation
HRESULT CALLBACK agent_impl(PDEBUG_CLIENT Client, PCSTR Args)
{
    IDebugControl* control = GetControl(Client);
    if (!control)
        return E_FAIL;

    // Parse subcommand
    std::string args_str = Args ? Args : "";

    // Trim leading whitespace
    size_t start = args_str.find_first_not_of(" \t");
    if (start != std::string::npos)
        args_str = args_str.substr(start);

    // Extract subcommand
    std::string subcmd;
    std::string rest;
    size_t space = args_str.find(' ');
    if (space != std::string::npos)
    {
        subcmd = args_str.substr(0, space);
        rest = args_str.substr(space + 1);
        // Trim leading whitespace from rest
        size_t rest_start = rest.find_first_not_of(" \t");
        if (rest_start != std::string::npos)
            rest = rest.substr(rest_start);
    }
    else
    {
        subcmd = args_str;
    }

    // Handle subcommands
    if (subcmd.empty() || subcmd == "help")
    {
        control->Output(
            DEBUG_OUTPUT_NORMAL,
            "WinDbg Agent - Debugger server extension\n"
            "\n"
            "Usage: !agent <command> [args]\n"
            "\n"
            "Commands:\n"
            "  help                  Show this help\n"
            "  version               Show version information\n"
            "  http [bind_addr]      Start HTTP server for external tools (port auto-assigned)\n"
            "  mcp [bind_addr]       Start MCP server for MCP-compatible clients\n"
            "\n"
            "Examples:\n"
            "  !agent http                              (start HTTP server on localhost)\n"
            "  !agent http 0.0.0.0                      (start HTTP server on all interfaces)\n"
            "  !agent mcp                               (start MCP server on localhost)\n");
    }
    else if (subcmd == "version")
    {
        control->Output(DEBUG_OUTPUT_NORMAL, "WinDbg Agent v%d.%d.%d\n",
                        WINDBG_AGENT_VERSION_MAJOR, WINDBG_AGENT_VERSION_MINOR,
                        WINDBG_AGENT_VERSION_PATCH);
    }
    else if (subcmd == "http")
    {
        // Start HTTP server for external tool integration
        // Usage: !agent http [bind_addr]
        // bind_addr: "127.0.0.1" (default, localhost only) or "0.0.0.0" (all interfaces)
        windbg_agent::WinDbgClient dbg_client(Client);

        // Parse optional bind address
        std::string bind_addr = "127.0.0.1";
        if (!rest.empty())
        {
            bind_addr = rest;
            // Trim whitespace
            size_t start = bind_addr.find_first_not_of(" \t");
            size_t end = bind_addr.find_last_not_of(" \t");
            if (start != std::string::npos)
                bind_addr = bind_addr.substr(start, end - start + 1);
        }

        if (bind_addr != "127.0.0.1")
        {
            control->Output(DEBUG_OUTPUT_WARNING,
                "WARNING: Binding to non-loopback address '%s'. "
                "The server has no authentication.\n", bind_addr.c_str());
        }

        // Get target state
        std::string target = dbg_client.GetTargetName();
        std::string state = dbg_client.GetTargetState();
        ULONG pid = dbg_client.GetProcessId();

        // Create exec callback - executes debugger commands and echoes to console
        windbg_agent::DmlOutput dml(control);
        windbg_agent::ExecCallback exec_cb = [&dbg_client, &dml](const std::string& command) -> std::string
        {
            dml.OutputCommand(command.c_str());
            std::string result = dbg_client.ExecuteCommand(command);
            dml.OutputCommandResult(result.c_str());
            return result;
        };

        // Start the HTTP server (OS assigns port)
        static windbg_agent::HttpServer http_server;
        if (http_server.is_running())
        {
            control->Output(DEBUG_OUTPUT_ERROR,
                            "HTTP server already running. Stop it before starting a new one.\n");
            control->Release();
            return E_FAIL;
        }
        int actual_port = http_server.start(exec_cb, bind_addr);
        if (actual_port <= 0)
        {
            control->Output(DEBUG_OUTPUT_ERROR,
                            "Failed to start HTTP server.\n");
            control->Release();
            return E_FAIL;
        }
        std::string url = "http://" + http_server.bind_addr() + ":" + std::to_string(http_server.port());

        // Format and output HTTP server info
        std::string http_info =
            windbg_agent::format_http_info(target, pid, state, url);
        control->Output(DEBUG_OUTPUT_NORMAL, "%s\n", http_info.c_str());

        // Copy to clipboard
        if (windbg_agent::copy_to_clipboard(http_info))
        {
            control->Output(DEBUG_OUTPUT_NORMAL, "[Copied to clipboard]\n");
        }

        control->Output(DEBUG_OUTPUT_NORMAL, "Press Ctrl+C to stop HTTP server.\n");

        // Set up interrupt check - stop server when user presses Ctrl+C
        http_server.set_interrupt_check([&dbg_client]() {
            return dbg_client.IsInterrupted();
        });

        // Block until server stops (user presses Ctrl+C or sends /shutdown)
        http_server.wait();
        control->Output(DEBUG_OUTPUT_NORMAL, "HTTP server stopped.\n");
    }
    else if (subcmd == "mcp")
    {
        // Start MCP server for MCP-compatible clients
        // Usage: !agent mcp [bind_addr]
        // bind_addr: "127.0.0.1" (default, localhost only) or "0.0.0.0" (all interfaces)
        windbg_agent::WinDbgClient dbg_client(Client);

        // Parse optional bind address
        std::string bind_addr = "127.0.0.1";
        if (!rest.empty())
        {
            bind_addr = rest;
            size_t start = bind_addr.find_first_not_of(" \t");
            size_t end = bind_addr.find_last_not_of(" \t");
            if (start != std::string::npos)
                bind_addr = bind_addr.substr(start, end - start + 1);
        }

        if (bind_addr != "127.0.0.1")
        {
            control->Output(DEBUG_OUTPUT_WARNING,
                "WARNING: Binding to non-loopback address '%s'. "
                "The server has no authentication.\n", bind_addr.c_str());
        }

        // Port 0 lets the MCP server pick a free port
        int port = 0;

        // Get target state
        std::string target = dbg_client.GetTargetName();
        std::string state = dbg_client.GetTargetState();
        ULONG pid = dbg_client.GetProcessId();

        // Create exec callback - executes debugger commands and echoes to console
        windbg_agent::DmlOutput dml(control);
        windbg_agent::ExecCallback exec_cb = [&dbg_client, &dml](const std::string& command) -> std::string
        {
            dml.OutputCommand(command.c_str());
            std::string result = dbg_client.ExecuteCommand(command);
            dml.OutputCommandResult(result.c_str());
            return result;
        };

        // Start the MCP server
        static windbg_agent::MCPServer mcp_server;
        if (mcp_server.is_running())
        {
            control->Output(DEBUG_OUTPUT_ERROR,
                            "MCP server already running. Stop it before starting a new one.\n");
            control->Release();
            return E_FAIL;
        }
        int actual_port = mcp_server.start(port, exec_cb, bind_addr);
        if (actual_port <= 0)
        {
            control->Output(DEBUG_OUTPUT_ERROR,
                            "Failed to start MCP server.\n");
            control->Release();
            return E_FAIL;
        }
        std::string url = "http://" + bind_addr + ":" + std::to_string(actual_port);

        // Format and output MCP server info
        std::string mcp_info =
            windbg_agent::format_mcp_info(target, pid, state, url);
        control->Output(DEBUG_OUTPUT_NORMAL, "%s\n", mcp_info.c_str());

        // Copy to clipboard
        if (windbg_agent::copy_to_clipboard(mcp_info))
        {
            control->Output(DEBUG_OUTPUT_NORMAL, "[Copied to clipboard]\n");
        }

        control->Output(DEBUG_OUTPUT_NORMAL, "Press Ctrl+C to stop MCP server.\n");

        // Set up interrupt check - stop MCP server when user presses Ctrl+C
        mcp_server.set_interrupt_check([&dbg_client]() {
            return dbg_client.IsInterrupted();
        });

        // Block until server stops (user presses Ctrl+C)
        mcp_server.wait();
        control->Output(DEBUG_OUTPUT_NORMAL, "MCP server stopped.\n");
    }
    else
    {
        control->Output(DEBUG_OUTPUT_ERROR, "Unknown subcommand: %s\n", subcmd.c_str());
        control->Output(DEBUG_OUTPUT_NORMAL, "Use '!agent help' for usage information.\n");
    }

    control->Release();
    return S_OK;
}

// !agent command - main entry point
extern "C" HRESULT CALLBACK agent(PDEBUG_CLIENT Client, PCSTR Args)
{
    return agent_impl(Client, Args);
}
