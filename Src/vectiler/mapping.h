#pragma once
//-----------------------------------------------------------------------------
#include <bitset>
//-----------------------------------------------------------------------------
typedef Vec2d	v2d;
typedef Vec2	v2;
typedef Vec3	v3;
typedef Vec4	v4;
typedef Vec4d	v4d;
//-----------------------------------------------------------------------------
enum ELayerType
{
	eLayerUnknown,		// 0
	eLayerTerrain,		// 1
	eLayerWater,		// 2
	eLayerBuildings,	// 3
	eLayerPlaces,		// 4
	eLayerTransit,		// 5
	eLayerPois,			// 6
	eLayerBoundaries,	// 7
	eLayerRoads,		// 8
	eLayerEarth,		// 9
	eLayerLanduse,		// 10
	eNumLayerTypes
};
//-----------------------------------------------------------------------------
// Note
// Some kind-values are valid in more than one layer
// e.g. "railway" is used in both Landuse & Transit layers
//-----------------------------------------------------------------------------
enum EFeatureKind
{
	eKind_unknown,

	// Buildings
	eKind_building,				eKind_building_part,	eKind_address,
	// Places
	eKind_borough,				eKind_macrohood,		eKind_microhood,			eKind_neighbourhood,		// eKind_country, eKind_locality, eKind_region
	//Earth
	eKind_archipelago,			eKind_arete,			eKind_cliff,				eKind_continent,			eKind_earth,			eKind_island,
	eKind_islet,				eKind_ridge,			eKind_valley,
	// Roads
	eKind_aerialway,			eKind_aeroway,			eKind_ferry,				eKind_highway,				eKind_major_road,		eKind_minor_road,
	eKind_path,					eKind_piste,			eKind_racetrack,			eKind_rail,					//	eKindPortageway
	// Landuse
	eKind_aerodrome,			eKind_allotments,		eKind_amusement_ride,		eKind_animal,				eKind_apron,			eKind_aquarium,
	eKind_artwork,				eKind_attraction,		eKind_aviary,				eKind_battlefield,			eKind_beach,			eKind_breakwater,
	eKind_bridge,				eKind_camp_site,		eKind_caravan_site,			eKind_carousel,				eKind_cemetery,			eKind_cinema,
	eKind_city_wall,			eKind_college,			eKind_commercial,			eKind_common,				eKind_cutline,			eKind_dam,
	eKind_dike,					eKind_dog_park,			eKind_enclosure,			eKind_farm,					eKind_farmland,			eKind_farmyard,
	eKind_fence,				eKind_footway,			eKind_forest,				eKind_fort,					eKind_fuel,				eKind_garden,
	eKind_gate,					eKind_generator,		eKind_glacier,				eKind_golf_course,			eKind_grass,			eKind_grave_yard,
	eKind_groyne,				eKind_hanami,			eKind_hedge,				eKind_hospital,				eKind_industrial,		eKind_land,
	eKind_library,				eKind_maze,				eKind_meadow,				eKind_military,				eKind_national_park,	eKind_nature_reserve,
	eKind_natural_forest,		eKind_natural_park,		eKind_natural_wood,			eKind_park,					eKind_parking,			eKind_pedestrian,
	eKind_petting_zoo,			eKind_picnic_site,		eKind_pier,					eKind_pitch,				eKind_place_of_worship,	eKind_plant,
	eKind_playground,			eKind_prison,			eKind_protected_area,		eKind_quarry,				eKind_railway,			eKind_recreation_ground,
	eKind_recreation_track,		eKind_residential,		eKind_resort,				eKind_rest_area,			eKind_retail,			eKind_retaining_wall,
	eKind_rock,					eKind_roller_coaster,	eKind_runway,				eKind_rural,				eKind_school,			eKind_scree,
	eKind_scrub,				eKind_service_area,		eKind_snow_fence,			eKind_sports_centre,		eKind_stadium,			eKind_stone,
	eKind_substation,			eKind_summer_toboggan,	eKind_taxiway,				eKind_theatre,				eKind_theme_park,		eKind_tower,
	eKind_trail_riding_station,	eKind_university,		eKind_urban_area,			eKind_urban,				eKind_village_green,	eKind_wastewater_plant,
	eKind_water_park,			eKind_water_slide,		eKind_water_works,			eKind_wetland,				eKind_wilderness_hut,	eKind_wildlife_park,
	eKind_winery,				eKind_winter_sports,	eKind_wood,					eKind_works,				eKind_zoo,
	// Transit
	eKind_light_rail,			eKind_monorail,			eKind_platform,				eKind_subway,				eKind_train,			eKind_tram,		// eKind_railway
	// Water
	eKind_basin,				eKind_bay,				eKind_canal,				eKind_ditch,				eKind_dock,				eKind_drain,
	eKind_fjord,				eKind_lake,				eKind_ocean,				eKind_playa,				eKind_river,			eKind_riverbank,
	eKind_sea,					eKind_stream,			eKind_strait,				eKind_swimming_pool,		eKind_water,
	// Pois
	eKind_station,
	//Boundaries
	eKind_aboriginal_lands,		eKind_country,			eKind_county,				eKind_disputed,				eKind_indefinite,		eKind_indeterminate,
	eKind_lease_limit,			eKind_line_of_control,	eKind_locality,				eKind_macroregion,			eKind_map_unit,			eKind_overlay_limit,
	eKind_region,

	NUM_KINDS
};
//-----------------------------------------------------------------------------
struct MapGeom
{
	Crgba	color;
	float	width;	// Lines
	bool	skip;

	MapGeom() {}
	MapGeom(const Crgba& c, const float w = 1.0f, const bool s = false) : color(c), width(w), skip(s) {}
};
//-----------------------------------------------------------------------------
struct Tile {
	int x;
	int y;
	int z;

	std::bitset<4> borders;
	Vec2d tileOrigin;

	bool operator==(const Tile& rhs) const { return x == rhs.x && y == rhs.y && z == rhs.z; }

	Tile(int x, int y, int z);
};
//-----------------------------------------------------------------------------
extern const char* layerNames[eNumLayerTypes + 1];
extern Hash<CStr, EFeatureKind> gKindHash;
extern Hash<int, MapGeom> gGeomHash;
//-----------------------------------------------------------------------------
void CreateKindHash();