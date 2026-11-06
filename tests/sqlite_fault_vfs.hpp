#pragma once
#include <sqlite3.h>
#include <atomic>
#include <stdexcept>
namespace sqlite_fault {
inline std::atomic<int> write_error{};
struct File {sqlite3_file base;sqlite3_file *real;};
inline sqlite3_file *real(sqlite3_file *f){return reinterpret_cast<File*>(f)->real;}
inline int close(sqlite3_file *f){return real(f)->pMethods->xClose(real(f));}
inline int read(sqlite3_file *f,void *p,int n,sqlite3_int64 o){return real(f)->pMethods->xRead(real(f),p,n,o);}
inline int write(sqlite3_file *f,const void *p,int n,sqlite3_int64 o){const int error=write_error.load();return error?error:real(f)->pMethods->xWrite(real(f),p,n,o);}
inline int truncate(sqlite3_file *f,sqlite3_int64 n){return real(f)->pMethods->xTruncate(real(f),n);}
inline int sync(sqlite3_file *f,int flags){return real(f)->pMethods->xSync(real(f),flags);}
inline int size(sqlite3_file *f,sqlite3_int64 *n){return real(f)->pMethods->xFileSize(real(f),n);}
inline int lock(sqlite3_file *f,int flags){return real(f)->pMethods->xLock(real(f),flags);}
inline int unlock(sqlite3_file *f,int flags){return real(f)->pMethods->xUnlock(real(f),flags);}
inline int reserved(sqlite3_file *f,int *n){return real(f)->pMethods->xCheckReservedLock(real(f),n);}
inline int control(sqlite3_file *f,int op,void *p){return real(f)->pMethods->xFileControl(real(f),op,p);}
inline int sector(sqlite3_file *f){return real(f)->pMethods->xSectorSize(real(f));}
inline int characteristics(sqlite3_file *f){return real(f)->pMethods->xDeviceCharacteristics(real(f));}
inline int map(sqlite3_file *f,int i,int n,int extend,void volatile **p){return real(f)->pMethods->xShmMap(real(f),i,n,extend,p);}
inline int shm_lock(sqlite3_file *f,int o,int n,int flags){return real(f)->pMethods->xShmLock(real(f),o,n,flags);}
inline void barrier(sqlite3_file *f){real(f)->pMethods->xShmBarrier(real(f));}
inline int unmap(sqlite3_file *f,int del){return real(f)->pMethods->xShmUnmap(real(f),del);}
inline int fetch(sqlite3_file *f,sqlite3_int64 o,int n,void **p){if(real(f)->pMethods->xFetch)return real(f)->pMethods->xFetch(real(f),o,n,p);*p=nullptr;return SQLITE_OK;}
inline int unfetch(sqlite3_file *f,sqlite3_int64 o,void *p){return real(f)->pMethods->xUnfetch?real(f)->pMethods->xUnfetch(real(f),o,p):SQLITE_OK;}
inline const sqlite3_io_methods methods{3,close,read,write,truncate,sync,size,lock,unlock,reserved,control,sector,characteristics,map,shm_lock,barrier,unmap,fetch,unfetch};
class Vfs {
 sqlite3_vfs *original_;sqlite3_vfs wrapper_;
 static int open(sqlite3_vfs *v,const char *name,sqlite3_file *file,int flags,int *out){
  auto *self=static_cast<Vfs*>(v->pAppData);auto *f=reinterpret_cast<File*>(file);
  f->base.pMethods=nullptr;f->real=reinterpret_cast<sqlite3_file*>(f+1);
  const int rc=self->original_->xOpen(self->original_,name,f->real,flags,out);
  if(rc==SQLITE_OK)f->base.pMethods=&methods;return rc;
 }
 public:
 Vfs():original_(sqlite3_vfs_find(nullptr)),wrapper_(*original_){
  wrapper_.zName="pulsetrace_isolated_faults";wrapper_.szOsFile=sizeof(File)+original_->szOsFile;
  wrapper_.pAppData=this;wrapper_.xOpen=open;
  if(sqlite3_vfs_register(&wrapper_,1)!=SQLITE_OK)throw std::runtime_error("Cannot register test VFS");
 }
 ~Vfs(){write_error=0;sqlite3_vfs_register(original_,1);sqlite3_vfs_unregister(&wrapper_);}
};
}