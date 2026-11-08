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
    {DatabaseManager db(file);check(db.migration_info()["from"]==6&&db.migration_info()["to"]==DatabaseManager::supported_schema,"v6 to current campus migration");
     check(db.query(1,2,1)[0]["value"]==7,"migration preserves existing counters");check(db.campus_latest().is_null(),"new campus history starts unknown");}
}
void measurements(const std::filesystem::path &root){
    check(std::abs(30399.45*campus_unit_factor("MB",1024)/campus_unit_factor("GB",1024)-29.687)<0.001,"school MB to GB uses 1024");
    check(campus_unit_factor("GiB",1000)==1073741824.0&&campus_unit_factor("GB",1000)==1e9,"IEC units keep explicit byte definitions");
    const auto file=root/"v7.db";
    {DatabaseManager db(file);Json s={{"observed_ms",1},{"portal_origin","https://school.example"},{"portal_url","https://school.example"},{"balance",27.45},{"used_bytes",30399.45e6},{"remaining_bytes",nullptr},{"quota_bytes",nullptr},{"fee",nullptr},{"quality","confirmed"},{"confidence",1.0},{"details",{{"web_units",{{"used_bytes","MB"}}},{"web_values",{{"used_bytes",30399.45e6}}}}}};db.save_campus_snapshot(s);}
    sqlite3 *raw{};const auto name=file.u8string();check(sqlite3_open(reinterpret_cast<const char*>(name.c_str()),&raw)==SQLITE_OK,"open schema fixture");
    check(sqlite3_exec(raw,"ALTER TABLE campus_snapshots DROP COLUMN measurement_json;PRAGMA user_version=7",nullptr,nullptr,nullptr)==SQLITE_OK,"create v7 measurement fixture");sqlite3_close(raw);
    {DatabaseManager db(file);const auto s=db.campus_latest();check(db.migration_info()["from"]==7,"v7 metadata migration");
     check(s["used_bytes"]==30399.45e6,"migration never rewrites historical normalized value");
     check(std::abs(s["measurement"]["raw_readings"]["used_bytes"]["value"].get<double>()-30399.45)<1e-8,"legacy raw value recovered from known unit");
     check(s["measurement"]["status"]=="recovered_source_units","recovered observations explicitly identified");}
    {DatabaseManager db(file);check(db.campus_latest()["used_bytes"]==30399.45e6,"reopening never applies a second conversion");
     check(db.campus_latest()["measurement"]["raw_readings"]["used_bytes"]["unit"]=="MB","raw unit survives restart");}
    check(campus_measurement(Json::object())["status"]=="needs_confirmation","unknown historical units are not guessed");
    const auto config=root/"legacy-price";std::filesystem::create_directories(config);
    {std::ofstream out(config/"config.json");out<<R"({"config_version":1,"campus_unit_price":0.6,"campus_price_unit":"GiB"})";}
    {CoreService c(config);const auto cfg=c.status()["config"];check(cfg["campus_unit_price"]==0.6&&cfg["campus_price_unit"]=="GiB"&&cfg["campus_price_basis"]=="legacy_bytes","legacy price keeps actual amount and economic byte basis");check(cfg["campus_traffic_radix"]==1024,"school convention defaults to 1024");}
    {CoreService c(config);check(c.status()["config"]["campus_price_basis"]=="legacy_bytes","price migration is idempotent");}
    const auto persisted=root/"school-config";std::filesystem::create_directories(persisted);
    {std::ofstream out(persisted/"config.json");out<<R"({"config_version":1,"campus_traffic_radix":1000,"campus_price_basis":"school","campus_price_unit":"GB","campus_unit_price":0.7,"campus_linear_tariff":true})";}
    {CoreService c(persisted);const auto cfg=c.status()["config"];check(cfg["campus_traffic_radix"]==1000&&cfg["campus_unit_price"]==0.7&&cfg["campus_linear_tariff"]==true,"school conversion and tariff survive application restart");}
}
void core(const std::filesystem::path &root){
    parse_message(R"({"type":"open_campus_portal","request_id":1,"params":{}})");
    parse_message(R"({"type":"campus_pick_field","request_id":2,"params":{"field":"balance"}})");
    for(const auto field:campus_field_names){
        Json id=nullptr;const auto request=Json{{"type","campus_pick_field"},{"request_id",41},{"params",{{"field",field}}}};
        check(parse_message(request.dump(),&id)["params"]["field"]==field&&id==41,"all eleven picker fields pass the real bridge parser");
    }
    Json rejected_id=nullptr;bool rejected_parameter=false;
    try{parse_message(R"({"type":"campus_pick_field","request_id":42,"params":{"field":"password"}})",&rejected_id);}catch(...){rejected_parameter=true;}
    check(rejected_parameter&&rejected_id==42,"invalid picker parameters retain their valid request ID for immediate rejection");
    for(const auto text:{R"({"type":"ping","request_id":42,"request_id":43,"params":{}})",R"({"type":"ping","request_id":-1,"params":{}})",R"({"type":"ping","request_id":"42","params":{}})",R"({"type":"ping","request_id":42,"params":{)",R"({"type":"ping","request_id":9007199254740992,"params":{}})"}){
        rejected_id=99;bool malformed=false;try{parse_message(text,&rejected_id);}catch(...){malformed=true;}
        check(malformed&&rejected_id.is_null(),"malformed or ambiguous envelopes do not expose a guessed correlation ID");
    }
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
        {"cycle_text","2026-11"},{"quality","confirmed"},{"confidence",1.0},{"source_units",{{"used_bytes","MB"},{"balance","password"}}},{"mapped_fields",Json::array({"balance","used_bytes"})}});
    check(saved["saved"]==true,"internal confirmed snapshot save");
    status=rpc(service,"get_campus_status");
    check(status["latest"]["used_bytes"]==1073741824&&status["latest"]["balance"]==8.5,"campus snapshot available through bridge");
    check(status["latest"]["details"]["web_values"]["free_quota_bytes"]==10737418240,"additional portal fields persist in structured snapshot details");
    check(status["latest"]["details"]["web_units"]["used_bytes"]=="MB","portal display unit persists through SQLite and bridge");
    check(!status["latest"]["details"]["web_units"].contains("balance"),"non-byte metadata is never stored as a source unit");
    auto invalid_source_unit=saved["snapshot"];invalid_source_unit["source_units"]={{"used_bytes","invalid"}};
    bool unit_rejected=false;try{snapshot_rpc(service,invalid_source_unit);}catch(...){unit_rejected=true;}
    check(unit_rejected,"invalid portal source unit rejected");
    check(rpc(service,"get_campus_status")["latest"]==status["latest"],"invalid metadata preserves the last valid snapshot");
    const auto captured=snapshot_rpc(service,{{"portal_url","https://campus.example/usage"},{"used_bytes",1},{"balance",27.45},{"quality","confirmed"},{"confidence",1.0},{"traffic_radix",1024},{"source_units",{{"used_bytes","MB"}}},{"raw_readings",{{"used_bytes",{{"value",30399.45},{"unit","MB"}}}}}});
    check(captured["snapshot"]["used_bytes"]==30399.45*1024*1024,"core recomputes normalized value from raw reading");
    check(rpc(service,"get_campus_status")["latest"]["measurement"]["raw_readings"]["used_bytes"]["value"]==30399.45,"raw precision survives SQLite and bridge");
    rpc(service,"set_config",{{"campus_traffic_radix",1000},{"campus_linear_tariff",true}});
    check(rpc(service,"get_campus_status")["latest"]["measurement"]==captured["snapshot"]["measurement"],"radix change leaves original readings and capture convention unchanged");
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
    std::filesystem::create_directories(root);storage(root);migration(root);measurements(root);core(root);
    std::cout<<checks<<" campus portal storage, migration and bridge checks passed\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
