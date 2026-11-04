// Explicit developer-only SQLite backup for rendering actual aggregate data in an isolated UI test.
// Never starts capture, copies input sequences, uploads data, or modifies the source database.
#include <sqlite3.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
int wmain(int argc,wchar_t **argv){
 if(argc!=3){std::cout<<"Usage: ui_validation_snapshot.exe SOURCE_MONITOR_DB NEW_TEST_DIRECTORY\n";return argc==1?0:2;}
 sqlite3 *source=nullptr,*destination=nullptr;
 try{
  const auto input=std::filesystem::absolute(argv[1]),dir=std::filesystem::absolute(argv[2]);
  if(!std::filesystem::is_regular_file(input)||std::filesystem::exists(dir))throw std::runtime_error("Source must exist; destination must be new");
  const auto sourceName=input.u8string();
  if(sqlite3_open_v2(reinterpret_cast<const char *>(sourceName.c_str()),&source,SQLITE_OPEN_READONLY,nullptr)!=SQLITE_OK)throw std::runtime_error("Cannot open source read-only");
  std::filesystem::create_directories(dir);const auto output=(dir/"monitor.db").u8string();
  if(sqlite3_open_v2(reinterpret_cast<const char *>(output.c_str()),&destination,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE,nullptr)!=SQLITE_OK)throw std::runtime_error("Cannot create private snapshot");
  auto *backup=sqlite3_backup_init(destination,"main",source,"main");if(!backup)throw std::runtime_error("Cannot start SQLite backup");
  int rc=SQLITE_OK;for(int i=0;i<200;++i){rc=sqlite3_backup_step(backup,256);if(rc==SQLITE_DONE)break;if(rc!=SQLITE_OK&&rc!=SQLITE_BUSY&&rc!=SQLITE_LOCKED)break;if(rc!=SQLITE_OK)sqlite3_sleep(25);}
  sqlite3_backup_finish(backup);if(rc!=SQLITE_DONE)throw std::runtime_error("Snapshot did not complete");
  sqlite3_close(destination);destination=nullptr;sqlite3_close(source);source=nullptr;
  std::ofstream out(dir/"config.json");out<<"{\"theme\":\"dark\",\"capture_on_launch\":false,\"retention_days\":365}";out.close();if(!out)throw std::runtime_error("Cannot save paused test config");
  std::cout<<"Read-only SQLite snapshot complete. Test capture is disabled.\n";return 0;
 }catch(const std::exception &e){if(destination)sqlite3_close(destination);if(source)sqlite3_close(source);std::cerr<<e.what()<<'\n';return 1;}
}
