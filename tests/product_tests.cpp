#include "pulsetrace/core_service.hpp"
#include "pulsetrace/probe_report.hpp"
#include "pulsetrace/protocol.hpp"
#include <fstream>
#include <future>
#include <iostream>
#include <sqlite3.h>
#include <stdexcept>
#include <shellapi.h>
using namespace pulsetrace;
namespace {
int checks{};
void require(bool ok, const char *message) {
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F fn, const char *message) {
    bool threw = false;
    try {
        for (const std::wstring argument : {L"", L"D:\\", L"C:\\a b\\", L"a\\\"b"}) {
            int argc = 0;
            const auto command = L"PulseTrace.exe " + quote_windows_argument(argument);
            auto argv = CommandLineToArgvW(command.c_str(), &argc);
            const bool roundtrip = argv && argc == 2 && argument == argv[1];
            if (argv)
                LocalFree(argv);
            require(roundtrip, "Windows argument quoting roundtrip");
        }
        fn();
    } catch (...) {
        threw = true;
    }
    require(threw, message);
}
Json rpc(CoreService &core, const std::string &type, Json params = Json::object()) {
    auto promise = std::make_shared<std::promise<Json>>();
    auto future = promise->get_future();
    core.request({{"type", type}, {"request_id", 1u}, {"params", params}},
                 [promise](Json response) { promise->set_value(std::move(response)); });
    if (future.wait_for(std::chrono::seconds(8)) != std::future_status::ready)
        throw std::runtime_error("Core RPC timeout");
    auto response = future.get();
    if (!response.value("ok", false))
        throw std::runtime_error(response["error"]["message"].get<std::string>());
    return response["data"];
}
} // namespace
int main() {
    try {
        KeyboardMonitor keys;
        keys.consume(1, 30, 0, 65, 100);
        keys.consume(1, 30, 0, 65, 100);
        keys.consume(1, 30, 1, 65, 100);
        keys.consume(1, 30, 0, 65, 101);
        require(keys.total() == 2, "keyboard repeat excluded from press count");
        require(keys.minutes()[100 % 120].repeats[30] == 1, "repeat separately aggregated");
        keys.consume(1, 29, 0, 17, 101);
        keys.consume(1, 29, 2, 17, 101);
        keys.consume(2, 29, 0, 17, 101);
        require(keys.minutes()[101 % 120].presses[29] == 2 && keys.minutes()[101 % 120].presses[285] == 1,
                "devices and E0 modifiers isolated");
        keys.consume(1, 69, 4, 0x13, 101);
        keys.consume(1, 69, 4, 0x13, 101);
        require(keys.minutes()[101 % 120].presses[581] == 2, "Pause without break handled");
        auto total = keys.total();
        keys.consume(1, 42, 2, 16, 101);
        keys.consume(1, 0, 0, 65, 101);
        keys.consume(1, 255, 0, 255, 101);
        require(keys.total() == total, "fake/zero/overrun packets excluded");
        keys.reset_state();
        keys.consume(1, 30, 0, 65, 101);
        require(keys.total() == total + 1, "pause/reset clears held state");
        const auto dir = executable_data_directory() / "tests" / ("product-" + std::to_string(utc_now_ms()));
        std::filesystem::create_directories(dir);
        auto row = [](int minute, int value) {
            return Json{{"minute", minute}, {"module", "keyboard"}, {"metric", "30"}, {"value", value}};
        };
        {
            DatabaseManager db(dir / "test.db");
            db.save("a", Json::array({row(59, 2), row(60, 3)}));
            db.save("a", Json::array({row(59, 2), row(60, 3)}));
            require(db.query(59, 61, 1).size() == 2, "idempotent snapshot retry");
            require(db.query(59, 60, 1)[0]["value"] == 2, "half-open minute query");
            db.save("b", Json::array({row(59, 4)}));
            require(db.query(0, 61, 1440)[0]["value"] == 9, "multi-session rollup");
            require(db.query(59, 61, 1, "a", 60)[0]["value"] == 6,
                    "live-overlay exclusion preserves other sessions");
            rejects([&] { db.save("bad", Json::array({row(100, 9), Json{{"minute", 101}}})); },
                    "transaction malformed batch rejected");
            require(db.query(100, 102, 1).empty(), "transaction rollback atomic");
            db.export_csv(dir / "export.csv", 0, 61);
            require(std::filesystem::file_size(dir / "export.csv") > 30, "CSV export written");
            db.prune(60);
            require(db.query(0, 61, 1).size() == 1, "retention cleanup");
            rejects([&] { db.query(1, 1, 1); }, "invalid range rejected");
        }
        {
            DatabaseManager db(dir / "test.db");
            require(db.query(60, 61, 1)[0]["value"] == 3, "durability after close and reopen");
            Json events = Json::array({{{"sequence", 1},
                                        {"observed_ms", 3600000},
                                        {"kind", "observed"},
                                        {"interface_id", "123"},
                                        {"name", "Test adapter"}}});
            db.save("events", Json::array(), events);
            db.save("events", Json::array(), events);
            require(db.network_events(60, 61).size() == 1, "network event retry idempotent");
            db.clear();
            require(db.query(0, 61, 1).empty() && db.network_events(0, 61).empty(),
                    "explicit clear persisted");
        }
        {
            sqlite3 *raw{};
            sqlite3_open((dir / "newer.db").string().c_str(), &raw);
            sqlite3_exec(raw, "PRAGMA user_version=99", nullptr, nullptr, nullptr);
            sqlite3_close(raw);
            rejects([&] { DatabaseManager db(dir / "newer.db"); }, "future schema preserved and rejected");
        }
        rejects([&] { DatabaseManager db(dir); }, "directory cannot masquerade as DB");
        {
            const auto child = executable_data_directory().parent_path() / "database_crash_writer.exe";
            const auto file = dir / "crash.db";
            const auto command = L"\"" + child.wstring() + L"\" \"" + file.wstring() + L"\"";
            std::vector<wchar_t> command_line(command.begin(), command.end());
            command_line.push_back(0);
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            PROCESS_INFORMATION process{};
            require(CreateProcessW(child.c_str(), command_line.data(), nullptr, nullptr, FALSE,
                                   CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process) != FALSE,
                    "crash-recovery child launched");
            const auto wait = WaitForSingleObject(process.hProcess, 10000);
            DWORD code{};
            GetExitCodeProcess(process.hProcess, &code);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            require(wait == WAIT_OBJECT_0 && code == 0, "child exited without SQLite cleanup");
            sqlite3 *db{};
            require(sqlite3_open(file.string().c_str(), &db) == SQLITE_OK, "WAL reopened after abrupt exit");
            sqlite3_stmt *stmt{};
            sqlite3_prepare_v2(db, "SELECT COUNT(*),SUM(value) FROM recovery", -1, &stmt, nullptr);
            sqlite3_step(stmt);
            require(sqlite3_column_int(stmt, 0) == 1 && sqlite3_column_int(stmt, 1) == 1,
                    "committed row survives; uncommitted transaction rolled back");
            sqlite3_finalize(stmt);
            sqlite3_close(db);
        }
        require(parse_message(R"({"type":"ping","request_id":1,"params":{}})")["type"] == "ping",
                "JSON bridge valid envelope");
        rejects([] { parse_message(R"({"type":"ping","type":"exit","request_id":1,"params":{}})"); },
                "duplicate message fields rejected");
        rejects([] { parse_message(R"({"type":"system_command","request_id":1,"params":{}})"); },
                "arbitrary system command rejected");
        rejects([] { parse_message(R"({"type":"get_status","request_id":1,"params":{"path":"C:/"}})"); },
                "unknown parameter rejected");
        rejects(
            [] { parse_message(R"({"type":"set_monitoring","request_id":1,"params":{"enabled":"true"}})"); },
            "wrong bool type rejected");
        rejects(
            [] {
                std::string nested(30, '[');
                nested += "0";
                nested += std::string(30, ']');
                parse_message(nested);
            },
            "nested JSON bounded");
        rejects([] { parse_message(std::string(65537, ' ')); }, "message size bounded");
        {
            NetworkMonitor network;
            const auto sample = network.sample(utc_now_ms() / 60000, false, "");
            require(sample.status["interfaces"].is_array(), "real IP Helper interface enumeration");
            require(!sample.status["protocol_split_supported"].get<bool>(),
                    "protocol family totals never fabricated");
            require(sample.rows.empty() && sample.events.empty(),
                    "paused network sampling persists no usage");
        }
        {
            CoreService core(dir);
            core.start();
            bool ready = false;
            for (int i = 0; i < 20; ++i) {
                if (rpc(core, "get_status")["input_ready"]) {
                    ready = true;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            require(ready, "background Raw Input ready");
            require(!rpc(core, "get_status")["monitoring"].get<bool>(), "first launch defaults paused");
            rpc(core, "set_monitoring", {{"enabled", true}});
            std::this_thread::sleep_for(std::chrono::milliseconds(2300));
            require(rpc(core, "get_status").value("heartbeat_ms", std::int64_t(0)) > 0,
                    "input heartbeat independent of UI");
            auto m = utc_now_ms() / 60000;
            auto stats = rpc(core, "get_statistics", {{"from", m - 1}, {"to", m + 1}, {"bucket", 1}});
            require(stats["rows"].is_array(), "live + database range query");
            rpc(core, "set_monitoring", {{"enabled", false}});
            rpc(core, "set_config", {{"theme", "dark"}, {"retention_days", 7}, {"flush_seconds", 5}});
            rejects([&] { rpc(core, "set_config", {{"retention_days", 4294967326ULL}}); },
                    "overflowing retention rejected before narrowing");
            rejects([&] { rpc(core, "set_config", {{"flush_seconds", 4294967326ULL}}); },
                    "overflowing flush interval rejected before narrowing");
            require(rpc(core, "get_status")["config"]["retention_days"] == 7,
                    "unsigned JSON config integers accepted");
            rpc(core, "clear_data", {{"confirmation", "DELETE"}});
            std::this_thread::sleep_for(std::chrono::milliseconds(1100));
            require(
                rpc(core, "get_statistics", {{"from", m - 1}, {"to", m + 1}, {"bucket", 1}})["rows"].empty(),
                "clear does not resurrect buffered data");
            core.stop();
            require(core.status().value("storage_error", "").empty(), "final flush successful");
        }
        {
            CoreService core(dir);
            require(core.status()["config"]["theme"] == "dark" &&
                        core.status()["config"]["retention_days"] == 7,
                    "configuration recovered after restart");
        }
        {
            const auto broken = dir / "broken";
            std::filesystem::create_directories(broken);
            {
                std::ofstream config(broken / "config.json");
                config << "{\"theme\":\"invalid\"}";
            }
            rejects([&] { CoreService invalid(broken); }, "invalid persisted theme rejected");
            {
                std::ofstream config(broken / "config.json");
                config << "{\"retention_days\":4294967326}";
            }
            rejects([&] { CoreService invalid(broken); }, "overflowing persisted config rejected");
            {
                std::ofstream config(broken / "config.json");
                config << "{}";
            }
            std::filesystem::create_directories(broken / "monitor.db");
            CoreService core(broken);
            core.start();
            for (int i = 0; i < 30 && core.status().value("storage_error", "").empty(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            require(!rpc(core, "get_status").value("storage_error", "").empty(),
                    "storage startup failure remains diagnosable");
            require(!core.status().value("monitoring", true), "database failure pauses capture");
            core.stop();
        }
        {
            const auto locked = dir / "locked";
            std::filesystem::create_directories(locked);
            CoreService core(locked);
            core.start();
            for (int i = 0; i < 40 && core.status().value("last_flush_ms", std::int64_t(0)) == 0; ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            rpc(core, "set_monitoring", {{"enabled", true}});
            std::this_thread::sleep_for(std::chrono::milliseconds(2200));
            sqlite3 *blocker{};
            sqlite3_open((locked / "monitor.db").string().c_str(), &blocker);
            require(sqlite3_exec(blocker, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) == SQLITE_OK,
                    "external writer lock acquired");
            core.stop();
            require(!core.status().value("storage_error", "").empty() &&
                        core.status().value("pending_retry_rows", 0) > 0,
                    "failed final flush retains pending aggregates");
            sqlite3_exec(blocker, "ROLLBACK", nullptr, nullptr, nullptr);
            sqlite3_close(blocker);
            core.stop();
            require(core.status().value("storage_error", "").empty() &&
                        core.status().value("pending_retry_rows", -1) == 0,
                    "final flush can retry after external lock released");
            DatabaseManager db(locked / "monitor.db");
            const auto minute = utc_now_ms() / 60000;
            require(!db.query(minute - 1, minute + 1, 1).empty(), "retained aggregates durable after retry");
        }
        std::cout << checks << " product checks passed. Test artifacts: " << dir.string() << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
