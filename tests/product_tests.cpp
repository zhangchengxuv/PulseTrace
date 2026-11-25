#include "pulsetrace/core_service.hpp"
#include "pulsetrace/probe_report.hpp"
#include "pulsetrace/protocol.hpp"
#include "pulsetrace/statistics_buckets.hpp"
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
        {
            DYNAMIC_TIME_ZONE_INFORMATION zone{};
            zone.Bias = -345;
            StatisticsBuckets day(1440, zone), hour(60, zone);
            require(day.start(1094) == -345 && day.start(1095) == 1095,
                    "local midnight uses the full UTC offset");
            require(hour.start(74) == 15 && hour.start(75) == 75,
                    "local hour follows quarter-hour zone offset");
            zone.Bias = 300;
            zone.DaylightBias = -60;
            zone.DaylightDate = {0, 3, 0, 2, 2, 0, 0, 0};
            zone.StandardDate = {0, 11, 0, 1, 2, 0, 0, 0};
            StatisticsBuckets dst_day(1440, zone), dst_hour(60, zone);
            require(dst_day.start(29550479) == 29549100 && dst_day.start(29550480) == 29550480,
                    "spring-forward day is 23 elapsed hours");
            require(dst_hour.start(29891850) == 29891820 && dst_hour.start(29891910) == 29891880,
                    "fall-back keeps the two occurrences of 01:00 separate");
        }
        for (const std::wstring argument : {L"", L"D:\\", L"C:\\a b\\", L"a\\\"b"}) {
            int argc = 0;
            const auto command = L"PulseTrace.exe " + quote_windows_argument(argument);
            auto argv = CommandLineToArgvW(command.c_str(), &argc);
            const bool roundtrip = argv && argc == 2 && argument == argv[1];
            if (argv)
                LocalFree(argv);
            require(roundtrip, "Windows argument quoting roundtrip");
        }

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
            std::filesystem::create_directories(dir/"style-config");CoreService styles(dir/"style-config");styles.start();rpc(styles,"set_config",{{"taskbar_unit","kib"},{"taskbar_font_size",12},{"taskbar_precision",3},{"taskbar_offset_x",-8},{"taskbar_upload_color","#123456"}});rejects([&]{rpc(styles,"set_config",{{"taskbar_font_size",99}});},"invalid widget font size rejected");rejects([&]{rpc(styles,"set_config",{{"taskbar_upload_color","bad"}});},"invalid widget color rejected");styles.stop();
            CoreService restored(dir/"style-config");require(restored.status()["config"]["taskbar_unit"]=="kib"&&restored.status()["config"]["taskbar_font_size"]==12,"taskbar style persists across restart");
        }
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
            require(db.history_bounds()["first_minute"]==59,"bounds cover saved history");
            require(db.query(0,30000000,1440)[0]["value"]==9,"all-data query reaches records older than 366 days");
            require(parse_message(R"({"type":"get_statistics","request_id":1,"params":{"from":0,"to":30000000,"bucket":1440}})")["type"]=="get_statistics","all-data bridge accepts epoch sentinel");
            rejects([&]{parse_message(R"({"type":"get_statistics","request_id":1,"params":{"from":1,"to":2,"time_zone":"foreign"}})");},"only local and UTC time zones are accepted");
            require(parse_message(R"({"type":"get_activity","request_id":1,"params":{"from":1,"to":3,"time_zone":"local","view":"year","metric":"active"}})")["type"]=="get_activity",
                    "activity bridge accepts calendar metric and a civil time zone");
            rejects([&]{parse_message(R"({"type":"get_activity","request_id":1,"params":{"from":1,"to":2,"time_zone":"UTC","view":"year","metric":"title"}})");},
                    "activity projection never accepts a metadata field");
            rejects([&]{parse_message(R"({"type":"get_activity","request_id":1,"params":{"from":0,"to":900000,"time_zone":"UTC","view":"rhythm"}})");},
                    "activity bridge bounds the indexed minute scan");
            for (int bucket : {60, 1440}) {
                StatisticsBuckets local(bucket, true);
                const auto rows = db.query(59, 60, bucket, "", 0, true);
                require(rows.size() == 1 && rows[0]["minute"] == local.start(59) && rows[0]["value"] == 6,
                        "SQLite local grouping preserves minute-range filtering and totals");
            }
            rejects([&]{parse_message(R"({"type":"get_statistics","request_id":1,"params":{"from":1,"to":30000000}})");},"custom range remains bounded");
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
            const auto legacy = dir / "legacy-v2.db";
            sqlite3_open(legacy.string().c_str(), &raw);
            sqlite3_exec(raw,
                         "CREATE TABLE counts(session TEXT,minute INTEGER,module TEXT,metric TEXT,value "
                         "INTEGER,PRIMARY KEY(session,minute,module,metric));INSERT INTO counts "
                         "VALUES('old',60,'network','interface_upload_bytes',777);PRAGMA user_version=2;",
                         nullptr, nullptr, nullptr);
            sqlite3_close(raw);
            raw = nullptr;
            DatabaseManager upgraded(legacy);
            require(upgraded.query(60, 61, 1)[0]["value"] == 777,
                    "v2 migration preserves old interface totals");
            require(upgraded.protocol_samples(60, 61, 1).empty(),
                    "legacy bytes never invented as protocol split");
            Json record = {{"minute", 60},
                           {"interface_id", "physical-test"},
                           {"source", "etw_ndis"},
                           {"basis", "ip_total_length"},
                           {"quality", "observed_unverified_reference"},
                           {"ipv4_up", 100},
                           {"ipv4_down", 200},
                           {"ipv6_up", 300},
                           {"ipv6_down", 400},
                           {"accepted", 4},
                           {"rejected", 0},
                           {"unsupported", 0},
                           {"events_lost", 0},
                           {"buffers_lost", 0}};
            Json row = {{"minute", 60},
                        {"module", "network_ip"},
                        {"metric", "physical-test/ipv4_up"},
                        {"value", 100},
                        {"ip_record", record}};
            upgraded.save("new", Json::array({row}));
            upgraded.save("new", Json::array({row}));
            require(upgraded.protocol_samples(60, 61, 1)[0]["ipv4_up"] == 100,
                    "protocol snapshot retries idempotent");
            row["minute"] = 61;
            row["value"] = 200;
            row["ip_record"]["minute"] = 61;
            row["ip_record"]["ipv4_up"] = 200;
            row["ip_record"]["quality"] = "incomplete_observation";
            row["ip_record"]["rejected"] = 1;
            upgraded.save("new", Json::array({row}));
            auto hour = upgraded.protocol_samples(60, 62, 60);
            require(hour.size() == 1 && hour[0]["ipv4_up"] == 300 &&
                        hour[0]["quality"] == "incomplete_observation",
                    "protocol hourly totals preserve worst quality");
            require(upgraded.protocol_samples(60, 62, 60, "new", 60).empty(),
                    "protocol live overlay exclusion");
            upgraded.prune(61);
            require(upgraded.protocol_samples(60, 62, 1).size() == 1, "protocol retention");
            upgraded.clear();
            require(upgraded.protocol_samples(60, 62, 1).empty(), "protocol clear");
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
        rejects([] {parse_message(R"({"type":"selftest_snapshot","request_id":1,"params":{"phase":"../data"}})");},"selftest snapshot path injection rejected");
        rejects([] {parse_message(R"({"type":"selftest_snapshot","request_id":1,"params":{"phase":"top","metrics":{"path":1}}})");},"selftest metrics reject unknown fields");
        rejects([] {parse_message(R"({"type":"selftest_snapshot","request_id":1,"params":{"phase":"top","path":"other"}})");},"selftest snapshot filename is not frontend-controlled");
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
        require(parse_message(R"({"type":"set_autostart","request_id":4,"params":{"enabled":true,"method":"task"}})")["params"]["method"]=="task",
                "task scheduler may be selected explicitly");
        rejects([]{parse_message(R"({"type":"set_autostart","request_id":4,"params":{"enabled":true,"method":"other"}})");},
                "invalid startup methods rejected");
        {
            const auto png=std::string("iVBORw0KGgo")+std::string(70001,'A');
            const Json message={{"type","save_summary_png"},{"request_id",7},{"params",{{"png",png}}}};
            require(parse_message(message.dump())==message,"PNG export accepts a bounded larger message");
            auto other=message;other["type"]="ping";
            rejects([&]{parse_message(other.dump());},"other messages retain the original request limit");
            auto oversized=message;oversized["params"]["png"]=png+std::string(summary_png_base64_limit,'A');
            rejects([&]{parse_message(oversized.dump());},"PNG export also has a hard size limit");
        }
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
            const auto fixture=dir/"protocol-identities";std::filesystem::create_directories(fixture);
            const auto minute=utc_now_ms()/60000-5;
            {DatabaseManager seed(fixture/"monitor.db");
                for(const auto &[source,basis]:{std::pair{"capture_a","ip_lengths"},std::pair{"capture_b","ip_lengths"},std::pair{"capture_a","other_lengths"}}){
                    Json record={{"minute",minute},{"interface_id","fixture"},{"source",source},{"basis",basis},{"quality","observed_unverified_reference"},{"ipv4_up",10},{"ipv4_down",20},{"ipv6_up",30},{"ipv6_down",40},{"accepted",1},{"rejected",0},{"unsupported",0},{"events_lost",0},{"buffers_lost",0}};
                    seed.save("fixture",Json::array({{{"minute",minute},{"module","network_ip"},{"metric","fixture/quality_state"},{"value",0},{"ip_record",record}}}));
                }
            }
            CoreService identity_core(fixture);identity_core.start();
            for(int bucket:{1,60,1440}){
                const auto samples=rpc(identity_core,"get_statistics",{{"from",minute},{"to",minute+1},{"bucket",bucket}})["protocol_samples"];
                require(samples.size()==3,"core range query preserves capture source and basis identities at every granularity");
                for(const auto &record:samples)require(record["ipv4_up"]==10&&record["accepted"]==1,"incompatible byte observations are never merged by core overlay");
                StatisticsBuckets local(bucket,true);
                const auto local_report=rpc(identity_core,"get_statistics",{{"from",minute},{"to",minute+1},{"bucket",bucket},{"time_zone","local"}});
                require(local_report["time_zone"]=="local"&&local_report["protocol_samples"].size()==3,"local protocol query preserves observation identity");
                for(const auto &record:local_report["protocol_samples"])
                    require(record["minute"]==local.start(minute)&&record["ipv4_up"]==10,"protocol samples retain the exact local bucket start");
            }
            identity_core.stop();
        }
        {
            const auto fixture = dir / "activity";
            std::filesystem::create_directories(fixture);
            const auto minute = (utc_now_ms() / 60000 - 300) / 60 * 60 + 7;
            const auto count = [](auto at, const char *module, const char *metric, int value) {
                return Json{{"minute", at}, {"module", module}, {"metric", metric}, {"value", value}};
            };
            {
                DatabaseManager seed(fixture / "monitor.db");
                seed.save("fixture", Json::array({count(minute, "keyboard", "30", 12),
                    count(minute, "repeat", "30", 500), count(minute, "mouse", "left", 3),
                    count(minute, "mouse", "double/left", 2), count(minute, "mouse", "wheel_up_events", 7),
                    count(minute, "network", "interface_upload_bytes", 100),
                    count(minute, "network", "interface_download_bytes", 250),
                    count(minute, "network", "bytes/other", 5000),
                    count(minute + 60, "coverage", "observed_ms", 60000),
                    count(minute + 120, "network", "interface_download_bytes", 0)}));
                seed.save_usage("fixture", Json::array({{{"minute", minute}, {"key", "FixtureApp"},
                    {"path", "fixture.exe"}, {"name", "Fixture"}, {"foreground_ms", 60000}, {"active_ms", 5000}}}));
                seed.save_music("fixture", Json::array({{{"minute", minute}, {"key", "FixtureTrack"},
                    {"player", "qq"}, {"title", "Fixture Title"}, {"artist", "Fixture Artist"},
                    {"album", ""}, {"duration_ms", 180000}, {"played_ms", 15000}, {"plays", 0}}}));
                seed.save("overlay", Json::array({count(minute, "keyboard", "30", 99)}));
                const auto counts = seed.activity_counts(minute, minute + 121, 0, false, "overlay", minute).at(0);
                require(counts.keyboard == 12 && counts.mouse == 3 && counts.wheel == 7 &&
                        counts.coverage == 60000 && counts.network == 350, "bounded summary counts and live exclusion");
                const auto empty = seed.activity_counts(minute - 1, minute, 0, false, "fixture", minute).at(0);
                require(!empty.keyboard && !empty.coverage && !empty.network, "no rows leave null totals");
                require(seed.activity_duration(minute, minute+1, false, false, "fixture", minute).empty() &&
                        seed.activity_duration(minute, minute+1, true, false, "fixture", minute).empty(),
                        "duration projections exclude same-session live rows");
            }
            CoreService core(fixture);
            core.start();
            Json range = {{"from", minute}, {"to", minute + 121}, {"time_zone", "UTC"}, {"view", "rhythm"}};
            auto rhythm = rpc(core, "get_activity", range).at("rows");
            require(rhythm.size() == 2 && rhythm.at(0) == Json{{"minute", minute - 7}, {"value", 114}} &&
                    rhythm.at(1) == Json{{"minute", minute + 53}, {"value", 0}},
                    "hourly input separates zero from missing, without repeats, scroll or double clicks");
            range["view"] = "summary";
            const auto summary = rpc(core, "get_activity", range).at("totals");
            require(summary.at("keyboard") == 111 && summary.at("mouse") == 3 && summary.at("network") == 350,
                    "summary values are independent projections");
            range["view"] = "year";
            range["time_zone"] = "local";
            range["to"] = minute + 1;
            StatisticsBuckets local_day(1440, true);
            for (const auto &[metric, value] : {std::pair{"keyboard", 111}, std::pair{"mouse", 3},
                                               std::pair{"network", 350}, std::pair{"active", 5000}}) {
                range["metric"] = metric;
                const auto daily = rpc(core, "get_activity", range).at("rows");
                require(daily.size() == 1 && daily.at(0) == Json{{"minute",local_day.start(minute)},{"value",value}},
                        "only local daily totals cross the bridge");
            }
            range["metric"] = "music";
            require(rpc(core, "get_activity", range).at("rows").empty(), "music data stays hidden while disabled");
            rpc(core, "set_config", {{"music_enabled", true}});
            const auto music = rpc(core, "get_activity", range).at("rows");
            require(music.size() == 1 && music.at(0) == Json{{"minute", local_day.start(minute)}, {"value", 15000}},
                    "music metric returns duration without track metadata");
            core.stop();
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
            require(rpc(core,"get_status")["config"]["taskbar_background"]=="transparent","old/default configuration starts with a transparent widget");
            rpc(core, "set_monitoring", {{"enabled", true}});
            require(rpc(core, "get_status")["monitoring"].get<bool>(), "explicit start reflected in status");
            std::this_thread::sleep_for(std::chrono::milliseconds(2300));
            require(rpc(core, "get_status").value("heartbeat_ms", std::int64_t(0)) > 0,
                    "input heartbeat independent of UI");
            auto m = utc_now_ms() / 60000;
            auto stats = rpc(core, "get_statistics", {{"from", m - 1}, {"to", m + 1}, {"bucket", 1}});
            require(stats["rows"].is_array(), "live + database range query");
            rpc(core, "set_monitoring", {{"enabled", false}});
            require(!rpc(core, "get_status")["monitoring"].get<bool>(), "explicit pause reflected in status");
            rpc(core, "set_config", {{"theme", "dark"}, {"retention_days", 7}, {"flush_seconds", 5}});
            const auto session=rpc(core,"get_status")["session"];
            rpc(core,"set_config",{{"taskbar_background","subtle"}});
            rejects([&]{rpc(core,"set_config",{{"taskbar_background","opaque"}});},"unsupported widget background rejected");
            const auto after_background=rpc(core,"get_status");
            require(after_background["session"]==session&&!after_background["monitoring"].get<bool>()&&after_background["config"]["taskbar_background"]=="subtle","background preference preserves collector session, pause state and valid configuration");
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
            require(core.status()["config"]["taskbar_background"]=="subtle","widget background preference persists across restart");
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
