#ifndef APP_CONTEXT_H
#define APP_CONTEXT_H

#include <atomic>
#include <map>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#include <GL/glew.h>

#include "database.h"
#include "map_utils.h"

struct CellTowerData {
    std::string type;
    int mcc = 0;
    int mnc = 0;
    int pci = 0;
    int tac = 0;
    int timing_advance = 0;
    int band = 0;
    int cell_identity = 0;
    int earfcn = 0;
    int asu_level = 0;
    int cqi = 0;
    int rsrp = 0;
    int rsrq = 0;
    int rssi = 0;
    int rssnr = 0;
    int bsic = 0;
    int arfcn = 0;
    int lac = 0;
    int dbm = 0;
    std::string nci;
    int nrarfcn = 0;
    int ss_rsrp = 0;
    int ss_rsrq = 0;
    int ss_sinr = 0;
    long timing_advance_micros = 0;
};

struct TrafficData {
    long long total_rx = 0;
    long long total_tx = 0;
    long long total = 0;
};

struct LocationData {
    float latitude = 0.0f;
    float longitude = 0.0f;
    float altitude = 0.0f;
    float accuracy = 0.0f;
    std::string time = "No data";
    long long time_milliseconds = 0;
    TrafficData traffic;
    std::vector<CellTowerData> cellTowers;
    std::mutex mutex;
};

struct FilterSettings {
    bool sendLocation = true;
    bool sendLTE = true;
    bool sendGSM = true;
    bool sendNR = true;
    bool sendTraffic = true;
    std::mutex mutex;
};

struct TowerSignalHistory {
    std::vector<double> rsrp;
    std::vector<double> rsrq;
    std::vector<double> ss_rsrp;
    std::vector<double> ss_rsrq;
    std::vector<double> ss_sinr;
    std::vector<double> dbm;
    std::mutex mutex;
    static const size_t MAX_SIZE = 100;

    int mcc = 0;
    int mnc = 0;
    int pci = 0;
    std::string type;
};

struct MapViewState {
    double centerLat = 55.0084;
    double centerLon = 82.9357;
    int zoom = 10;
    std::map<TileCoord, GLuint> textureCache;
    std::mutex mutex;
};

struct AppContext {
    DatabaseConnection dbConnection;
    LocationData locationData;
    FilterSettings filterSettings;
    std::map<std::string, TowerSignalHistory> towerHistories;
    std::mutex historiesMutex;
    std::mutex commandMutex;
    std::queue<std::string> commandQueue;
    std::atomic<bool> commandsEnabled{false};
    std::atomic<bool> running{true};
    MapViewState mapState;
};

void run_gui(AppContext* app);
void run_server(AppContext* app);

#endif
