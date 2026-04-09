#ifndef DATABASE_H
#define DATABASE_H

#include <string>
#include <vector>
#include <pqxx/pqxx>

struct DatabaseConnection {
    pqxx::connection* conn = nullptr;
    bool connected = false;
    std::string host;
    std::string dbname;
    std::string user;
    std::string password;
};

bool db_init(DatabaseConnection& db,
             const std::string& host = "localhost",
             const std::string& dbname = "telecom_db",
             const std::string& user = "postgres",
             const std::string& password = "postgres");

void db_close(DatabaseConnection& db);

bool db_is_connected(const DatabaseConnection& db);

int db_insert_location(DatabaseConnection& db,
                       long long timestamp,
                       const std::string& time_str,
                       float latitude,
                       float longitude,
                       float altitude,
                       float accuracy,
                       long long total_rx,
                       long long total_tx,
                       long long total);

bool db_insert_cell(DatabaseConnection& db,
                    int location_id,
                    long long timestamp,
                    const std::string& cell_type,
                    int mcc,
                    int mnc,
                    int pci,
                    int tac,
                    int lac,
                    long long cell_identity,
                    int earfcn,
                    int arfcn,
                    int nrarfcn,
                    int band,
                    int rsrp,
                    int rsrq,
                    int rssi,
                    int rssnr,
                    int ss_rsrp,
                    int ss_rsrq,
                    int ss_sinr,
                    int dbm,
                    int asu_level,
                    int cqi,
                    int timing_advance,
                    long long timing_advance_micros,
                    int bsic,
                    const std::string& nci);

#endif
