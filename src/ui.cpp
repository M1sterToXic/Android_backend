#include <GL/glew.h>
#include <SDL2/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"
#include "imgui.h"
#include "implot.h"

#include "app_context.h"

using json = nlohmann::json;

const ImVec4 g_pciColors[] = {
    ImVec4(1.0f, 0.0f, 0.0f, 1.0f),
    ImVec4(0.0f, 1.0f, 0.0f, 1.0f),
    ImVec4(0.0f, 0.0f, 1.0f, 1.0f),
    ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
    ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
    ImVec4(0.0f, 1.0f, 1.0f, 1.0f),
    ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
    ImVec4(0.5f, 0.0f, 1.0f, 1.0f),
    ImVec4(0.0f, 1.0f, 0.5f, 1.0f),
    ImVec4(1.0f, 0.0f, 0.5f, 1.0f),
};

static void sendFilterCommands(AppContext& appState) {
    if (!appState.commandsEnabled) return;
    try {
        json command;
        command["type"] = "filter_update";
        command["filters"] = {
            {"location", appState.filterSettings.sendLocation},
            {"lte", appState.filterSettings.sendLTE},
            {"gsm", appState.filterSettings.sendGSM},
            {"nr", appState.filterSettings.sendNR},
            {"traffic", appState.filterSettings.sendTraffic}
        };
        std::lock_guard<std::mutex> lock(appState.commandMutex);
        appState.commandQueue.push(command.dump());
    } catch (const std::exception& e) {}
}

