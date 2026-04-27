#ifndef MAP_UTILS_H
#define MAP_UTILS_H

#include <string>
#include <vector>
#include <GL/glew.h>

struct TileCoord {
    int x;
    int y;
    int z;
    bool operator<(const TileCoord& other) const {
        if (z != other.z) return z < other.z;
        if (x != other.x) return x < other.x;
        return y < other.y;
    }
};

TileCoord latLonToTile(double lat, double lon, int zoom);
std::string tileUrl(const TileCoord& tc);
std::string tileCachePath(const TileCoord& tc);
bool downloadTile(const TileCoord& tc);
GLuint loadTileTexture(const TileCoord& tc, int& outWidth, int& outHeight);
void deleteTexture(GLuint tex);

#endif