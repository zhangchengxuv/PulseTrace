#include <sqlite3.h>
#include <windows.h>
// Test-only child: commit one row, leave a second transaction uncommitted, and
// terminate without SQLite cleanup to exercise WAL recovery in the parent.
int main(int argc, char **argv) {
    if (argc != 2)
        return 2;
    sqlite3 *db{};
    if (sqlite3_open(argv[1], &db) != SQLITE_OK)
        return 3;
    if (sqlite3_exec(db,
                     "PRAGMA journal_mode=WAL;CREATE TABLE recovery(value INTEGER);INSERT INTO recovery "
                     "VALUES(1);BEGIN IMMEDIATE;INSERT INTO recovery VALUES(2);",
                     nullptr, nullptr, nullptr) != SQLITE_OK)
        return 4;
    ExitProcess(0);
}
