// Read-only local validation report viewer. No input collection, configuration,
// process launching, database, or filesystem mutation endpoints.
#include <winsock2.h>
#include "pulsetrace/probe_report.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
std::atomic<bool> stopping{};
BOOL WINAPI control(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT || signal == CTRL_CLOSE_EVENT) {
        stopping.store(true); return TRUE;
    }
    return FALSE;
}
class Winsock {
public:
    Winsock() { WSADATA data{}; if (WSAStartup(MAKEWORD(2, 2), &data)) throw std::runtime_error("WSAStartup failed"); }
    ~Winsock() { WSACleanup(); }
};
class Socket {
public:
    explicit Socket(SOCKET value) : value_(value) {}
    ~Socket() { if (value_ != INVALID_SOCKET) closesocket(value_); }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    SOCKET get() const { return value_; }
private:
    SOCKET value_;
};
std::string read_file(const std::filesystem::path& path, std::size_t limit) {
    if (!std::filesystem::is_regular_file(path)) throw std::runtime_error("File not available");
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        throw std::runtime_error("Links are not served");
    const auto size = std::filesystem::file_size(path);
    if (size > limit) throw std::runtime_error("File exceeds viewer size limit");
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read file");
    std::string text(static_cast<std::size_t>(size), '\0');
    input.read(text.data(), static_cast<std::streamsize>(size));
    if (input.gcount() != static_cast<std::streamsize>(size)) throw std::runtime_error("Report is still being written");
    return text;
}
bool valid_report(std::string_view name) {
    const auto prefix = name.starts_with("mouse-probe-") ? std::string_view("mouse-probe-") : std::string_view("mouse-replay-");
    if (!name.starts_with(prefix) || !name.ends_with(".json") || name.size() <= prefix.size() + 5) return false;
    const auto digits = name.substr(prefix.size(), name.size() - prefix.size() - 5);
    return std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; });
}
std::string report_index(const std::filesystem::path& data) {
    std::vector<std::pair<std::string, std::uintmax_t>> files;
    for (const auto& entry : std::filesystem::directory_iterator(data)) {
        const auto name = entry.path().filename().string();
        if (valid_report(name) && entry.is_regular_file() && !entry.is_symlink()) files.emplace_back(name, entry.file_size());
    }
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
        return a.first.substr(a.first.find_last_of('-') + 1) > b.first.substr(b.first.find_last_of('-') + 1);
    });
    std::ostringstream json;
    json << "{\"service\":\"PulseTrace report viewer\",\"read_only\":true,\"capture_active\":false,\"reports\":[";
    std::size_t count{};
    for (const auto& [name, bytes] : files) {
        if (count == 200) break;
        json << (count++ ? "," : "") << "{\"name\":\"" << name << "\",\"kind\":\""
             << (name.starts_with("mouse-probe-") ? "capture" : "replay") << "\",\"bytes\":" << bytes << '}';
    }
    json << "]}"; return json.str();
}
void respond(SOCKET client, int status, std::string_view type, const std::string& body) {
    const char* reason = status == 200 ? "OK" : status == 403 ? "Forbidden" : status == 404 ? "Not Found" :
                         status == 405 ? "Method Not Allowed" : status == 431 ? "Request Header Fields Too Large" : "Bad Request";
    std::ostringstream headers;
    headers << "HTTP/1.1 " << status << ' ' << reason << "\r\nContent-Type: " << type
            << "\r\nContent-Length: " << body.size() << "\r\nConnection: close\r\nCache-Control: no-store\r\n"
            << "X-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\n"
            << "Cross-Origin-Resource-Policy: same-origin\r\nX-Frame-Options: DENY\r\n"
            << "Content-Security-Policy: default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self'; object-src 'none'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'\r\n\r\n";
    const auto send_all = [client](std::string_view data) {
        while (!data.empty()) {
            const auto sent = send(client, data.data(), static_cast<int>(std::min<std::size_t>(data.size(), 65536)), 0);
            if (sent <= 0) return false;
            data.remove_prefix(static_cast<std::size_t>(sent));
        }
        return true;
    };
    if (send_all(headers.str())) send_all(body);
}
std::string lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
std::string trim(std::string text) {
    const auto first = text.find_first_not_of(" \t"), last = text.find_last_not_of(" \t");
    return first == std::string::npos ? "" : text.substr(first, last - first + 1);
}
void serve(SOCKET client, unsigned port, const std::filesystem::path& data, const std::filesystem::path& web) {
    const DWORD timeout = 1500;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    std::string request;
    std::array<char, 2048> chunk{};
    while (request.find("\r\n\r\n") == std::string::npos) {
        const int n = recv(client, chunk.data(), static_cast<int>(chunk.size()), 0);
        if (n <= 0) return;
        request.append(chunk.data(), static_cast<std::size_t>(n));
        if (request.size() > 8192) { respond(client, 431, "text/plain; charset=utf-8", "Headers too large"); return; }
    }
    std::istringstream lines(request.substr(0, request.find("\r\n\r\n")));
    std::string line, method, target, version, extra;
    std::getline(lines, line);
    std::istringstream first(line);
    if (!(first >> method >> target >> version) || (first >> extra) || (version != "HTTP/1.1" && version != "HTTP/1.0")) {
        respond(client, 400, "text/plain", "Invalid request"); return;
    }
    std::map<std::string, std::string> headers;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto colon = line.find(':');
        if (colon == std::string::npos || colon == 0 || line.front() == ' ' || line.front() == '\t') {
            respond(client, 400, "text/plain", "Invalid header"); return;
        }
        const auto key = lower(line.substr(0, colon));
        if (!headers.emplace(key, trim(line.substr(colon + 1))).second) {
            respond(client, 400, "text/plain", "Duplicate header"); return;
        }
    }
    const auto expected_host = "127.0.0.1:" + std::to_string(port);
    if (headers["host"] != expected_host ||
        (headers.contains("origin") && headers["origin"] != "http://" + expected_host) ||
        (headers.contains("sec-fetch-site") && headers["sec-fetch-site"] != "same-origin" && headers["sec-fetch-site"] != "none")) {
        respond(client, 403, "text/plain", "Only same-origin loopback requests are allowed"); return;
    }
    if (method != "GET") { respond(client, 405, "text/plain", "Read-only viewer"); return; }
    if (headers.contains("transfer-encoding") || (headers.contains("content-length") && headers["content-length"] != "0")) {
        respond(client, 400, "text/plain", "Request bodies are not supported"); return;
    }
    try {
        if (target == "/api/reports") respond(client, 200, "application/json; charset=utf-8", report_index(data));
        else if (target.starts_with("/api/report?name=")) {
            const auto name = target.substr(17);
            if (!valid_report(name)) { respond(client, 400, "text/plain", "Invalid report name"); return; }
            respond(client, 200, "application/json; charset=utf-8", read_file(data / name, 32 * 1024 * 1024));
        } else if (target == "/" || target == "/index.html") {
            respond(client, 200, "text/html; charset=utf-8", read_file(web / "index.html", 512 * 1024));
        } else if (target == "/app.js") respond(client, 200, "text/javascript; charset=utf-8", read_file(web / "app.js", 512 * 1024));
        else if (target == "/styles.css") respond(client, 200, "text/css; charset=utf-8", read_file(web / "styles.css", 512 * 1024));
        else respond(client, 404, "text/plain", "Not found");
    } catch (const std::exception&) { respond(client, 404, "text/plain", "Report or asset unavailable; wait for capture to finish and refresh"); }
}
unsigned parse_port(std::string_view text) {
    unsigned value{};
    const auto converted = std::from_chars(text.data(), text.data() + text.size(), value);
    if (converted.ec != std::errc{} || converted.ptr != text.data() + text.size() || value < 1024 || value > 65535)
        throw std::invalid_argument("Port must be 1024..65535");
    return value;
}
}
int main(int argc, char** argv) {
    try {
        unsigned preferred = 8765; bool fallback = true;
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg = argv[i];
            if (arg == "--help") { std::cout << "mouse_dashboard [--port 8765] [--strict-port]\nRead-only EXE/data reports; no input monitoring. Uses next free port (up to +20) when occupied. Ctrl+C stops.\n"; return 0; }
            if (arg == "--port" && i + 1 < argc) preferred = parse_port(argv[++i]);
            else if (arg == "--strict-port") fallback = false;
            else throw std::invalid_argument("Unknown or incomplete argument");
        }
        const auto data = pulsetrace::executable_data_directory(), web = data.parent_path() / "web";
        (void)read_file(web / "index.html", 512 * 1024); // Fail before announcing a broken page.
        Winsock winsock;
        Socket listener(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
        if (listener.get() == INVALID_SOCKET) throw std::runtime_error("Cannot create listener");
        const BOOL exclusive = TRUE;
        if (setsockopt(listener.get(), SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)))
            throw std::runtime_error("Cannot enable exclusive port binding");
        unsigned port = preferred;
        for (;;) {
            sockaddr_in address{}; address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); address.sin_port = htons(static_cast<u_short>(port));
            if (bind(listener.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != SOCKET_ERROR) break;
            const auto error = WSAGetLastError();
            if (!fallback || port >= preferred + 20 || port == 65535 || (error != WSAEADDRINUSE && error != WSAEACCES))
                throw std::runtime_error("Cannot bind requested loopback port; Winsock=" + std::to_string(error));
            std::cerr << "Port " << port << " unavailable; trying " << port + 1 << ".\n"; ++port;
        }
        if (listen(listener.get(), SOMAXCONN) == SOCKET_ERROR) throw std::runtime_error("listen failed");
        if (!SetConsoleCtrlHandler(control, TRUE)) throw std::runtime_error("Cannot register console control");
        std::cout << "PulseTrace report viewer: http://127.0.0.1:" << port << "/\nRead-only reports. Input monitoring is not active in this process.\n" << std::flush;
        while (!stopping.load()) {
            fd_set ready; FD_ZERO(&ready); FD_SET(listener.get(), &ready);
            timeval wait{1, 0};
            const auto selected = select(0, &ready, nullptr, nullptr, &wait);
            if (selected == SOCKET_ERROR) throw std::runtime_error("select failed");
            if (!selected) continue;
            Socket client(accept(listener.get(), nullptr, nullptr));
            if (client.get() != INVALID_SOCKET) serve(client.get(), port, data, web);
        }
        SetConsoleCtrlHandler(control, FALSE);
        return 0;
    } catch (const std::exception& error) { std::cerr << "Error: " << error.what() << '\n'; return 1; }
}
