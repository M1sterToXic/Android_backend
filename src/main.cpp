#include <GL/glew.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <fstream>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <vector>
#include <map>
#include <mutex>
#include <queue>
#include <zmq.hpp>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct CellTowerData {
    std::string type;
    int mcc = 0; int mnc = 0;
    int pci = 0; int tac = 0;
    int timing_advance = 0; int band = 0;
    int cell_identity = 0; int earfcn = 0;
    int asu_level = 0; int cqi = 0;
    int rsrp = 0; int rsrq = 0;
    int rssi = 0; int rssnr = 0;
    int bsic = 0; int arfcn = 0;
    int lac = 0; int dbm = 0;
    std::string nci;
    int nrarfcn = 0; int ss_rsrp = 0;
    int ss_rsrq = 0; int ss_sinr = 0;
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

LocationData g_locationData;

void saveToJsonFile(const LocationData& data, int counter) {
    try {
        json j;
        j["counter"] = counter;
        j["latitude"] = data.latitude;
        j["longitude"] = data.longitude;
        j["altitude"] = data.altitude;
        j["accuracy"] = data.accuracy;
        j["time"] = data.time;
        j["time_milliseconds"] = data.time_milliseconds;

        json traffic;
        traffic["total_rx"] = data.traffic.total_rx;
        traffic["total_tx"] = data.traffic.total_tx;
        traffic["total"] = data.traffic.total;
        j["traffic"] = traffic;

        json cells = json::array();
        for (const auto& cell : data.cellTowers) {
            json cellJson;
            cellJson["type"] = cell.type;
            cellJson["mcc"] = cell.mcc;
            cellJson["mnc"] = cell.mnc;
            cellJson["pci"] = cell.pci;
            cellJson["tac"] = cell.tac;
            cellJson["timing_advance"] = cell.timing_advance;

            if (cell.type == "LTE") {
                cellJson["band"] = cell.band;
                cellJson["cell_identity"] = cell.cell_identity;
                cellJson["earfcn"] = cell.earfcn;
                cellJson["asu_level"] = cell.asu_level;
                cellJson["cqi"] = cell.cqi;
                cellJson["rsrp"] = cell.rsrp;
                cellJson["rsrq"] = cell.rsrq;
                cellJson["rssi"] = cell.rssi;
                cellJson["rssnr"] = cell.rssnr;
            } else if (cell.type == "GSM") {
                cellJson["bsic"] = cell.bsic;
                cellJson["arfcn"] = cell.arfcn;
                cellJson["lac"] = cell.lac;
                cellJson["dbm"] = cell.dbm;
            } else if (cell.type == "NR") {
                cellJson["nci"] = cell.nci;
                cellJson["nrarfcn"] = cell.nrarfcn;
                cellJson["ss_rsrp"] = cell.ss_rsrp;
                cellJson["ss_rsrq"] = cell.ss_rsrq;
                cellJson["ss_sinr"] = cell.ss_sinr;
                cellJson["timing_advance_micros"] = cell.timing_advance_micros;
            }
            cells.push_back(cellJson);
        }
        j["cells"] = cells;

        const std::string filename = "location_history.json";
        json root;
        std::ifstream inputFile(filename);
        if (inputFile.good()) {
            try {
                inputFile >> root;
                inputFile.close();
            } catch (...) {
                root = json::array();
            }
        } else {
            root = json::array();
        }
        if (!root.is_array()) root = json::array();
        root.push_back(j);
        std::ofstream outputFile(filename);
        outputFile << root.dump(4);
        outputFile.close();
    } catch (const std::exception& e) {}
}

CellTowerData parseCellTower(const json& cellJson) {
    CellTowerData cell;
    cell.type = cellJson.value("type", "Unknown");
    cell.mcc = cellJson.value("mcc", 0);
    cell.mnc = cellJson.value("mnc", 0);
    cell.pci = cellJson.value("pci", 0);
    cell.tac = cellJson.value("tac", 0);
    cell.timing_advance = cellJson.value("timing_advance", 0);

    if (cell.type == "LTE") {
        cell.band = cellJson.value("band", 0);
        cell.cell_identity = cellJson.value("cell_identity", 0);
        cell.earfcn = cellJson.value("earfcn", 0);
        cell.asu_level = cellJson.value("asu_level", 0);
        cell.cqi = cellJson.value("cqi", 0);
        cell.rsrp = cellJson.value("rsrp", 0);
        cell.rsrq = cellJson.value("rsrq", 0);
        cell.rssi = cellJson.value("rssi", 0);
        cell.rssnr = cellJson.value("rssnr", 0);
    } else if (cell.type == "GSM") {
        cell.cell_identity = cellJson.value("cell_identity", 0);
        cell.bsic = cellJson.value("bsic", 0);
        cell.arfcn = cellJson.value("arfcn", 0);
        cell.lac = cellJson.value("lac", 0);
        cell.dbm = cellJson.value("dbm", 0);
    } else if (cell.type == "NR") {
        cell.nci = cellJson.value("nci", "");
        cell.nrarfcn = cellJson.value("nrarfcn", 0);
        cell.ss_rsrp = cellJson.value("ss_rsrp", 0);
        cell.ss_rsrq = cellJson.value("ss_rsrq", 0);
        cell.ss_sinr = cellJson.value("ss_sinr", 0);
        cell.timing_advance_micros = cellJson.value("timing_advance_micros", 0);
    }
    return cell;
}