static void drawMapWindow(AppContext& appState) {
    ImGui::Begin("Map View");

    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    if (canvasSize.x < 100) canvasSize.x = 800;
    if (canvasSize.y < 100) canvasSize.y = 600;

    static std::vector<MapSignalPoint> cachedDbPoints;
    static Uint64 lastDbFetchTick = 0;
    const Uint64 nowTick = SDL_GetTicks64();
    if (db_is_connected(appState.dbConnection) && nowTick - lastDbFetchTick >= 1000) {
        cachedDbPoints = db_get_recent_map_points(appState.dbConnection, 0);
        lastDbFetchTick = nowTick;
    }

    if (ImPlot::BeginPlot("##MapPlot", canvasSize, ImPlotFlags_NoTitle | ImPlotFlags_NoLegend | ImPlotFlags_NoMenus)) {
        ImPlot::SetupAxes(NULL, NULL, ImPlotAxisFlags_NoDecorations, ImPlotAxisFlags_NoDecorations);
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Linear);
        ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Linear);

        int zoom = appState.mapState.zoom;
        double centerLat = appState.mapState.centerLat;
        double centerLon = appState.mapState.centerLon;

        centerLat = std::clamp(centerLat, -85.05112878, 85.05112878);
        while (centerLon < -180.0) centerLon += 360.0;
        while (centerLon > 180.0) centerLon -= 360.0;

        double tileSize = 256.0;
        const int tileCount = (1 << zoom);
        const double worldSize = tileCount * tileSize;

        const double centerXPixel = (centerLon + 180.0) / 360.0 * worldSize;
        const double centerYPixel = (1.0 - asinh(tan(centerLat * M_PI / 180.0)) / M_PI) * 0.5 * worldSize;

        double xMin = centerXPixel - canvasSize.x * 0.5;
        double xMax = centerXPixel + canvasSize.x * 0.5;
        double yMin = centerYPixel - canvasSize.y * 0.5;
        double yMax = centerYPixel + canvasSize.y * 0.5;

        double plotYMin = worldSize - yMax;
        double plotYMax = worldSize - yMin;

        ImPlot::SetupAxisLimits(ImAxis_X1, xMin, xMax, ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, plotYMin, plotYMax, ImGuiCond_Always);

        int minTileX = static_cast<int>(std::floor(xMin / tileSize));
        int maxTileX = static_cast<int>(std::floor((xMax - 1.0) / tileSize));
        int minTileY = static_cast<int>(std::floor(yMin / tileSize));
        int maxTileY = static_cast<int>(std::floor((yMax - 1.0) / tileSize));

        for (int ty = minTileY; ty <= maxTileY; ++ty) {
            if (ty < 0 || ty >= tileCount) continue;
            for (int tx = minTileX; tx <= maxTileX; ++tx) {
                int wrappedX = tx % tileCount;
                if (wrappedX < 0) wrappedX += tileCount;
                TileCoord tc{wrappedX, ty, zoom};
                if (!downloadTile(tc)) continue;

                GLuint tex = 0;
                {
                    std::lock_guard<std::mutex> lock(appState.mapState.mutex);
                    auto it = appState.mapState.textureCache.find(tc);
                    if (it != appState.mapState.textureCache.end()) {
                        tex = it->second;
                    }
                }
                if (!tex) {
                    int w, h;
                    tex = loadTileTexture(tc, w, h);
                    if (tex) {
                        std::lock_guard<std::mutex> lock(appState.mapState.mutex);
                        appState.mapState.textureCache[tc] = tex;
                    }
                }
                if (tex) {
                    double x0 = tx * tileSize;
                    double x1 = (tx + 1) * tileSize;
                    double y0 = worldSize - (ty + 1) * tileSize;
                    double y1 = worldSize - ty * tileSize;
                    ImPlot::PlotImage("##tile", (ImTextureID)(intptr_t)tex,
                                      ImPlotPoint(x0, y0), ImPlotPoint(x1, y1));
                }
            }
        }

        std::vector<double> strongX, strongY;
        std::vector<double> mediumX, mediumY;
        std::vector<double> weakX, weakY;
        std::vector<double> unknownX, unknownY;
        strongX.reserve(cachedDbPoints.size());
        strongY.reserve(cachedDbPoints.size());
        mediumX.reserve(cachedDbPoints.size());
        mediumY.reserve(cachedDbPoints.size());
        weakX.reserve(cachedDbPoints.size());
        weakY.reserve(cachedDbPoints.size());
        unknownX.reserve(cachedDbPoints.size());
        unknownY.reserve(cachedDbPoints.size());

        for (const auto& point : cachedDbPoints) {
            if (point.latitude == 0.0 && point.longitude == 0.0) continue;
            if (point.latitude < -85.05112878 || point.latitude > 85.05112878) continue;

            double px = (point.longitude + 180.0) / 360.0 * worldSize;
            while (px < xMin) px += worldSize;
            while (px > xMax) px -= worldSize;
            if (px < xMin || px > xMax) continue;

            double pyWorld = (1.0 - asinh(tan(point.latitude * M_PI / 180.0)) / M_PI) * 0.5 * worldSize;
            if (pyWorld < yMin || pyWorld > yMax) continue;
            double py = worldSize - pyWorld;

            if (!std::isfinite(point.signal)) {
                unknownX.push_back(px);
                unknownY.push_back(py);
            } else if (point.signal >= -90.0) {
                strongX.push_back(px);
                strongY.push_back(py);
            } else if (point.signal >= -105.0) {
                mediumX.push_back(px);
                mediumY.push_back(py);
            } else {
                weakX.push_back(px);
                weakY.push_back(py);
            }
        }

        if (!strongX.empty()) {
            ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 5.0f, ImVec4(0.10f, 0.85f, 0.15f, 0.95f), IMPLOT_AUTO, ImVec4(0.10f, 0.85f, 0.15f, 0.95f));
            ImPlot::PlotScatter("##DbStrong", strongX.data(), strongY.data(), static_cast<int>(strongX.size()));
        }
        if (!mediumX.empty()) {
            ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 5.0f, ImVec4(0.98f, 0.72f, 0.10f, 0.95f), IMPLOT_AUTO, ImVec4(0.98f, 0.72f, 0.10f, 0.95f));
            ImPlot::PlotScatter("##DbMedium", mediumX.data(), mediumY.data(), static_cast<int>(mediumX.size()));
        }
        if (!weakX.empty()) {
            ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 5.0f, ImVec4(0.92f, 0.22f, 0.22f, 0.95f), IMPLOT_AUTO, ImVec4(0.92f, 0.22f, 0.22f, 0.95f));
            ImPlot::PlotScatter("##DbWeak", weakX.data(), weakY.data(), static_cast<int>(weakX.size()));
        }
        if (!unknownX.empty()) {
            ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 4.0f, ImVec4(0.75f, 0.75f, 0.75f, 0.70f), IMPLOT_AUTO, ImVec4(0.75f, 0.75f, 0.75f, 0.70f));
            ImPlot::PlotScatter("##DbUnknown", unknownX.data(), unknownY.data(), static_cast<int>(unknownX.size()));
        }

        if (ImPlot::IsPlotHovered() && ImGui::GetIO().MouseWheel != 0) {
            int newZoom = zoom + (ImGui::GetIO().MouseWheel > 0 ? 1 : -1);
            if (newZoom >= 2 && newZoom <= 18) {
                std::lock_guard<std::mutex> lock(appState.mapState.mutex);
                appState.mapState.zoom = newZoom;
            }
        }

        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && ImPlot::IsPlotHovered()) {
            ImVec2 dragDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
            if (dragDelta.x != 0 || dragDelta.y != 0) {
                double newCenterX = centerXPixel - dragDelta.x;
                double newCenterY = centerYPixel - dragDelta.y;
                while (newCenterX < 0.0) newCenterX += worldSize;
                while (newCenterX >= worldSize) newCenterX -= worldSize;
                newCenterY = std::clamp(newCenterY, 0.0, worldSize - 1.0);

                double newCenterLon = newCenterX / worldSize * 360.0 - 180.0;
                double n = M_PI - 2.0 * M_PI * newCenterY / worldSize;
                double newCenterLat = std::atan(std::sinh(n)) * 180.0 / M_PI;
                {
                    std::lock_guard<std::mutex> lock(appState.mapState.mutex);
                    appState.mapState.centerLon = newCenterLon;
                    appState.mapState.centerLat = newCenterLat;
                }
                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
            }
        }

        ImPlot::EndPlot();
    }

    if (ImGui::Button("Reset to Novosibirsk")) {
        std::lock_guard<std::mutex> lock(appState.mapState.mutex);
        appState.mapState.centerLat = 55.0084;
        appState.mapState.centerLon = 82.9357;
        appState.mapState.zoom = 10;
    }
    ImGui::SameLine();
    ImGui::Text("Zoom: %d", appState.mapState.zoom);

    ImGui::End();
}

