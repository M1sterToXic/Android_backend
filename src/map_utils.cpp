#include "map_utils.h"
#include <curl/curl.h>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <cmath>
#include <cstdlib>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace fs = std::filesystem;

TileCoord latLonToTile(double lat, double lon, int zoom) {
    double lat_rad = lat * M_PI / 180.0;
    double n = pow(2.0, zoom);
    int x = (int)((lon + 180.0) / 360.0 * n);
    int y = (int)((1.0 - asinh(tan(lat_rad)) / M_PI) / 2.0 * n);
    return {x, y, zoom};
}

std::string tileUrl(const TileCoord& tc) {
    return "https://tile.openstreetmap.org/" + std::to_string(tc.z) + "/" + 
           std::to_string(tc.x) + "/" + std::to_string(tc.y) + ".png";
}

std::string tileCachePath(const TileCoord& tc) {
    std::string path = "tiles/" + std::to_string(tc.z) + "/" + std::to_string(tc.x);
    return path + "/" + std::to_string(tc.y) + ".png";
}

static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    std::ofstream* ofs = static_cast<std::ofstream*>(userp);
    size_t total = size * nmemb;
    ofs->write(static_cast<char*>(contents), total);
    return total;
}

bool downloadTile(const TileCoord& tc) {
    std::string cachePath = tileCachePath(tc);
    if (fs::exists(cachePath)) return true;
    
    fs::create_directories(fs::path(cachePath).parent_path());
    
    CURL* curl = curl_easy_init();
    if (!curl) return false;
    
    std::ofstream ofs(cachePath, std::ios::binary);
    if (!ofs) {
        curl_easy_cleanup(curl);
        return false;
    }
    
    std::string url = tileUrl(tc);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ofs);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "TelecomMonitor/1.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    ofs.close();
    
    if (res != CURLE_OK) {
        fs::remove(cachePath);
        return false;
    }
    return true;
}

GLuint loadTileTexture(const TileCoord& tc, int& outWidth, int& outHeight) {
    std::string path = tileCachePath(tc);
    int w, h, comp;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &comp, STBI_rgb_alpha);
    if (!data) return 0;
    
    outWidth = w;
    outHeight = h;
    
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    
    stbi_image_free(data);
    return tex;
}

void deleteTexture(GLuint tex) {
    if (tex) glDeleteTextures(1, &tex);
}