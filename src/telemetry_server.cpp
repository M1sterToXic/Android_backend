#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>
#include <zmq.hpp>

#include "app_context.h"

using json = nlohmann::json;

static void saveToDatabase(AppContext& appState, const LocationData& data) {
    if (!db_is_connected(appState.dbConnection)) {
        return;
    }

    std::vector<CellTowerData> filtered;
    {
        std::lock_guard<std::mutex> lock(appState.filterSettings.mutex);
        for (const auto& cell : data.cellTowers) {
            bool sendThisType = false;
            if (cell.type == "LTE") sendThisType = appState.filterSettings.sendLTE;
            else if (cell.type == "GSM") sendThisType = appState.filterSettings.sendGSM;
            else if (cell.type == "NR") sendThisType = appState.filterSettings.sendNR;
            if (sendThisType) {
                filtered.push_back(cell);
            }
        }
    }

    std::sort(filtered.begin(), filtered.end(),
        [](const CellTowerData& a, const CellTowerData& b) {
            bool aValid = (a.mcc != 2147483647 && a.mnc != 2147483647);
            bool bValid = (b.mcc != 2147483647 && b.mnc != 2147483647);
            if (aValid && !bValid) return true;
            if (!aValid && bValid) return false;
            if (aValid && bValid) {
                if (a.mcc != b.mcc) return a.mcc < b.mcc;
                return a.mnc < b.mnc;
            }
            if (!aValid && !bValid) {
                if (a.mcc != b.mcc) return a.mcc < b.mcc;
                return a.mnc < b.mnc;
            }
            return false;
        });

    const size_t maxTowersPerType = 5;
    std::vector<CellTowerData> lteTowers, nrTowers, gsmTowers;
    for (const auto& cell : filtered) {
        if (cell.type == "LTE" && lteTowers.size() < maxTowersPerType) lteTowers.push_back(cell);
        else if (cell.type == "NR" && nrTowers.size() < maxTowersPerType) nrTowers.push_back(cell);
        else if (cell.type == "GSM" && gsmTowers.size() < maxTowersPerType) gsmTowers.push_back(cell);
    }

    std::vector<CellTowerData> cellsToSave;
    cellsToSave.insert(cellsToSave.end(), lteTowers.begin(), lteTowers.end());
    cellsToSave.insert(cellsToSave.end(), gsmTowers.begin(), gsmTowers.end());
    cellsToSave.insert(cellsToSave.end(), nrTowers.begin(), nrTowers.end());

    int location_id = db_insert_location(
        appState.dbConnection,
        data.time_milliseconds,
        data.time,
        data.latitude,
        data.longitude,
        data.altitude,
        data.accuracy,
        data.traffic.total_rx,
        data.traffic.total_tx,
        data.traffic.total
    );

    if (location_id <= 0) {
        std::cerr << "[DB] Error: failed to insert location data" << std::endl;
        return;
    }

    for (const auto& cell : cellsToSave) {
        db_insert_cell(
            appState.dbConnection,
            location_id,
            data.time_milliseconds,
            cell.type,
            cell.mcc,
            cell.mnc,
            cell.pci,
            cell.tac,
            cell.lac,
            cell.cell_identity,
            cell.earfcn,
            cell.arfcn,
            cell.nrarfcn,
            cell.band,
            cell.rsrp,
            cell.rsrq,
            cell.rssi,
            cell.rssnr,
            cell.ss_rsrp,
            cell.ss_rsrq,
            cell.ss_sinr,
            cell.dbm,
            cell.asu_level,
            cell.cqi,
            cell.timing_advance,
            cell.timing_advance_micros,
            cell.bsic,
            cell.nci
        );
    }
}

