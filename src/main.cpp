#include <GL/glew.h>
#include <SDL2/SDL.h>
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
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"
#include "imgui.h"
#include "implot.h"

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
};

std::map<std::string, TowerSignalHistory> g_towerHistories;
std::mutex g_historiesMutex;

LocationData g_locationData;
FilterSettings g_filterSettings;
std::mutex g_commandMutex;
std::queue<json> g_commandQueue;
bool g_commandsEnabled = false;

void sendFilterCommands() {
    if (!g_commandsEnabled) return;
    try {
        json command;
        command["type"] = "filter_update";
        command["filters"] = {
            {"location", g_filterSettings.sendLocation},
            {"lte", g_filterSettings.sendLTE},
            {"gsm", g_filterSettings.sendGSM},
            {"nr", g_filterSettings.sendNR},
            {"traffic", g_filterSettings.sendTraffic}
        };
        std::lock_guard<std::mutex> lock(g_commandMutex);
        g_commandQueue.push(command);
    } catch (const std::exception& e) {}
}

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

void run_gui(LocationData* loc) {
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
    SDL_Window* window = SDL_CreateWindow(
        "Data Monitor",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1800, 1100, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 8.0f;
    style.FrameRounding = 6.0f;
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.09f, 0.10f, 0.12f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.20f, 0.22f, 0.24f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.25f, 0.27f, 0.29f, 1.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.17f, 0.20f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.00f, 0.50f, 0.80f, 0.60f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.00f, 0.60f, 0.90f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.00f, 0.70f, 1.00f, 1.00f);
    colors[ImGuiCol_Separator] = ImVec4(0.30f, 0.32f, 0.35f, 1.00f);
    style.WindowPadding = ImVec2(20, 20);
    style.FramePadding = ImVec2(15, 10);
    style.ItemSpacing = ImVec2(15, 15);
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 330");
    io.FontGlobalScale = 1.4f;

    bool running = true;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(1800, 1100));
        ImGui::Begin("Main", nullptr,
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoSavedSettings);

        float filtersChanged = false;
        bool locVal = false, lteVal = false, gsmVal = false, nrVal = false, trafficVal = false;
        {
            std::lock_guard<std::mutex> lock(g_filterSettings.mutex);
            locVal = g_filterSettings.sendLocation;
            lteVal = g_filterSettings.sendLTE;
            gsmVal = g_filterSettings.sendGSM;
            nrVal = g_filterSettings.sendNR;
            trafficVal = g_filterSettings.sendTraffic;
        }

        ImGui::Columns(2, "top_row", false);
        ImGui::SetColumnWidth(0, 450);

        ImGui::BeginChild("FiltersChild", ImVec2(0, 300), true);
        ImGui::TextColored(ImVec4(0.00f, 1.00f, 0.00f, 1.00f), "FILTERS");
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Checkbox("Send Location Data", &locVal)) {
            std::lock_guard<std::mutex> lock(g_filterSettings.mutex);
            g_filterSettings.sendLocation = locVal;
            filtersChanged = true;
        }
        ImGui::Spacing();
        if (ImGui::Checkbox("Send LTE Data", &lteVal)) {
            std::lock_guard<std::mutex> lock(g_filterSettings.mutex);
            g_filterSettings.sendLTE = lteVal;
            filtersChanged = true;
        }
        if (ImGui::Checkbox("Send GSM Data", &gsmVal)) {
            std::lock_guard<std::mutex> lock(g_filterSettings.mutex);
            g_filterSettings.sendGSM = gsmVal;
            filtersChanged = true;
        }
        if (ImGui::Checkbox("Send NR (5G) Data", &nrVal)) {
            std::lock_guard<std::mutex> lock(g_filterSettings.mutex);
            g_filterSettings.sendNR = nrVal;
            filtersChanged = true;
        }
        ImGui::Spacing();
        if (ImGui::Checkbox("Send Traffic Statistics", &trafficVal)) {
            std::lock_guard<std::mutex> lock(g_filterSettings.mutex);
            g_filterSettings.sendTraffic = trafficVal;
            filtersChanged = true;
        }
        if (filtersChanged) {
            g_commandsEnabled = true;
            sendFilterCommands();
        }
        ImGui::EndChild();

        ImGui::NextColumn();
        ImGui::SetColumnWidth(1, 1200);

        ImGui::BeginChild("PositionChild", ImVec2(0, 300), true);
        ImGui::TextColored(ImVec4(0.00f, 1.00f, 0.00f, 1.00f), "CURRENT POSITION");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextColored(ImVec4(0.80f, 0.80f, 0.80f, 1.00f), "Latitude:");
        ImGui::SameLine(120);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.00f, 1.00f, 0.00f, 1.00f));
        ImGui::Text("%.6f°", loc->latitude);
        ImGui::PopStyleColor();

        ImGui::TextColored(ImVec4(0.80f, 0.80f, 0.80f, 1.00f), "Longitude:");
        ImGui::SameLine(120);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.00f, 1.00f, 0.00f, 1.00f));
        ImGui::Text("%.6f°", loc->longitude);
        ImGui::PopStyleColor();

        ImGui::TextColored(ImVec4(0.80f, 0.80f, 0.80f, 1.00f), "Altitude:");
        ImGui::SameLine(120);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.00f, 1.00f, 0.00f, 1.00f));
        ImGui::Text("%.2f m", loc->altitude);
        ImGui::PopStyleColor();

        ImGui::TextColored(ImVec4(0.80f, 0.80f, 0.80f, 1.00f), "Accuracy:");
        ImGui::SameLine(120);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.00f, 1.00f, 0.00f, 1.00f));
        ImGui::Text("%.2f m", loc->accuracy);
        ImGui::PopStyleColor();

        ImGui::TextColored(ImVec4(0.80f, 0.80f, 0.80f, 1.00f), "Time:");
        ImGui::SameLine(150);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.00f, 1.00f, 0.00f, 1.00f));
        ImGui::Text("%s", loc->time.c_str());
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextColored(ImVec4(0.00f, 1.00f, 0.00f, 1.00f), "TRAFFIC");
        ImGui::Spacing();

        float total_mb = loc->traffic.total / (1024.0 * 1024.0);
        float rx_mb = loc->traffic.total_rx / (1024.0 * 1024.0);
        float tx_mb = loc->traffic.total_tx / (1024.0 * 1024.0);

        ImGui::TextColored(ImVec4(0.80f, 0.80f, 0.80f, 1.00f), "Total:");
        ImGui::SameLine(90);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.00f, 1.00f, 0.00f, 1.00f));
        ImGui::Text("%.2f MB", total_mb);
        ImGui::PopStyleColor();

        ImGui::TextColored(ImVec4(0.80f, 0.80f, 0.80f, 1.00f), "Download (RX):");
        ImGui::SameLine(160);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.00f, 1.00f, 0.00f, 1.00f));
        ImGui::Text("%.2f MB", rx_mb);
        ImGui::PopStyleColor();

        ImGui::TextColored(ImVec4(0.80f, 0.80f, 0.80f, 1.00f), "Upload (TX):");
        ImGui::SameLine(150);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.00f, 1.00f, 0.00f, 1.00f));
        ImGui::Text("%.2f MB", tx_mb);
        ImGui::PopStyleColor();

        ImGui::EndChild();

        ImGui::Columns(1);
        ImGui::Spacing();

        ImGui::BeginChild("GraphsChild", ImVec2(0, 800), true);
        ImGui::TextColored(ImVec4(0.00f, 1.00f, 0.00f, 1.00f), "SIGNAL STRENGTH GRAPHS");
        ImGui::Separator();
        ImGui::Spacing();

        {
            std::vector<CellTowerData> currentTowers;
            {
                std::lock_guard<std::mutex> lock(loc->mutex);
                currentTowers = loc->cellTowers;
            }

            std::vector<CellTowerData> filtered;
            for (const auto& cell : currentTowers) {
                bool sendThisType = false;
                {
                    std::lock_guard<std::mutex> lock(g_filterSettings.mutex);
                    if (cell.type == "LTE") sendThisType = g_filterSettings.sendLTE;
                    else if (cell.type == "GSM") sendThisType = g_filterSettings.sendGSM;
                    else if (cell.type == "NR") sendThisType = g_filterSettings.sendNR;
                }
                if (sendThisType) {
                    filtered.push_back(cell);
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

            const size_t windowSize = 20;

            if (!lteTowers.empty()) {
                if (ImPlot::BeginPlot("LTE RSRP (dBm)", ImVec2(-1, 270))) {
                    ImPlot::SetupAxes("Sample", "dBm");
                    ImPlot::SetupAxisLimits(ImAxis_X1, 0, windowSize-1, ImGuiCond_Always);
                    for (size_t i = 0; i < lteTowers.size(); ++i) {
                        std::string key = "LTE_" + std::to_string(lteTowers[i].mcc) + "_" + std::to_string(lteTowers[i].mnc) + "_" + std::to_string(lteTowers[i].pci);
                        std::lock_guard<std::mutex> lock(g_historiesMutex);
                        auto it = g_towerHistories.find(key);
                        if (it != g_towerHistories.end()) {
                            size_t n = it->second.rsrp.size();
                            if (n > 0) {
                                const double* data = n > windowSize ? it->second.rsrp.data() + (n - windowSize) : it->second.rsrp.data();
                                size_t count = n > windowSize ? windowSize : n;
                                ImPlot::PlotLine(("RSRP " + std::to_string(i+1)).c_str(), data, count);
                            }
                        }
                    }
                    ImPlot::EndPlot();
                }

                if (ImPlot::BeginPlot("LTE RSRQ (dB)", ImVec2(-1, 270))) {
                    ImPlot::SetupAxes("Sample", "dB");
                    ImPlot::SetupAxisLimits(ImAxis_X1, 0, windowSize-1, ImGuiCond_Always);
                    for (size_t i = 0; i < lteTowers.size(); ++i) {
                        std::string key = "LTE_" + std::to_string(lteTowers[i].mcc) + "_" + std::to_string(lteTowers[i].mnc) + "_" + std::to_string(lteTowers[i].pci);
                        std::lock_guard<std::mutex> lock(g_historiesMutex);
                        auto it = g_towerHistories.find(key);
                        if (it != g_towerHistories.end()) {
                            size_t n = it->second.rsrq.size();
                            if (n > 0) {
                                const double* data = n > windowSize ? it->second.rsrq.data() + (n - windowSize) : it->second.rsrq.data();
                                size_t count = n > windowSize ? windowSize : n;
                                ImPlot::PlotLine(("RSRQ " + std::to_string(i+1)).c_str(), data, count);
                            }
                        }
                    }
                    ImPlot::EndPlot();
                }
            }

            if (!nrTowers.empty()) {
                if (ImPlot::BeginPlot("NR SS-RSRP (dBm)", ImVec2(-1, 270))) {
                    ImPlot::SetupAxes("Sample", "dBm");
                    ImPlot::SetupAxisLimits(ImAxis_X1, 0, windowSize-1, ImGuiCond_Always);
                    for (size_t i = 0; i < nrTowers.size(); ++i) {
                        std::string key = "NR_" + std::to_string(nrTowers[i].mcc) + "_" + std::to_string(nrTowers[i].mnc) + "_" + std::to_string(nrTowers[i].pci);
                        std::lock_guard<std::mutex> lock(g_historiesMutex);
                        auto it = g_towerHistories.find(key);
                        if (it != g_towerHistories.end()) {
                            size_t n = it->second.ss_rsrp.size();
                            if (n > 0) {
                                const double* data = n > windowSize ? it->second.ss_rsrp.data() + (n - windowSize) : it->second.ss_rsrp.data();
                                size_t count = n > windowSize ? windowSize : n;
                                ImPlot::PlotLine(("SS-RSRP " + std::to_string(i+1)).c_str(), data, count);
                            }
                        }
                    }
                    ImPlot::EndPlot();
                }

                if (ImPlot::BeginPlot("NR SS-RSRQ (dB)", ImVec2(-1, 270))) {
                    ImPlot::SetupAxes("Sample", "dB");
                    ImPlot::SetupAxisLimits(ImAxis_X1, 0, windowSize-1, ImGuiCond_Always);
                    for (size_t i = 0; i < nrTowers.size(); ++i) {
                        std::string key = "NR_" + std::to_string(nrTowers[i].mcc) + "_" + std::to_string(nrTowers[i].mnc) + "_" + std::to_string(nrTowers[i].pci);
                        std::lock_guard<std::mutex> lock(g_historiesMutex);
                        auto it = g_towerHistories.find(key);
                        if (it != g_towerHistories.end()) {
                            size_t n = it->second.ss_rsrq.size();
                            if (n > 0) {
                                const double* data = n > windowSize ? it->second.ss_rsrq.data() + (n - windowSize) : it->second.ss_rsrq.data();
                                size_t count = n > windowSize ? windowSize : n;
                                ImPlot::PlotLine(("SS-RSRQ " + std::to_string(i+1)).c_str(), data, count);
                            }
                        }
                    }
                    ImPlot::EndPlot();
                }

                if (ImPlot::BeginPlot("NR SS-SINR (dB)", ImVec2(-1, 270))) {
                    ImPlot::SetupAxes("Sample", "dB");
                    ImPlot::SetupAxisLimits(ImAxis_X1, 0, windowSize-1, ImGuiCond_Always);
                    for (size_t i = 0; i < nrTowers.size(); ++i) {
                        std::string key = "NR_" + std::to_string(nrTowers[i].mcc) + "_" + std::to_string(nrTowers[i].mnc) + "_" + std::to_string(nrTowers[i].pci);
                        std::lock_guard<std::mutex> lock(g_historiesMutex);
                        auto it = g_towerHistories.find(key);
                        if (it != g_towerHistories.end()) {
                            size_t n = it->second.ss_sinr.size();
                            if (n > 0) {
                                const double* data = n > windowSize ? it->second.ss_sinr.data() + (n - windowSize) : it->second.ss_sinr.data();
                                size_t count = n > windowSize ? windowSize : n;
                                ImPlot::PlotLine(("SS-SINR " + std::to_string(i+1)).c_str(), data, count);
                            }
                        }
                    }
                    ImPlot::EndPlot();
                }
            }

            if (!gsmTowers.empty()) {
                if (ImPlot::BeginPlot("GSM Dbm (dBm)", ImVec2(-1, 270))) {
                    ImPlot::SetupAxes("Sample", "dBm");
                    ImPlot::SetupAxisLimits(ImAxis_X1, 0, windowSize-1, ImGuiCond_Always);
                    for (size_t i = 0; i < gsmTowers.size(); ++i) {
                        std::string key = "GSM_" + std::to_string(gsmTowers[i].mcc) + "_" + std::to_string(gsmTowers[i].mnc) + "_" + std::to_string(gsmTowers[i].pci);
                        std::lock_guard<std::mutex> lock(g_historiesMutex);
                        auto it = g_towerHistories.find(key);
                        if (it != g_towerHistories.end()) {
                            size_t n = it->second.dbm.size();
                            if (n > 0) {
                                const double* data = n > windowSize ? it->second.dbm.data() + (n - windowSize) : it->second.dbm.data();
                                size_t count = n > windowSize ? windowSize : n;
                                ImPlot::PlotLine(("Dbm " + std::to_string(i+1)).c_str(), data, count);
                            }
                        }
                    }
                    ImPlot::EndPlot();
                }
            }

            if (lteTowers.empty() && nrTowers.empty() && gsmTowers.empty()) {
                ImGui::Text("No data yet");
            }
        }
        ImGui::EndChild();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::BeginChild("TelephonyChild", ImVec2(0, 400), true);
        ImGui::TextColored(ImVec4(0.00f, 1.00f, 0.00f, 1.00f), "TELEPHONY DATA");
        ImGui::Separator();
        ImGui::Spacing();

        {
            std::vector<CellTowerData> allTowers;
            {
                std::lock_guard<std::mutex> lock(loc->mutex);
                allTowers = loc->cellTowers;
            }

            std::vector<CellTowerData> filtered;
            for (const auto& cell : allTowers) {
                bool sendThisType = false;
                {
                    std::lock_guard<std::mutex> lock(g_filterSettings.mutex);
                    if (cell.type == "LTE") sendThisType = g_filterSettings.sendLTE;
                    else if (cell.type == "GSM") sendThisType = g_filterSettings.sendGSM;
                    else if (cell.type == "NR") sendThisType = g_filterSettings.sendNR;
                }
                if (sendThisType) {
                    filtered.push_back(cell);
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

            size_t count = std::min<size_t>(filtered.size(), 5);
            for (size_t i = 0; i < count; ++i) {
                const auto& cell = filtered[i];
                ImVec4 typeColor;
                if (cell.type == "LTE") typeColor = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);
                else if (cell.type == "NR") typeColor = ImVec4(1.00f, 0.80f, 0.00f, 1.00f);
                else typeColor = ImVec4(0.80f, 0.80f, 0.80f, 1.00f);

                ImGui::PushStyleColor(ImGuiCol_Text, typeColor);
                ImGui::Text("Tower %zu [%s]", i + 1, cell.type.c_str());
                ImGui::PopStyleColor();

                ImGui::Columns(2, "tower_columns", false);
                ImGui::SetColumnWidth(0, 500);

                if (cell.type == "LTE") {
                    ImGui::TextColored(ImVec4(0.80f,0.80f,0.80f,1.00f), "Band:");
                    ImGui::SameLine(100);
                    ImGui::Text("%d", cell.band);
                    ImGui::TextColored(ImVec4(0.80f,0.80f,0.80f,1.00f), "Cell ID:");
                    ImGui::SameLine(100);
                    ImGui::Text("%d", cell.cell_identity);
                    ImGui::TextColored(ImVec4(0.80f,0.80f,0.80f,1.00f), "EARFCN:");
                    ImGui::SameLine(100);
                    ImGui::Text("%d", cell.earfcn);
                    ImGui::TextColored(ImVec4(0.80f,0.80f,0.80f,1.00f), "MCC:");
                    ImGui::SameLine(100);
                    ImGui::Text("%d", cell.mcc);
                    ImGui::TextColored(ImVec4(0.80f,0.80f,0.80f,1.00f), "MNC:");
                    ImGui::SameLine(100);
                    ImGui::Text("%d", cell.mnc);
                    ImGui::TextColored(ImVec4(0.80f,0.80f,0.80f,1.00f), "PCI:");
                    ImGui::SameLine(100);
                    ImGui::Text("%d", cell.pci);
                    ImGui::TextColored(ImVec4(0.80f,0.80f,0.80f,1.00f), "TAC:");
                    ImGui::SameLine(100);
                    ImGui::Text("%d", cell.tac);
                } else if (cell.type == "GSM") {
                    ImGui::TextColored(ImVec4(0.80f,0.80f,0.80f,1.00f), "Cell ID:");
                    ImGui::SameLine(100);
                    ImGui::Text("%d", cell.cell_identity);
                    ImGui::TextColored(ImVec4(0.80f,0.80f,0.80f,1.00f), "ARFCN:");
                    ImGui::SameLine(100);
                    ImGui::Text("%d", cell.arfcn);
                    ImGui::TextColored(ImVec4(0.80f,0.80f,0.80f,1.00f), "BSIC:");
                    ImGui::SameLine(100);
                    ImGui::Text("%d", cell.bsic);
                    ImGui::TextColored(ImVec4(0.80f,0.80f,0.80f,1.00f), "LAC:");
                    ImGui::SameLine(100);
                    ImGui::Text("%d", cell.lac);
                    ImGui::TextColored(ImVec4(0.80f,0.80f,0.80f,1.00f), "MCC:");
                    ImGui::SameLine(100);
                    ImGui::Text("%d", cell.mcc);
                    ImGui::TextColored(ImVec4(0.80f,0.80f,0.80f,1.00f), "MNC:");
                    ImGui::SameLine(100);
                    ImGui::Text("%d", cell.mnc);
                } else if (cell.type == "NR") {
                    ImGui::TextColored(ImVec4(0.80f,0.80f,0.80f,1.00f), "Band:");
                    ImGui::SameLine(100);
                    ImGui::Text("%d", cell.band);
                    ImGui::TextColored(ImVec4(0.80f,0.80f,0.80f,1.00f), "NCI:");
                    ImGui::SameLine(100);
                    ImGui::Text("%s", cell.nci.c_str());
                    ImGui::TextColored(ImVec4(0.80f,0.80f,0.80f,1.00f), "NRARFCN:");
                    ImGui::SameLine(100);
                    ImGui::Text("%d", cell.nrarfcn);
                    ImGui::TextColored(ImVec4(0.80f,0.80f,0.80f,1.00f), "PCI:");
                    ImGui::SameLine(100);
                    ImGui::Text("%d", cell.pci);
                    ImGui::TextColored(ImVec4(0.80f,0.80f,0.80f,1.00f), "TAC:");
                    ImGui::SameLine(100);
                    ImGui::Text("%d", cell.tac);
                    ImGui::TextColored(ImVec4(0.80f,0.80f,0.80f,1.00f), "MCC:");
                    ImGui::SameLine(100);
                    ImGui::Text("%d", cell.mcc);
                    ImGui::TextColored(ImVec4(0.80f,0.80f,0.80f,1.00f), "MNC:");
                    ImGui::SameLine(100);
                    ImGui::Text("%d", cell.mnc);
                }

                ImGui::NextColumn();

                if (cell.type == "LTE") {
                    ImGui::TextColored(ImVec4(0.80f,0.80f,0.80f,1.00f), "ASU Level:");
                    ImGui::SameLine(120);
                    ImGui::Text("%d", cell.asu_level);
                    ImGui::TextColored(ImVec4(0.80f,0.80f,0.80f,1.00f), "CQI:");
                    ImGui::SameLine(100);
                    ImGui::Text("%d", cell.cqi);
                    ImGui::TextColored(ImVec4(0.80f,0.80f,0.80f,1.00f), "RSRP:");
                    ImGui::SameLine(100);
                    ImGui::Text("%d dBm", cell.rsrp);
                    ImGui::TextColored(ImVec4(0.80f,0.80f,0.80f,1.00f), "RSRQ:");
                    ImGui::SameLine(100);
                    ImGui::Text("%d", cell.rsrq);
                    ImGui::TextColored(ImVec4(0.80f,0.80f,0.80f,1.00f), "RSSI:");
                    ImGui::SameLine(100);
                    ImGui::Text("%d", cell.rssi);
                    ImGui::TextColored(ImVec4(0.80f,0.80f,0.80f,1.00f), "RSSNR:");
                    ImGui::SameLine(100);
                    ImGui::Text("%d", cell.rssnr);
                    ImGui::TextColored(ImVec4(0.80f,0.80f,0.80f,1.00f), "TA:");
                    ImGui::SameLine(100);
                    ImGui::Text("%d", cell.timing_advance);
                } else if (cell.type == "GSM") {
                    ImGui::TextColored(ImVec4(0.80f,0.80f,0.80f,1.00f), "Dbm:");
                    ImGui::SameLine(100);
                    ImGui::Text("%d dBm", cell.dbm);
                    ImGui::TextColored(ImVec4(0.80f,0.80f,0.80f,1.00f), "RSSI:");
                    ImGui::SameLine(100);
                    ImGui::Text("%d", cell.rssi);
                    ImGui::TextColored(ImVec4(0.80f,0.80f,0.80f,1.00f), "TA:");
                    ImGui::SameLine(100);
                    ImGui::Text("%d", cell.timing_advance);
                } else if (cell.type == "NR") {
                    ImGui::TextColored(ImVec4(0.80f,0.80f,0.80f,1.00f), "SS-RSRP:");
                    ImGui::SameLine(100);
                    ImGui::Text("%d dBm", cell.ss_rsrp);
                    ImGui::TextColored(ImVec4(0.80f,0.80f,0.80f,1.00f), "SS-RSRQ:");
                    ImGui::SameLine(100);
                    ImGui::Text("%d", cell.ss_rsrq);
                    ImGui::TextColored(ImVec4(0.80f,0.80f,0.80f,1.00f), "SS-SINR:");
                    ImGui::SameLine(100);
                    ImGui::Text("%d", cell.ss_sinr);
                    ImGui::TextColored(ImVec4(0.80f,0.80f,0.80f,1.00f), "TA:");
                    ImGui::SameLine(100);
                    ImGui::Text("%ld µs", cell.timing_advance_micros);
                }

                ImGui::Columns(1);
                if (i < count - 1) {
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();
                }
            }
            if (filtered.empty()) {
                ImGui::TextColored(ImVec4(1.00f, 0.50f, 0.50f, 1.00f), "No cell tower data");
            }
        }

        ImGui::EndChild();
        ImGui::End();

        ImGui::Render();
        glClearColor(0.08f, 0.09f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
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
        socket.bind("tcp://*:5555");
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

                        {
                            std::lock_guard<std::mutex> lock(g_historiesMutex);
                            for (const auto& cell : newData.cellTowers) {
                                std::string key;
                                if (cell.type == "LTE") {
                                    key = "LTE_" + std::to_string(cell.mcc) + "_" + std::to_string(cell.mnc) + "_" + std::to_string(cell.pci);
                                    g_towerHistories[key].rsrp.push_back(cell.rsrp);
                                    g_towerHistories[key].rsrq.push_back(cell.rsrq);
                                    if (g_towerHistories[key].rsrp.size() > TowerSignalHistory::MAX_SIZE) {
                                        g_towerHistories[key].rsrp.erase(g_towerHistories[key].rsrp.begin());
                                        g_towerHistories[key].rsrq.erase(g_towerHistories[key].rsrq.begin());
                                    }
                                } else if (cell.type == "NR") {
                                    key = "NR_" + std::to_string(cell.mcc) + "_" + std::to_string(cell.mnc) + "_" + std::to_string(cell.pci);
                                    g_towerHistories[key].ss_rsrp.push_back(cell.ss_rsrp);
                                    g_towerHistories[key].ss_rsrq.push_back(cell.ss_rsrq);
                                    g_towerHistories[key].ss_sinr.push_back(cell.ss_sinr);
                                    if (g_towerHistories[key].ss_rsrp.size() > TowerSignalHistory::MAX_SIZE) {
                                        g_towerHistories[key].ss_rsrp.erase(g_towerHistories[key].ss_rsrp.begin());
                                        g_towerHistories[key].ss_rsrq.erase(g_towerHistories[key].ss_rsrq.begin());
                                        g_towerHistories[key].ss_sinr.erase(g_towerHistories[key].ss_sinr.begin());
                                    }
                                } else if (cell.type == "GSM") {
                                    key = "GSM_" + std::to_string(cell.mcc) + "_" + std::to_string(cell.mnc) + "_" + std::to_string(cell.pci);
                                    g_towerHistories[key].dbm.push_back(cell.dbm);
                                    if (g_towerHistories[key].dbm.size() > TowerSignalHistory::MAX_SIZE) {
                                        g_towerHistories[key].dbm.erase(g_towerHistories[key].dbm.begin());
                                    }
                                }
                            }
                        }

                        auto now = std::chrono::steady_clock::now();
                        if (now - last_save_time >= save_interval) {
                            counter++;
                            saveToJsonFile(newData, counter);
                            last_save_time = now;
                        }

                        json command;
                        bool hasCommand = false;
                        {
                            std::lock_guard<std::mutex> lock(g_commandMutex);
                            if (!g_commandQueue.empty()) {
                                command = g_commandQueue.front();
                                g_commandQueue.pop();
                                hasCommand = true;
                            }
                        }

                        std::string response;
                        if (hasCommand) {
                            response = command.dump();
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

int main(int argc, char *argv[]) {
    std::thread server_thread(run_server);
    std::thread gui_thread(run_gui, &g_locationData);
    gui_thread.join();
    server_thread.join();
    return 0;
}