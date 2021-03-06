#pragma once
#include "rapidjson/document.h"
#include "tiledata.h"
//-----------------------------------------------------------------------------
namespace GeoJson
{
   // int extractLineString(const rapidjson::Value& _in, LineString& _out, const Tile& _tile);
    //void extractPoly(const rapidjson::Value& _in, Polygon2& _out, const Tile& _tile);
    bool extractFeature(const ELayerType layerType, const rapidjson::Value& _in, Feature& _out, const Tile& _tile, const float defaultHeight);
    void extractLayer(const rapidjson::Value& _in, Layer& _out, const Tile& _tile);
}
