#include "pulsetrace/campus.hpp"
#include "pulsetrace/core_service.hpp"
#include "pulsetrace/protocol.hpp"
#include <future>
#include <fstream>
#include <iostream>
#include <sqlite3.h>
using namespace pulsetrace;
namespace {
int checks{};
void check(bool value,const char *message){++checks;if(!value)throw std::runtime_error(message);}
Json rpc(CoreService &core,const char *type,Json params=Json::object()){
    auto promise=std::make_shared<std::promise<Json>>();auto future=promise->get_future();
    core.request({{"type",type},{"request_id",1},{"params",params}},[promise](Json value){promise->set_value(std::move(value));});
    if(future.wait_for(std::chrono::seconds(10))!=std::future_status::ready)throw std::runtime_error("Campus RPC timeout");
    auto response=future.get();if(!response.value("ok",false))throw std::runtime_error(response["error"]["message"]);return response["data"];
}
Json snapshot_rpc(CoreService &core,Json snapshot){
    auto promise=std::make_shared<std::promise<Json>>();auto future=promise->get_future();
    core.save_campus_snapshot(std::move(snapshot),[promise](Json value){promise->set_value(std::move(value));});
    if(future.wait_for(std::chrono::seconds(10))!=std::future_status::ready)throw std::runtime_error("Campus snapshot timeout");
    auto response=future.get();if(!response.value("ok",false))throw std::runtime_error(response["error"]["message"]);return response["data"];
}
void storage(const std::filesystem::path &root){
    bool invalid=false;try{normalize_campus_portal_url("http://");}catch(...){invalid=true;}check(invalid,"portal URL without host rejected");
    check(campus_origin("HTTPS://CAMPUS.EXAMPLE/usage")=="https://campus.example","scheme and origin normalization");
    Json legacy={{"balance",{{"selector","#balance"},{"kind","currency"},{"unit","CNY"},{"confidence",1}}}};normalize_campus_mapping(legacy);
    check(legacy["balance"]["mode"]=="auto","legacy selector rules migrate to automatic mode");
    DatabaseManager db(root/"storage.db");
    Json snapshot={{"observed_ms",100},{"portal_origin","https://campus.example"},{"portal_url","https://campus.example/usage"},
                   {"balance",12.5},{"used_bytes",1024},{"remaining_bytes",2048},{"quota_bytes",3072},{"fee",1.25},
                   {"cycle_text","2026-11"},{"quality","confirmed"},{"confidence",1.0},{"details",{{"mapped_fields",Json::array({"balance","used_bytes"})}}}};
    db.save_campus_snapshot(snapshot);
    const auto latest=db.campus_latest("https://campus.example");
    check(latest["balance"]==12.5&&latest["remaining_bytes"]==2048,"confirmed campus snapshot round trip");
    check(db.campus_latest("https://other.example").is_null(),"campus snapshots remain portal-specific");
}
void migration(const std::filesystem::path &root){
    const auto file=root/"v6.db";{DatabaseManager db(file);db.save("legacy",Json::array({{{"minute",1},{"module","keyboard"},{"metric","30"},{"value",7}}}));}
    sqlite3 *raw{};const auto name=file.u8string();check(sqlite3_open(reinterpret_cast<const char*>(name.c_str()),&raw)==SQLITE_OK,"open v6 fixture");
    check(sqlite3_exec(raw,"DROP TABLE campus_snapshots;PRAGMA user_version=6",nullptr,nullptr,nullptr)==SQLITE_OK,"create genuine v6 fixture");sqlite3_close(raw);
    {DatabaseManager db(file);check(db.migration_info()["from"]==6&&db.migration_info()["to"]==7,"v6 to v7 campus migration");
     check(db.query(1,2,1)[0]["value"]==7,"migration preserves existing counters");check(db.campus_latest().is_null(),"new campus history starts unknown");}
}
void core(const std::filesystem::path &root){
    parse_message(R"({"type":"open_campus_portal","request_id":1,"params":{}})");
    parse_message(R"({"type":"campus_pick_field","request_id":2,"params":{"field":"balance"}})");
    bool bad=false;try{parse_message(R"({"type":"campus_pick_field","request_id":3,"params":{"field":"password"}})");}catch(...){bad=true;}
    check(bad,"campus bridge rejects unknown field picker");
    CoreService service(root);service.start();
    auto status=rpc(service,"get_campus_status");
    check(status["enabled"]==false&&status["portal_url"]=="","campus monitoring defaults disabled and unconfigured");
    rpc(service,"set_config",{{"campus_enabled",true},{"campus_portal_url","https://campus.example/usage"},
                              {"campus_allowed_origins",Json::array({"https://login.example"})},{"campus_refresh_minutes",5},{"campus_unit_price",0.8}});
    bool credential_url=false;try{rpc(service,"set_config",{{"campus_portal_url","https://campus.example/?token=secret"}});}catch(...){credential_url=true;}
    check(credential_url,"portal URL rejects query credentials");
    parse_message(R"({"type":"set_config","request_id":4,"params":{"campus_price_unit":"GB"}})");
    rpc(service,"set_config",{{"campus_price_unit","GB"}});
    check(rpc(service,"get_status")["config"]["campus_price_unit"]=="GB","decimal billing unit persisted");
    bool bad_unit=false;try{rpc(service,"set_config",{{"campus_price_unit","MB"}});}catch(...){bad_unit=true;}
    check(bad_unit,"unsupported campus price units rejected");
    const Json fields={{"balance",{{"mode","auto"},{"selector","#balance"},{"kind","currency"},{"unit","CNY"},{"confidence",0.95}}},
                       {"used_bytes",{{"mode","selector"},{"selector","#used"},{"kind","bytes"},{"unit","B"},{"confidence",0.92}}},
                       {"free_quota_bytes",{{"mode","manual"},{"value",10},{"unit","GiB"},{"kind","bytes"},{"reset_on_cycle",true}}},
                       {"used_free_bytes",{{"mode","manual"},{"value",4},{"unit","GiB"},{"kind","bytes"},{"reset_on_cycle",true}}},
                       {"free_remaining_bytes",{{"mode","formula"},{"formula","free_quota_bytes - used_free_bytes"},{"kind","bytes"}}},
                       {"spendable_balance",{{"mode","manual"},{"value",2},{"unit","CNY"},{"kind","currency"}}},
                       {"paid_used_bytes",{{"mode","formula"},{"formula","max(0, used_bytes - used_free_bytes)"},{"kind","bytes"}}},
                       {"billing_cycle",{{"mode","manual"},{"value","2026-11"},{"unit","text"},{"kind","text"}}}};
    rpc(service,"save_campus_mapping",{{"fields",fields}});
    status=rpc(service,"get_campus_status");
    check(status["rule"]["fields"]["balance"]["selector"]=="#balance","campus rule persisted");
    check(status["rule"]["refresh_minutes"]==5&&status["rule"]["unit_price"]==0.8&&status["rule"]["allowed_origins"].size()==1,"per-portal refresh, price and allowed domains persisted");
    check(status["rule"]["fields"]["free_quota_bytes"]["mode"]=="manual"&&status["rule"]["fields"]["free_remaining_bytes"]["mode"]=="formula","manual and formula mappings persisted");
    const auto saved=snapshot_rpc(service,{{"portal_url","https://campus.example/usage"},{"balance",8.5},
        {"used_bytes",1073741824},{"free_quota_bytes",10737418240},{"remaining_bytes",5368709120},{"quota_bytes",6442450944},{"fee",2.0},
        {"cycle_text","2026-11"},{"quality","confirmed"},{"confidence",1.0},{"mapped_fields",Json::array({"balance","used_bytes"})}});
    check(saved["saved"]==true,"internal confirmed snapshot save");
    status=rpc(service,"get_campus_status");
    check(status["latest"]["used_bytes"]==1073741824&&status["latest"]["balance"]==8.5,"campus snapshot available through bridge");
    check(status["latest"]["details"]["web_values"]["free_quota_bytes"]==10737418240,"additional portal fields persist in structured snapshot details");
    auto extended=saved["snapshot"];extended["mapped_fields"]=Json::array({"balance","free_quota_bytes","free_remaining_bytes","used_free_bytes","spendable_balance","paid_used_bytes","used_bytes","remaining_bytes","quota_bytes","fee","billing_cycle"});
    check(snapshot_rpc(service,extended)["saved"]==true,"all eleven mapped fields can synchronize");
    check(status["rule"]["last_status"]=="confirmed"&&status["rule"]["last_sync_ms"].get<std::int64_t>()>0,"per-portal latest recognition state persisted");
    bool rejected=false;try{snapshot_rpc(service,{{"portal_url","https://attacker.example"},{"quality","confirmed"},{"confidence",1.0}});}catch(...){rejected=true;}
    check(rejected,"snapshot outside configured origin rejected");
    rpc(service,"delete_campus_rule");
    check(rpc(service,"get_campus_status")["rule"].is_null(),"campus rule can be deleted");
    service.stop();
}
}
int main(){try{
    const auto root=std::filesystem::absolute("data/tests/campus-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
    std::filesystem::create_directories(root);storage(root);migration(root);core(root);
    std::cout<<checks<<" campus portal storage, migration and bridge checks passed\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
