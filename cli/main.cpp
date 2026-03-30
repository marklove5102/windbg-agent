#include <iostream>
#include <string>
#include <cstdlib>

#include <httplib.h>
#include <nlohmann/json.hpp>

// Standalone HTTP client for windbg_agent HTTP server.
//
// TODO: Evolve into a standalone headless debugger that hosts dbgeng directly.
// See comments in previous revision for the full vision.

void print_usage() {
    std::cerr << "Usage: windbg_agent.exe [--url=URL] <command> [args]\n\n";
    std::cerr << "Commands:\n";
    std::cerr << "  exec <cmd>       Run debugger command, return raw output\n";
    std::cerr << "  status           Check server status\n";
    std::cerr << "  shutdown         Stop HTTP server\n\n";
    std::cerr << "Environment:\n";
    std::cerr << "  WINDBG_AGENT_URL     HTTP server URL (default: http://127.0.0.1:9999)\n";
}

std::string get_url(int argc, char* argv[]) {
    // Priority: --url=X flag > WINDBG_AGENT_URL env > default
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg.rfind("--url=", 0) == 0) {
            return arg.substr(6);
        }
    }
    if (const char* env = std::getenv("WINDBG_AGENT_URL")) {
        return env;
    }
    return "http://127.0.0.1:9999";
}

class HttpClient {
public:
    explicit HttpClient(const std::string& url) : url_(url) {
        client_ = std::make_unique<httplib::Client>(url);
        client_->set_read_timeout(120, 0);
        client_->set_connection_timeout(5, 0);
    }

    std::string exec(const std::string& cmd) {
        nlohmann::json body = {{"command", cmd}};
        auto res = client_->Post("/exec", body.dump(), "application/json");

        if (!res) {
            throw std::runtime_error("Connection failed - is HTTP server running?");
        }
        if (res->status != 200) {
            auto json = nlohmann::json::parse(res->body);
            throw std::runtime_error(json.value("error", "Request failed"));
        }

        auto json = nlohmann::json::parse(res->body);
        return json.value("output", "");
    }

    std::string status() {
        auto res = client_->Get("/status");
        if (!res) {
            throw std::runtime_error("Connection failed - is HTTP server running?");
        }
        return res->body;
    }

    void shutdown() {
        auto res = client_->Post("/shutdown", "", "application/json");
        if (!res) {
            throw std::runtime_error("Connection failed - is HTTP server running?");
        }
    }

private:
    std::string url_;
    std::unique_ptr<httplib::Client> client_;
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    std::string url = get_url(argc, argv);

    // Find command index (skip --url if present)
    int cmd_idx = 1;
    if (std::string(argv[1]).rfind("--url=", 0) == 0) {
        cmd_idx = 2;
    }

    if (cmd_idx >= argc) {
        print_usage();
        return 1;
    }

    std::string command = argv[cmd_idx];

    // Collect remaining args as the command
    std::string args;
    for (int i = cmd_idx + 1; i < argc; i++) {
        if (!args.empty()) args += " ";
        args += argv[i];
    }

    try {
        HttpClient client(url);

        if (command == "exec") {
            if (args.empty()) {
                std::cerr << "Error: exec requires a command\n";
                return 1;
            }
            std::cout << client.exec(args);
            return 0;
        }
        else if (command == "status") {
            std::cout << client.status() << "\n";
            return 0;
        }
        else if (command == "shutdown") {
            client.shutdown();
            std::cout << "HTTP server stopped.\n";
            return 0;
        }
        else {
            std::cerr << "Unknown command: " << command << "\n";
            print_usage();
            return 1;
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        std::cerr << "URL: " << url << "\n";
        return 1;
    }

    return 0;
}
