#pragma once
#include "rapidjson/document.h"
#include "tiledata.h"
//-----------------------------------------------------------------------------
namespace GeoJson
{
    bool extractPoint(const rapidjson::Value& _in, Point& _out, const Tile& _tile, Point* last = nullptr);
    void extractLine(const rapidjson::Value& _in, Line& _out, const Tile& _tile);
    void extractPoly(const rapidjson::Value& _in, Polygon2& _out, const Tile& _tile);
    void extractFeature(const ELayerType layerType, const rapidjson::Value& _in, Feature& _out, const Tile& _tile, const float defaultHeight);
    void extractLayer(const rapidjson::Value& _in, Layer& _out, const Tile& _tile);
}
