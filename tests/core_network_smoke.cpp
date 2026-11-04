#include "pulsetrace/core_service.hpp"
#include "pulsetrace/probe_report.hpp"
#include <future>
#include <iostream>
#include <fstream>
using namespace pulsetrace;
Json request(CoreService &core, const char *type, Json params = Json::object()) {
    auto reply = std::make_shared<std::promise<Json>>();
    auto future = reply->get_future();
    core.request({{"type", type}, {"request_id", 1}, {"params", params}},
                 [reply](Json r) { reply->set_value(std::move(r)); });
    if (future.wait_for(std::chrono::seconds(15)) != std::future_status::ready)
        throw std::runtime_error("Core network timeout");
    auto r = future.get();
    if (!r.value("ok", false))
        throw std::runtime_error(r.dump());
    return r["data"];
}
int main() {
    try {
        const auto dir =
            executable_data_directory() / "tests" / ("network-core-" + std::to_string(utc_now_ms()));
        std::filesystem::create_directories(dir);
        CoreService core(dir);
        core.start();
        request(core, "set_monitoring", {{"enabled", true}});
        std::this_thread::sleep_for(std::chrono::milliseconds(3500));
        request(core, "set_monitoring", {{"enabled", false}});
        const auto m = utc_now_ms() / 60000;
        auto before = request(core, "get_statistics",
                              {{"from", m - 1}, {"to", m + 2}, {"bucket", 1}})["protocol_samples"];
        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        auto after = request(core, "get_statistics",
                             {{"from", m - 1}, {"to", m + 2}, {"bucket", 1}})["protocol_samples"];
        if (before != after)
            throw std::runtime_error("Paused protocol samples changed");
        const auto exported = request(core, "export_csv", {{"from", m - 1}, {"to", m + 2}});
        std::ifstream csv(dir / exported["protocol_file"].get<std::string>());
        std::string contents((std::istreambuf_iterator<char>(csv)), {});
        if (contents.find("source,basis,quality") == std::string::npos ||
            contents.find("reference_validated") == std::string::npos)
            throw std::runtime_error("Protocol export lost quality metadata");
        core.stop();
        if (!core.status().value("storage_error", "").empty())
            throw std::runtime_error("Final network save failed");
        DatabaseManager db(dir / "monitor.db");
        if (db.protocol_samples(m - 1, m + 2, 1) != after)
            throw std::runtime_error("Protocol live/database mismatch");
        MonthlyTrafficStore months(dir / "network-months");
        const auto archived=months.query("all")["totals"], original=db.traffic_summary();
        for(const char *key:{"upload_bytes","download_bytes","ipv4_up","ipv4_down","ipv6_up","ipv6_down"})
            if(archived[key]!=original[key])throw std::runtime_error(std::string("Real monthly archive mismatch: ")+key);
        std::cout << "Core physical network pause/persistence regression passed; real sample buckets="
                  << after.size() << " (empty is not proof of capture accuracy)\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