void run_gui(AppContext* app) {
    AppContext& appState = *app;
    LocationData* loc = &appState.locationData;

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

        if (ImGui::BeginTabBar("MainTabs")) {
            if (ImGui::BeginTabItem("Telemetry")) {
                float filtersChanged = false;
                bool locVal = false, lteVal = false, gsmVal = false, nrVal = false, trafficVal = false;
                {
                    std::lock_guard<std::mutex> lock(appState.filterSettings.mutex);
                    locVal = appState.filterSettings.sendLocation;
                    lteVal = appState.filterSettings.sendLTE;
                    gsmVal = appState.filterSettings.sendGSM;
                    nrVal = appState.filterSettings.sendNR;
                    trafficVal = appState.filterSettings.sendTraffic;
                }

                ImGui::Columns(2, "top_row", false);
                ImGui::SetColumnWidth(0, 450);

                ImGui::BeginChild("FiltersChild", ImVec2(0, 300), true);
                ImGui::TextColored(ImVec4(0.00f, 1.00f, 0.00f, 1.00f), "FILTERS");
                ImGui::Separator();
                ImGui::Spacing();

                if (ImGui::Checkbox("Send Location Data", &locVal)) {
                    std::lock_guard<std::mutex> lock(appState.filterSettings.mutex);
                    appState.filterSettings.sendLocation = locVal;
                    filtersChanged = true;
                }
                ImGui::Spacing();
                if (ImGui::Checkbox("Send LTE Data", &lteVal)) {
                    std::lock_guard<std::mutex> lock(appState.filterSettings.mutex);
                    appState.filterSettings.sendLTE = lteVal;
                    filtersChanged = true;
                }
                if (ImGui::Checkbox("Send GSM Data", &gsmVal)) {
                    std::lock_guard<std::mutex> lock(appState.filterSettings.mutex);
                    appState.filterSettings.sendGSM = gsmVal;
                    filtersChanged = true;
                }
                if (ImGui::Checkbox("Send NR (5G) Data", &nrVal)) {
                    std::lock_guard<std::mutex> lock(appState.filterSettings.mutex);
                    appState.filterSettings.sendNR = nrVal;
                    filtersChanged = true;
                }
                ImGui::Spacing();
                if (ImGui::Checkbox("Send Traffic Statistics", &trafficVal)) {
                    std::lock_guard<std::mutex> lock(appState.filterSettings.mutex);
                    appState.filterSettings.sendTraffic = trafficVal;
                    filtersChanged = true;
                }
                if (filtersChanged) {
                    appState.commandsEnabled = true;
                    sendFilterCommands(appState);
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
                            std::lock_guard<std::mutex> lock(appState.filterSettings.mutex);
                            if (cell.type == "LTE") sendThisType = appState.filterSettings.sendLTE;
                            else if (cell.type == "GSM") sendThisType = appState.filterSettings.sendGSM;
                            else if (cell.type == "NR") sendThisType = appState.filterSettings.sendNR;
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
                                std::string key = "LTE_" + std::to_string(lteTowers[i].mcc) + "_" +
                                                  std::to_string(lteTowers[i].mnc) + "_" +
                                                  std::to_string(lteTowers[i].pci);
                                std::lock_guard<std::mutex> lock(appState.historiesMutex);
                                auto it = appState.towerHistories.find(key);
                                if (it != appState.towerHistories.end()) {
                                    size_t n = it->second.rsrp.size();
                                    if (n > 0) {
                                        std::string label = "PCI " + std::to_string(lteTowers[i].pci) +
                                                            " (MCC:" + std::to_string(lteTowers[i].mcc) +
                                                            " MNC:" + std::to_string(lteTowers[i].mnc) +
                                                            ") " + std::to_string((int)it->second.rsrp.back()) + " dBm";

                                        const double* data = n > windowSize ? it->second.rsrp.data() + (n - windowSize) : it->second.rsrp.data();
                                        size_t count = n > windowSize ? windowSize : n;

                                        ImPlot::SetNextLineStyle(g_pciColors[i % (sizeof(g_pciColors)/sizeof(g_pciColors[0]))]);
                                        ImPlot::PlotLine(label.c_str(), data, count);
                                    }
                                }
                            }
                            ImPlot::EndPlot();
                        }

                        if (ImPlot::BeginPlot("LTE RSRQ (dB)", ImVec2(-1, 270))) {
                            ImPlot::SetupAxes("Sample", "dB");
                            ImPlot::SetupAxisLimits(ImAxis_X1, 0, windowSize-1, ImGuiCond_Always);

                            for (size_t i = 0; i < lteTowers.size(); ++i) {
                                std::string key = "LTE_" + std::to_string(lteTowers[i].mcc) + "_" +
                                                  std::to_string(lteTowers[i].mnc) + "_" +
                                                  std::to_string(lteTowers[i].pci);
                                std::lock_guard<std::mutex> lock(appState.historiesMutex);
                                auto it = appState.towerHistories.find(key);
                                if (it != appState.towerHistories.end()) {
                                    size_t n = it->second.rsrq.size();
                                    if (n > 0) {
                                        std::string label = "PCI " + std::to_string(lteTowers[i].pci) +
                                                            " " + std::to_string((int)it->second.rsrq.back()) + " dB";

                                        const double* data = n > windowSize ? it->second.rsrq.data() + (n - windowSize) : it->second.rsrq.data();
                                        size_t count = n > windowSize ? windowSize : n;

                                        ImPlot::SetNextLineStyle(g_pciColors[i % (sizeof(g_pciColors)/sizeof(g_pciColors[0]))]);
                                        ImPlot::PlotLine(label.c_str(), data, count);
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
                                std::string key = "NR_" + std::to_string(nrTowers[i].mcc) + "_" +
                                                  std::to_string(nrTowers[i].mnc) + "_" +
                                                  std::to_string(nrTowers[i].pci);
                                std::lock_guard<std::mutex> lock(appState.historiesMutex);
                                auto it = appState.towerHistories.find(key);
                                if (it != appState.towerHistories.end()) {
                                    size_t n = it->second.ss_rsrp.size();
                                    if (n > 0) {
                                        std::string label = "PCI " + std::to_string(nrTowers[i].pci) +
                                                            " " + std::to_string((int)it->second.ss_rsrp.back()) + " dBm";

                                        const double* data = n > windowSize ? it->second.ss_rsrp.data() + (n - windowSize) : it->second.ss_rsrp.data();
                                        size_t count = n > windowSize ? windowSize : n;

                                        ImPlot::SetNextLineStyle(g_pciColors[i % (sizeof(g_pciColors)/sizeof(g_pciColors[0]))]);
                                        ImPlot::PlotLine(label.c_str(), data, count);
                                    }
                                }
                            }
                            ImPlot::EndPlot();
                        }

                        if (ImPlot::BeginPlot("NR SS-RSRQ (dB)", ImVec2(-1, 270))) {
                            ImPlot::SetupAxes("Sample", "dB");
                            ImPlot::SetupAxisLimits(ImAxis_X1, 0, windowSize-1, ImGuiCond_Always);

                            for (size_t i = 0; i < nrTowers.size(); ++i) {
                                std::string key = "NR_" + std::to_string(nrTowers[i].mcc) + "_" +
                                                  std::to_string(nrTowers[i].mnc) + "_" +
                                                  std::to_string(nrTowers[i].pci);
                                std::lock_guard<std::mutex> lock(appState.historiesMutex);
                                auto it = appState.towerHistories.find(key);
                                if (it != appState.towerHistories.end()) {
                                    size_t n = it->second.ss_rsrq.size();
                                    if (n > 0) {
                                        std::string label = "PCI " + std::to_string(nrTowers[i].pci) +
                                                            " " + std::to_string((int)it->second.ss_rsrq.back()) + " dB";

                                        const double* data = n > windowSize ? it->second.ss_rsrq.data() + (n - windowSize) : it->second.ss_rsrq.data();
                                        size_t count = n > windowSize ? windowSize : n;

                                        ImPlot::SetNextLineStyle(g_pciColors[i % (sizeof(g_pciColors)/sizeof(g_pciColors[0]))]);
                                        ImPlot::PlotLine(label.c_str(), data, count);
                                    }
                                }
                            }
                            ImPlot::EndPlot();
                        }

                        if (ImPlot::BeginPlot("NR SS-SINR (dB)", ImVec2(-1, 270))) {
                            ImPlot::SetupAxes("Sample", "dB");
                            ImPlot::SetupAxisLimits(ImAxis_X1, 0, windowSize-1, ImGuiCond_Always);

                            for (size_t i = 0; i < nrTowers.size(); ++i) {
                                std::string key = "NR_" + std::to_string(nrTowers[i].mcc) + "_" +
                                                  std::to_string(nrTowers[i].mnc) + "_" +
                                                  std::to_string(nrTowers[i].pci);
                                std::lock_guard<std::mutex> lock(appState.historiesMutex);
                                auto it = appState.towerHistories.find(key);
                                if (it != appState.towerHistories.end()) {
                                    size_t n = it->second.ss_sinr.size();
                                    if (n > 0) {
                                        std::string label = "PCI " + std::to_string(nrTowers[i].pci) +
                                                            " " + std::to_string((int)it->second.ss_sinr.back()) + " dB";

                                        const double* data = n > windowSize ? it->second.ss_sinr.data() + (n - windowSize) : it->second.ss_sinr.data();
                                        size_t count = n > windowSize ? windowSize : n;

                                        ImPlot::SetNextLineStyle(g_pciColors[i % (sizeof(g_pciColors)/sizeof(g_pciColors[0]))]);
                                        ImPlot::PlotLine(label.c_str(), data, count);
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
                                std::string key = "GSM_" + std::to_string(gsmTowers[i].mcc) + "_" +
                                                  std::to_string(gsmTowers[i].mnc) + "_" +
                                                  std::to_string(gsmTowers[i].pci);
                                std::lock_guard<std::mutex> lock(appState.historiesMutex);
                                auto it = appState.towerHistories.find(key);
                                if (it != appState.towerHistories.end()) {
                                    size_t n = it->second.dbm.size();
                                    if (n > 0) {
                                        std::string label = "PCI " + std::to_string(gsmTowers[i].pci) +
                                                            " " + std::to_string((int)it->second.dbm.back()) + " dBm";

                                        const double* data = n > windowSize ? it->second.dbm.data() + (n - windowSize) : it->second.dbm.data();
                                        size_t count = n > windowSize ? windowSize : n;

                                        ImPlot::SetNextLineStyle(g_pciColors[i % (sizeof(g_pciColors)/sizeof(g_pciColors[0]))]);
                                        ImPlot::PlotLine(label.c_str(), data, count);
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
                            std::lock_guard<std::mutex> lock(appState.filterSettings.mutex);
                            if (cell.type == "LTE") sendThisType = appState.filterSettings.sendLTE;
                            else if (cell.type == "GSM") sendThisType = appState.filterSettings.sendGSM;
                            else if (cell.type == "NR") sendThisType = appState.filterSettings.sendNR;
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
                ImGui::EndTabItem();
            }
            
            if (ImGui::BeginTabItem("Map")) {
                drawMapWindow(appState);
                ImGui::EndTabItem();
            }
            
            ImGui::EndTabBar();
        }
        
        ImGui::End();

        ImGui::Render();
        glClearColor(0.08f, 0.09f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    {
        std::lock_guard<std::mutex> lock(appState.mapState.mutex);
        for (auto& pair : appState.mapState.textureCache) {
            deleteTexture(pair.second);
        }
        appState.mapState.textureCache.clear();
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
}