static CellTowerData parseCellTower(const json& cellJson) {
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

void run_server(AppContext* app) {
    AppContext& appState = *app;
    zmq::context_t context(1);
    zmq::socket_t socket(context, zmq::socket_type::rep);
    try {
        socket.bind("tcp://*:5555");

        while (appState.running.load()) {
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
                            std::lock_guard<std::mutex> lock(appState.locationData.mutex);
                            appState.locationData.latitude = newData.latitude;
                            appState.locationData.longitude = newData.longitude;
                            appState.locationData.altitude = newData.altitude;
                            appState.locationData.accuracy = newData.accuracy;
                            appState.locationData.time = newData.time;
                            appState.locationData.time_milliseconds = newData.time_milliseconds;
                            appState.locationData.traffic = newData.traffic;
                            appState.locationData.cellTowers = newData.cellTowers;
                        }

                        {
                            std::lock_guard<std::mutex> lock(appState.historiesMutex);
                            for (const auto& cell : newData.cellTowers) {
                                std::string key;
                                if (cell.type == "LTE") {
                                    key = "LTE_" + std::to_string(cell.mcc) + "_" +
                                          std::to_string(cell.mnc) + "_" +
                                          std::to_string(cell.pci);
                                    appState.towerHistories[key].mcc = cell.mcc;
                                    appState.towerHistories[key].mnc = cell.mnc;
                                    appState.towerHistories[key].pci = cell.pci;
                                    appState.towerHistories[key].type = cell.type;
                                    appState.towerHistories[key].rsrp.push_back(cell.rsrp);
                                    appState.towerHistories[key].rsrq.push_back(cell.rsrq);
                                    if (appState.towerHistories[key].rsrp.size() > TowerSignalHistory::MAX_SIZE) {
                                        appState.towerHistories[key].rsrp.erase(appState.towerHistories[key].rsrp.begin());
                                        appState.towerHistories[key].rsrq.erase(appState.towerHistories[key].rsrq.begin());
                                    }
                                } else if (cell.type == "NR") {
                                    key = "NR_" + std::to_string(cell.mcc) + "_" +
                                          std::to_string(cell.mnc) + "_" +
                                          std::to_string(cell.pci);
                                    appState.towerHistories[key].mcc = cell.mcc;
                                    appState.towerHistories[key].mnc = cell.mnc;
                                    appState.towerHistories[key].pci = cell.pci;
                                    appState.towerHistories[key].type = cell.type;
                                    appState.towerHistories[key].ss_rsrp.push_back(cell.ss_rsrp);
                                    appState.towerHistories[key].ss_rsrq.push_back(cell.ss_rsrq);
                                    appState.towerHistories[key].ss_sinr.push_back(cell.ss_sinr);
                                    if (appState.towerHistories[key].ss_rsrp.size() > TowerSignalHistory::MAX_SIZE) {
                                        appState.towerHistories[key].ss_rsrp.erase(appState.towerHistories[key].ss_rsrp.begin());
                                        appState.towerHistories[key].ss_rsrq.erase(appState.towerHistories[key].ss_rsrq.begin());
                                        appState.towerHistories[key].ss_sinr.erase(appState.towerHistories[key].ss_sinr.begin());
                                    }
                                } else if (cell.type == "GSM") {
                                    key = "GSM_" + std::to_string(cell.mcc) + "_" +
                                          std::to_string(cell.mnc) + "_" +
                                          std::to_string(cell.pci);
                                    appState.towerHistories[key].mcc = cell.mcc;
                                    appState.towerHistories[key].mnc = cell.mnc;
                                    appState.towerHistories[key].pci = cell.pci;
                                    appState.towerHistories[key].type = cell.type;
                                    appState.towerHistories[key].dbm.push_back(cell.dbm);
                                    if (appState.towerHistories[key].dbm.size() > TowerSignalHistory::MAX_SIZE) {
                                        appState.towerHistories[key].dbm.erase(appState.towerHistories[key].dbm.begin());
                                    }
                                }
                            }
                        }

                        saveToDatabase(appState, newData);

                        std::string command;
                        bool hasCommand = false;
                        {
                            std::lock_guard<std::mutex> lock(appState.commandMutex);
                            if (!appState.commandQueue.empty()) {
                                command = appState.commandQueue.front();
                                appState.commandQueue.pop();
                                hasCommand = true;
                            }
                        }

                        std::string response;
                        if (hasCommand) {
                            response = command;
                        } else {
                            response = "OK";
                        }

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