void run_server() {
    zmq::context_t context(1);
    zmq::socket_t socket(context, zmq::socket_type::rep);
    try {
        socket.bind("tcp://*:80");
        std::cout << "Server started on port 80" << std::endl;
        int counter = 0;
        auto last_save_time = std::chrono::steady_clock::now();
        const std::chrono::seconds save_interval(10);

        while (true) {
            try {
                zmq::message_t request;
                socket.set(zmq::sockopt::rcvtimeo, 1000);
                if (socket.recv(request, zmq::recv_flags::none)) {
                    std::string received(static_cast<char*>(request.data()), request.size());
                    try {
                        json j = json::parse(received);
                        LocationData newData;

                        if (j.contains("location") && j["location"].is_object()) {
                            auto& loc = j["location"];
                            newData.latitude = loc.value("latitude", 0.0);
                            newData.longitude = loc.value("longitude", 0.0);
                            newData.altitude = loc.value("altitude", 0.0);
                            newData.accuracy = loc.value("accuracy", 0.0);
                            long long time_milliseconds = loc.value("current_time", 0LL);
                            newData.time_milliseconds = time_milliseconds;
                            if (time_milliseconds > 0) {
                                std::time_t time_seconds = static_cast<std::time_t>(time_milliseconds / 1000);
                                std::stringstream ss;
                                ss << std::put_time(std::localtime(&time_seconds), "%Y-%m-%d %H:%M:%S");
                                newData.time = ss.str();
                            }
                        }

                        if (j.contains("traffic") && j["traffic"].is_object()) {
                            auto& traffic = j["traffic"];
                            newData.traffic.total_rx = traffic.value("total_rx", 0LL);
                            newData.traffic.total_tx = traffic.value("total_tx", 0LL);
                            newData.traffic.total = traffic.value("total", 0LL);
                        }

                        if (j.contains("cells") && j["cells"].is_array()) {
                            for (const auto& cellJson : j["cells"]) {
                                newData.cellTowers.push_back(parseCellTower(cellJson));
                            }
                        }

                        {
                            std::lock_guard<std::mutex> lock(g_locationData.mutex);
                            g_locationData.latitude = newData.latitude;
                            g_locationData.longitude = newData.longitude;
                            g_locationData.altitude = newData.altitude;
                            g_locationData.accuracy = newData.accuracy;
                            g_locationData.time = newData.time;
                            g_locationData.time_milliseconds = newData.time_milliseconds;
                            g_locationData.traffic = newData.traffic;
                            g_locationData.cellTowers = newData.cellTowers;
                        }

                        auto now = std::chrono::steady_clock::now();
                        if (now - last_save_time >= save_interval) {
                            counter++;
                            saveToJsonFile(newData, counter);
                            last_save_time = now;
                        }

                        std::cout << "Received data at " << newData.time << std::endl;
                        std::cout << "  Location: " << newData.latitude << ", " << newData.longitude
                                  << " (alt: " << newData.altitude << " m, acc: " << newData.accuracy << " m)" << std::endl;
                        std::cout << "  Traffic: RX=" << newData.traffic.total_rx << " bytes, TX=" << newData.traffic.total_tx
                                  << " bytes, Total=" << newData.traffic.total << " bytes" << std::endl;
                        std::cout << "  Cell towers (" << newData.cellTowers.size() << "):" << std::endl;
                        for (const auto& cell : newData.cellTowers) {
                            std::cout << "    Type: " << cell.type;
                            if (cell.type == "LTE") {
                                std::cout << ", MCC=" << cell.mcc << ", MNC=" << cell.mnc << ", PCI=" << cell.pci
                                          << ", RSRP=" << cell.rsrp << " dBm, RSRQ=" << cell.rsrq << " dB";
                            } else if (cell.type == "GSM") {
                                std::cout << ", MCC=" << cell.mcc << ", MNC=" << cell.mnc << ", CID=" << cell.cell_identity
                                          << ", Dbm=" << cell.dbm << " dBm";
                            } else if (cell.type == "NR") {
                                std::cout << ", MCC=" << cell.mcc << ", MNC=" << cell.mnc << ", PCI=" << cell.pci
                                          << ", SS-RSRP=" << cell.ss_rsrp << " dBm";
                            }
                            std::cout << std::endl;
                        }

                        std::string response = "OK";
                        zmq::message_t reply(response.size());
                        memcpy(reply.data(), response.c_str(), response.size());
                        socket.send(reply, zmq::send_flags::none);

                    } catch (const json::parse_error& e) {
                        std::string response = "ERROR";
                        zmq::message_t reply(response.size());
                        memcpy(reply.data(), response.c_str(), response.size());
                        socket.send(reply, zmq::send_flags::none);
                    }
                }
            } catch (const zmq::error_t& e) {
                if (e.num() != EAGAIN) {}
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    } catch (const std::exception& e) {}
    socket.close();
    context.close();
}

int main(int argc, char *argv[]) {
    std::thread server_thread(run_server);
    server_thread.join();
    return 0;
}