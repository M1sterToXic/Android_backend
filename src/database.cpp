#include "database.h"
#include <iostream>
#include <mutex>

static std::mutex g_dbMutex;

bool db_init(DatabaseConnection& db,
             const std::string& host,
             const std::string& dbname,
             const std::string& user,
             const std::string& password) {
    db.host = host;
    db.dbname = dbname;
    db.user = user;
    db.password = password;

    try {
        std::string conn_str = "host=" + host +
                               " dbname=" + dbname +
                               " user=" + user +
                               " password=" + password;

        db.conn = new pqxx::connection(conn_str);

        if (db.conn->is_open()) {
            db.connected = true;
            std::cout << "[DB] Connected to database: " << dbname << std::endl;
            return true;
        } else {
            std::cerr << "[DB] Error: failed to open connection" << std::endl;
            delete db.conn;
            db.conn = nullptr;
            return false;
        }
    } catch (const pqxx::sql_error& e) {
        std::cerr << "[DB] SQL error during connection: " << e.what() << std::endl;
        if (db.conn) {
            delete db.conn;
            db.conn = nullptr;
        }
        return false;
    } catch (const std::exception& e) {
        std::cerr << "[DB] Error during connection: " << e.what() << std::endl;
        if (db.conn) {
            delete db.conn;
            db.conn = nullptr;
        }
        return false;
    }
}

void db_close(DatabaseConnection& db) {
    std::lock_guard<std::mutex> lock(g_dbMutex);
    if (db.conn) {
        delete db.conn;
        db.conn = nullptr;
    }
    db.connected = false;
    std::cout << "[DB] Подключение закрыто" << std::endl;
}

bool db_is_connected(const DatabaseConnection& db) {
    return db.connected && db.conn && db.conn->is_open();
}

int db_insert_location(DatabaseConnection& db,
                       long long timestamp,
                       const std::string& time_str,
                       float latitude,
                       float longitude,
                       float altitude,
                       float accuracy,
                       long long total_rx,
                       long long total_tx,
                       long long total) {
    if (!db_is_connected(db)) {
        std::cerr << "[DB] Ошибка: нет подключения для вставки location" << std::endl;
        return 0;
    }

    std::lock_guard<std::mutex> lock(g_dbMutex);

    try {
        pqxx::work txn(*db.conn);

        std::string sql =
            "INSERT INTO location_measurements "
            "(timestamp, time_str, latitude, longitude, altitude, accuracy, "
            " total_rx, total_tx, total) "
            "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9) "
            "RETURNING id";

        pqxx::result r = txn.exec_params(sql,
            timestamp,
            time_str,
            latitude,
            longitude,
            altitude,
            accuracy,
            total_rx,
            total_tx,
            total
        );

        if (r.size() > 0) {
            int id = r[0][0].as<int>();
            txn.commit();
            return id;
        } else {
            txn.abort();
            std::cerr << "[DB] Error: failed to get ID after location insert" << std::endl;
            return 0;
        }
    } catch (const pqxx::sql_error& e) {
        std::cerr << "[DB] SQL error during location insert: " << e.what() << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "[DB] Error during location insert: " << e.what() << std::endl;
        return 0;
    }
}

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
                    const std::string& nci) {
    if (!db_is_connected(db)) {
        std::cerr << "[DB] Ошибка: нет подключения для вставки cell" << std::endl;
        return false;
    }

    std::lock_guard<std::mutex> lock(g_dbMutex);

    try {
        pqxx::work txn(*db.conn);

        std::string sql =
            "INSERT INTO cell_measurements ("
            " location_id, timestamp, cell_type, mcc, mnc, pci, tac, lac, "
            " cell_identity, earfcn, arfcn, nrarfcn, band, "
            " rsrp, rsrq, rssi, rssnr, "
            " ss_rsrp, ss_rsrq, ss_sinr, dbm, "
            " asu_level, cqi, timing_advance, timing_advance_micros, bsic, nci"
            ") VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, "
            "         $14, $15, $16, $17, $18, $19, $20, $21, "
            "         $22, $23, $24, $25, $26, $27)";

        txn.exec_params(sql,
            location_id,
            timestamp,
            cell_type,
            mcc,
            mnc,
            pci,
            tac,
            lac,
            cell_identity,
            earfcn,
            arfcn,
            nrarfcn,
            band,
            rsrp,
            rsrq,
            rssi,
            rssnr,
            ss_rsrp,
            ss_rsrq,
            ss_sinr,
            dbm,
            asu_level,
            cqi,
            timing_advance,
            timing_advance_micros,
            bsic,
            nci
        );

        txn.commit();
        return true;
    } catch (const pqxx::sql_error& e) {
        std::cerr << "[DB] SQL ошибка при вставке cell: " << e.what() << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::cerr << "[DB] Ошибка при вставке cell: " << e.what() << std::endl;
        return false;
    }
}
