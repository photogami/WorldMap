#ifdef AOBAKER
#include "aobaker.h"
#else
int aobaker_bake(
	char const* inputmesh,
	char const* outputmesh,
	char const* outputatlas,
	int sizehint,
	int nsamples,
	bool gbuffer,
	bool chartinfo,
	float multiply)
{
	printf("WARNING -- Vectiler was built without ao baker\n");
	return 0;
}
#endif

struct Params {
	const char* apiKey;
	const char* tilex;
	const char* tiley;
	int tilez;
	float offset[2];
	bool splitMesh;
	int aoSizeHint;
	int aoSamples;
	bool aoBaking;
	bool append;
	bool terrain;
	int terrainSubdivision;
	float terrainExtrusionScale;
	bool buildings;
	float buildingsExtrusionScale;
	bool roads;
	float roadsHeight;
	float roadsExtrusionWidth;
	bool normals;
	float buildingsHeight;
	int pedestal;
	float pedestalHeight;
};

int vectiler(struct Params parameters, CStrL& outFileName);

//-----------------------------------------------------------------------------
bool extractTileRange(const CStr& range, int& start, int& end)
{
	CStr tmp;
	int p = 0;

	if (!range.Tokenize('/', p, tmp))
		return false;

	start = tmp.ToInt();
	if (range.Tokenize('/', p, tmp))	end = tmp.ToInt();
	else								end = start;

	return end >= start;
}
//-----------------------------------------------------------------------------
bool ExtractTiles(const char* tilex, const char* tiley, const int tilez, std::vector<Tile>& tiles)
{
	int startx, starty, endx, endy;
	if (!extractTileRange(CStr(tilex), startx, endx)) { LOG("Bad param: %s\n", tilex); return false; }
	if (!extractTileRange(CStr(tiley), starty, endy)) { LOG("Bad param: %s\n", tiley); return false; }

	for (int x = startx; x <= endx; ++x)
	{
		for (int y = starty; y <= endy; ++y)
		{
			Tile t(x, y, tilez);

			if (x == startx)	t.borders.set(Border::left, 1);
			if (x == endx)		t.borders.set(Border::right, 1);
			if (y == starty)	t.borders.set(Border::top, 1);
			if (y == endy)      t.borders.set(Border::bottom, 1);

			tiles.push_back(t);
		}
	}

	if (tiles.size() == 0) { LOG("No tiles to download\n"); return false; }

	return true;
}

//-----------------------------------------------------------------------------
int vectiler(Params exportParams, CStrL& outFileName)
{
	/// Parse tile params
	std::vector<Tile> tiles;
	if (!ExtractTiles(exportParams.tilex, exportParams.tiley, exportParams.tilez, tiles))
		return EXIT_FAILURE;

	const char* apiKey = exportParams.apiKey;
	LOG("Using API key %s\n", apiKey);

	//std::unordered_map<Tile, std::unique_ptr<HeightData>> heightData;
	//std::unordered_map<Tile, std::unique_ptr<TileData>> vectorTileData;
	std::unordered_map<Tile, HeightData*> heightData;
	std::unordered_map<Tile, TileVectorData*> vectorTileData;

	/// Download data
	LOG("---- Downloading tile data ----\n");

	for (auto tile : tiles)
	{
		if (exportParams.terrain)
		{
			auto textureData = DownloadHeightmapTile(tile, apiKey, exportParams.terrainExtrusionScale);

			if (!textureData)
				LOG("Failed to download heightmap texture data for tile %d %d %d\n", tile.x, tile.y, tile.z);

			heightData[tile] = std::move(textureData);
		}

		if (exportParams.buildings || exportParams.roads)
		{
			auto tileData = DownloadVectorTile(tile, apiKey);

			if (!tileData)
				LOG("Failed to download vector tile data for tile %d %d %d\n", tile.x, tile.y, tile.z);

			vectorTileData[tile] = std::move(tileData);
		}
	}

	/// Adjust terrain edges
	if (exportParams.terrain)
		adjustTerrainEdges(heightData);

	//std::vector<std::unique_ptr<PolygonMesh>> meshes;
	std::vector<PolygonMesh*> meshes;

	v2 offset;
	Tile origin = tiles[0];

	LOG("---- Building tile data ----\n");
	CStopWatch sw;
	// Build meshes for each of the tiles
	for (auto tile : tiles)
	{
		offset.x = (tile.x - origin.x) * 2;
		offset.y = -(tile.y - origin.y) * 2;

		//const auto& textureData = heightData[tile];
		const auto heightMap = heightData[tile];

		/// Build terrain mesh
		if (exportParams.terrain)
		{
			if (!heightMap)
				continue;

			PolygonMesh* mesh = CreateTerrainMesh(tile, heightMap, exportParams.terrainSubdivision);
			if (exportParams.normals) computeNormals(mesh);	// Compute faces normals
			mesh->offset = offset;
			meshes.push_back(mesh);

			/// Build pedestal
			if (exportParams.pedestal)
			{
				PolygonMesh* groundWall[2];
				CreatePedestalMeshes(tile, heightMap, groundWall, exportParams.terrainSubdivision, exportParams.pedestalHeight);
				groundWall[0]->offset = offset;
				groundWall[1]->offset = offset;
				meshes.push_back(groundWall[0]);
				meshes.push_back(groundWall[1]);
			}
		}

		/// Build vector tile mesh
		if (exportParams.buildings || exportParams.roads)
		{
			const auto& data = vectorTileData[tile];

			if (data)
			{
				const static std::string keyHeight("height");
				const static std::string keyMinHeight("min_height");
				const float scale = tile.invScale * exportParams.buildingsExtrusionScale;

				for (auto layer : data->layers)
				{
					const int typeIdx = IndexFromStringTable(layer.name.c_str(), layerNames);
					const ELayerType type = typeIdx >= 0 ? (ELayerType)typeIdx : eLayerUnknown;

					if (type == eLayerBuildings && !exportParams.buildings) continue;	// Skip buildings
					if (type == eLayerRoads && !exportParams.roads) continue;			// Skip roads

					const float default_height = type == eLayerBuildings ? exportParams.buildingsHeight * tile.invScale : 0.0f;

					for (auto feature : layer.features)
					{
						if (heightMap && type != eLayerBuildings && type != eLayerRoads) continue;

						// Height
						float height = default_height;
						auto itHeight = feature.props.numericProps.find(keyHeight);
						if (itHeight != feature.props.numericProps.end())
							height = itHeight->second * scale;

						//if (textureData && !layerRoads && height == 0.0f)
						if (heightMap && type != eLayerRoads && height == 0.0f)
							continue;

						// Min height
						double minHeight = 0.0;
						auto itMinHeight = feature.props.numericProps.find(keyMinHeight);
						if (itMinHeight != feature.props.numericProps.end())
							minHeight = itMinHeight->second * scale;

						// JJ 
						//auto mesh = new PolygonMesh(type);
						auto mesh = CreateMeshFromFeature(type, feature, minHeight, height, heightMap, tile.invScale, exportParams.roadsExtrusionWidth, exportParams.roadsHeight);
						if (mesh)
						{
							// Add local mesh offset
							mesh->offset = offset;
							meshes.push_back(mesh);
						}
					}
				}
			}
		}
	}

	LOG("Built PolygonMeshes in %.1fms\n", sw.GetMs());

	// Separate all meshes in it's respective layer
	std::vector<PolygonMesh*> meshArr[eNumLayerTypes];
	MergeLayerMeshes(meshes, meshArr);

	// Merge all meshes from same layer into 1 big mesh. If this mesh gets vcount>65536 the mesh is split
	/*for (PolygonMesh* mesh : meshes)
	{
	const int nv = mesh->vertices.size();

	// This mesh is to big. Throw it away
	if (nv > 65536)
	{
	gLogger.Warning("Mesh > 65536. Must implement mesh splitting");
	continue;
	}

	std::vector<PolygonMesh*>& arr = meshArr[mesh->layerType];	// Choose correct layer
	PolygonMesh* bigMesh = arr.empty() ? NULL : arr.back();
	if (!bigMesh || !AddMeshToMesh(mesh, bigMesh))				// Try to add current mesh to our bigMesh
	{
	bigMesh = new PolygonMesh(mesh->layerType);
	arr.push_back(bigMesh);
	AddMeshToMesh(mesh, bigMesh);
	}
	}*/


	std::string outFile = std::to_string(origin.x) + "." + std::to_string(origin.y) + "." + std::to_string(origin.z);
	std::string outputOBJ = outFile + ".obj";
	std::string outputBIN = outFile + ".bin";
	outFileName = outputBIN.c_str();

	// Save output OBJ file
	bool saved = saveOBJ(outputOBJ.c_str(),
		exportParams.splitMesh, meshes,
		exportParams.offset[0],
		exportParams.offset[1],
		exportParams.append,
		exportParams.normals);

	// Save output BIN file
	SaveBin(outputBIN.c_str(), meshArr);

	//bool saved2 = SaveOBJ2("jerry.obj", meshArr);

	if (!saved)
		return EXIT_FAILURE;

	// Bake ambiant occlusion using AO Baker
	if (exportParams.aoBaking)
	{
		std::string aoFile = outFile + "-ao.obj";
		std::string aoImageFile = outFile + ".png";
		std::string aoMaterialFile = outFile + ".mtl";

		int aoBaked = aobaker_bake(outputOBJ.c_str(), aoFile.c_str(), aoImageFile.c_str(),
			exportParams.aoSizeHint, exportParams.aoSamples,
			false,  // g-buffers
			false,  // charinfo
			1.0f);   // multiply

		bool success = aoBaked == 1;
		std::string aoFileData;

		{
			std::ifstream ifile(aoFile);
			success &= ifile.is_open();

			if (success) {
				aoFileData = std::string((std::istreambuf_iterator<char>(ifile)), (std::istreambuf_iterator<char>()));
				ifile.close();
			}
		}

		// Inject material to wavefront
		{
			std::ofstream ofile(aoFile);
			success &= ofile.is_open();

			if (success) {
				std::string materialHeader;
				materialHeader += "mtllib " + aoMaterialFile + "\n";
				materialHeader += "usemtl tile_mtl\n\n";
				ofile << materialHeader;
				ofile << aoFileData;
				ofile.close();
			}
		}

		// Export material properties
		{
			std::ofstream materialfile(aoMaterialFile);
			success &= materialfile.is_open();

			if (success) {
				printf("Saving material file %s\n", aoMaterialFile.c_str());
				std::string material = R"END(
                    newmtl tile_mtl
                    Ka 0.0000 0.0000 0.0000
                    Kd 1.0000 1.0000 1.0000
                    Ks 0.0000 0.0000 0.0000
                    d 1.0
                    map_Kd )END";
				material += aoImageFile;
				materialfile << material;
				materialfile.close();
			}
		}

		if (!success)
			return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
//-----------------------------------------------------------------------------
int GetTile(const char* tileX, const char* tileY, int tileZ, CStrL& outFileName)
{
	// Default parameters
	int aoAtlasSize = 512;
	int aoSamples = 256;
	int pedestal = 0;
	float pedestalHeight = 0.f;
	int terrainSubdivision = 64;
	float terrainExtrusionScale = 1.f;
	float buildingsExtrusionScale = 1.f;
	float buildingsHeight = 10.0f; //0.0f
	float roadsHeight = 0.2f; // 1.0f
	float roadsExtrusionWidth = 5.f;
	const char* apiKey = "vector-tiles-qVaBcRA";

	// Parse params
	/* flag_usage("[options]");
	flag_string(&apiKey, "apikey", "Developer API key (https://mapzen.com/developers/)");
	flag_int(&splitMeshes, "splitMeshes", "Generate one mesh per feature in wavefront file");
	flag_string(&tileX, "tilex", "Tile X (can be a tile range: 19294/19295)");
	flag_string(&tileY, "tiley", "Tile Y (can be a tile range: 24642/24643)");
	flag_int(&tileZ, "tilez", "Tile Z (zoom)");
	flag_float(&offsetX, "offsetx", "Global tile Offset on X coordinate");
	flag_float(&offsetY, "offsety", "Global tile Offset on Y coordinate");
	flag_int(&append, "append", "Append the obj to an existing obj file");
	flag_int(&buildings, "buildings", "Whether to export building geometry");
	flag_float(&buildingsExtrusionScale, "buildingsExtrusionScale", "Building height scale factor");
	flag_float(&buildingsHeight, "buildingsHeight", "The height at which building should be extruded (if no height data is available)");
	flag_int(&pedestal, "pedestal", "Build a pedestal when running with terrain option (Useful for 3d printing)");
	flag_float(&pedestalHeight, "pedestalHeight", "Pedestal height, can be negative");
	flag_int(&terrain, "terrain", "Generate terrain elevation topography");
	flag_int(&terrainSubdivision, "terrainSubdivision", "Terrain mesh subdivision");
	flag_float(&terrainExtrusionScale, "terrainExtrusionScale", "Terrain mesh extrusion scale");
	flag_int(&aoBaking, "aoBaking", "Generate ambiant occlusion baked atlas");
	flag_int(&aoAtlasSize, "aoAtlasSize", "Controls resolution of atlas");
	flag_int(&aoSamples, "aoSamples", "Number of samples for ambient occlusion");
	flag_int(&roads, "roads", "Whether to export roads geometry");
	flag_float(&roadsHeight, "roadsHeight", "The roads height offset (z-axis)");
	flag_float(&roadsExtrusionWidth, "roadsExtrusionWidth", "The roads extrusion width");
	flag_int(&normals, "normals", "Export with normals");
	flag_parse(argc, argv, "v" "0.1.0", 0);*/

	struct Params parameters =
	{
		"vector-tiles-qVaBcRA",		// apiKey
		tileX,						// Tile X (can be a tile range: 19294/19295)
		tileY,						// Tile Y (can be a tile range: 24642/24643)
		tileZ,						// Tile Z (zoom)
		{ 0.0f, 0.0f },				// Global tile Offset on X/Y coordinate
		false,						// splitMesh. Generate one mesh per feature in wavefront file
		aoAtlasSize, aoSamples,
		false,						// aoBaking. Generate ambiant occlusion baked atlas
		false,						// Append the obj to an existing obj file
		false,						// terrain. Generate terrain elevation topography
		terrainSubdivision, terrainExtrusionScale,
		true,						// buildings. Whether to export building geometry
		buildingsExtrusionScale,
		true,						// roads. Whether to export roads geometry
		roadsHeight, roadsExtrusionWidth,
		true,						// normals. Export with normals
		buildingsHeight, pedestal,
		pedestalHeight };

	if (!parameters.terrain && parameters.pedestal) {
		printf("Pedestal parameters can only be given when exporting with terrain (--terrain)\n");
		return EXIT_FAILURE;
	}

	if (parameters.aoBaking && (parameters.terrain || parameters.roads)) {
		printf("Ambient occlusion baking not yet available when exporting with option --terrain or --roads\n");
		return EXIT_FAILURE;
	}

	return vectiler(parameters, outFileName);
}
//-----------------------------------------------------------------------------
/*MyTile* GetTile2(const Vec3i tms)
{
const CStrL tileName = Str_Printf("%d_%d_%d", tms.x, tms.y, tms.z);
const CStrL fname = tileName + ".bin";

Vec2i google = TmsToGoogleTile(Vec2i(tileKey.x, tms.y), tms.z);
LOG("GetTile tms: <%d,%d>, google: <%d, %d>\n", tms.x, tms.y, google.x, google.y);

//if (LoadBin(fname))
//	return true;


// Mapzen uses google xyz indexing
struct Params2 params =
{
"vector-tiles-qVaBcRA",	// apiKey
tms.x,					// Tile X
tms.y,					// Tile Y
tms.z,					// Tile Z (zoom)
false,					// terrain. Generate terrain elevation topography
64,						// terrainSubdivision
1.0f,					// terrainExtrusionScale
true,					// buildings. Whether to export building geometry
10.0f,					// buildingsHeight
1.0f,					// buildingsExtrusionScale
true,					// roads. Whether to export roads geometry
0.2f,					// roadsHeight
3.0f					// roadsExtrusionWidth,
};

if (!vectiler2(params))
return NULL;

TStackArray<GGeom, 64> geoms;
if (!LoadBin(fname, geoms))
return NULL;

MyTile* tileEntity = new MyTile(tileKey);

for (int i = 0; i < geoms.Num(); i++)
{
CMesh* mesh = CreateMeshFromGeoms(geoms[i].name, &geoms[i], 1);
Entity* e = new Entity(geoms[i].name);
MeshComponent* meshcomp = e->CreateComponent<MeshComponent>();
meshcomp->m_DrawableFlags.Set(Drawable::eLightMap);
meshcomp->SetMesh(mesh, MeshComponent::eMeshDelete);
//e->SetPos(CVec3(0, 10, 0));
//gScene.AddEntity(entity);
tileEntity->AddChild(e);
}

gScene.AddEntity(tileEntity);
return tileEntity;
}*/

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
void WriteMesh(FILE* f, const PolygonMesh* mesh, const char* layerName, const int meshIdx, const int idxOffset)
{
	fprintf(f, "o %s_%d\n", layerName, meshIdx);
	for (auto v : mesh->vertices)
	{
		// JJ - flip
		fprintf(f, "v %f %f %f\n", v.position.x, v.position.z, -v.position.y);
		//file << "v " << v.position.x + offsetx + mesh.offset.x << " " << v.position.y + offsety + mesh.offset.y << " " << v.position.z << "\n";
	}
	for (auto v : mesh->vertices)
	{
		fprintf(f, "vn %f %f %f\n", v.normal.x, v.normal.z, -v.normal.y);
	}

	for (size_t i = 0; i < mesh->indices.size(); i += 3)
	{
		int idx0 = mesh->indices[i + 0] + idxOffset + 1;
		int idx1 = mesh->indices[i + 1] + idxOffset + 1;
		int idx2 = mesh->indices[i + 2] + idxOffset + 1;
		fprintf(f, "f %d//%d %d//%d %d//%d\n", idx0, idx0, idx1, idx1, idx2, idx2);
	}

	//indexOffset += mesh->vertices.size();
}
//-----------------------------------------------------------------------------
bool SaveOBJ2(const char* fname, std::vector<PolygonMesh*> meshArr[eNumLayerTypes])
{
	FILE* f = fopen(fname, "wt");
	if (!f)
		return false;

	fprintf(f, "# exported by Jerry's WorldMap\n\n");

	int statNumObjects = 0;
	int statNumVertices = 0;
	int statNumTriangles = 0;

	int idxOffset = 0;
	for (int i = 0; i < eNumLayerTypes; i++)
	{
		for (size_t k = 0; k < meshArr[i].size(); k++)
		{
			WriteMesh(f, meshArr[i][k], layerNames[i], k, idxOffset);
			idxOffset += meshArr[i][k]->vertices.size();

			statNumObjects++;
			statNumVertices += meshArr[i][k]->vertices.size();
			statNumTriangles += meshArr[i][k]->indices.size() / 3;
		}
	}

	fclose(f);

	LOG("Saved obj file: %s\n", fname);
	LOG("Objects: %ld\n", statNumObjects);
	LOG("Triangles: %ld\n", statNumTriangles);
	LOG("Vertices: %ld\n", statNumVertices);

	return true;
}
//-----------------------------------------------------------------------------
#if 0
void addFaces(std::ostream& file, const PolygonMesh& mesh, size_t indexOffset, bool normals)
{
	for (size_t i = 0; i < mesh.indices.size(); i += 3)
	{
		file << "f " << mesh.indices[i] + indexOffset + 1 << (normals ? "//" + std::to_string(mesh.indices[i] + indexOffset + 1) : "");
		file << " ";
		file << mesh.indices[i + 1] + indexOffset + 1 << (normals ? "//" + std::to_string(mesh.indices[i + 1] + indexOffset + 1) : "");
		file << " ";
		file << mesh.indices[i + 2] + indexOffset + 1 << (normals ? "//" + std::to_string(mesh.indices[i + 2] + indexOffset + 1) : "");
		file << "\n";
	}
}
//-----------------------------------------------------------------------------
void addNormals(std::ostream& file, const PolygonMesh& mesh)
{
	// JJ Flip
	for (auto v : mesh.vertices)
		file << "vn " << v.normal.x << " " << v.normal.z << " " << -v.normal.y << "\n";
	//for (auto vertex : mesh.vertices)
	//	file << "vn " << vertex.normal.x << " " << vertex.normal.y << " " << vertex.normal.z << "\n";
}
//-----------------------------------------------------------------------------
void addPositions(std::ostream& file, const PolygonMesh& mesh, float offsetx, float offsety)
{
	for (auto v : mesh.vertices)
	{
		// JJ - flip
		file << "v " << v.position.x + offsetx + mesh.offset.x << " " << v.position.z << " " << -v.position.y + offsety + mesh.offset.y << "\n";
		//file << "v " << v.position.x + offsetx + mesh.offset.x << " " << v.position.y + offsety + mesh.offset.y << " " << v.position.z << "\n";
	}
}
/*-----------------------------------------------------------------------------
* Save an obj file for the set of meshes
* - outputOBJ: the output filename of the wavefront object file
* - splitMeshes: will enable exporting meshes as single objects within
*   the wavefront file
* - offsetx/y: are global offset, additional to the inner mesh offset
* - append: option will append meshes to an existing obj file
*   (filename should be the same)
-----------------------------------------------------------------------------*/
bool saveOBJ(const char* outputOBJ,
	bool splitMeshes,
	std::vector<PolygonMesh*>& meshes,
	float offsetx,
	float offsety,
	bool append,
	bool normals)
{
	size_t maxindex = 0;

	/// Find max index from previously existing wavefront vertices
	{
		std::ifstream filein(outputOBJ, std::ios::in);
		std::string token;

		if (filein.good() && append)
		{
			// TODO: optimize this
			while (!filein.eof())
			{
				filein >> token;
				if (token == "f") {
					std::string faceLine;
					getline(filein, faceLine);

					for (unsigned int i = 0; i < faceLine.length(); ++i)
					{
						if (faceLine[i] == '/')
							faceLine[i] = ' ';
					}

					std::stringstream ss(faceLine);
					std::string faceToken;

					for (int i = 0; i < 6; ++i)
					{
						ss >> faceToken;
						if (faceToken.find_first_not_of("\t\n ") != std::string::npos)
						{
							size_t index = atoi(faceToken.c_str());
							maxindex = index > maxindex ? index : maxindex;
						}
					}
				}
			}

			filein.close();
		}
	}

	/// Save obj file
	{
		std::ofstream file(outputOBJ);

		if (append)
			file.seekp(std::ios_base::end);

		if (file.is_open())
		{
			size_t nVertex = 0;
			size_t nTriangles = 0;

			file << "# exported with vectiler: https://github.com/karimnaaji/vectiler" << "\n";
			file << "\n";

			int indexOffset = maxindex;

			if (splitMeshes)
			{
				int meshCnt = 0;

				for (const auto& mesh : meshes)
				{
					if (mesh->vertices.size() == 0) { continue; }

					file << "o mesh" << meshCnt++ << "\n";

					addPositions(file, *mesh, offsetx, offsety);
					nVertex += mesh->vertices.size();

					if (normals)
						addNormals(file, *mesh);

					addFaces(file, *mesh, indexOffset, normals);
					nTriangles += mesh->indices.size() / 3;

					file << "\n";

					indexOffset += mesh->vertices.size();
				}
			}
			else
			{
				file << "o " << outputOBJ << "\n";

				for (const auto& mesh : meshes)
				{
					if (mesh->vertices.size() == 0) continue;
					addPositions(file, *mesh, offsetx, offsety);
					nVertex += mesh->vertices.size();
				}

				if (normals)
				{
					for (const auto& mesh : meshes)
					{
						if (mesh->vertices.size() == 0) continue;
						addNormals(file, *mesh);
					}
				}

				for (const auto& mesh : meshes)
				{
					if (mesh->vertices.size() == 0) continue;
					addFaces(file, *mesh, indexOffset, normals);
					indexOffset += mesh->vertices.size();
					nTriangles += mesh->indices.size() / 3;
				}
			}

			file.close();

			// Print infos
			{
				LOG("Saved obj file: %s\n", outputOBJ);
				LOG("Triangles: %ld\n", nTriangles);
				LOG("Vertices: %ld\n", nVertex);

				std::ifstream in(outputOBJ, std::ifstream::ate | std::ifstream::binary);
				if (in.is_open())
				{
					int size = (int)in.tellg();
					LOG("File size: %fmb\n", float(size) / (1024 * 1024));
					in.close();
				}
			}

			return true;
		}
		else {
			LOG("Can't open file %s\n", outputOBJ);
		}
	}

	return false;
}
#endif


//bool AddMeshToMesh(const PolygonMesh* src, PolygonMesh* dst);
//bool SaveBin(const char* fname, std::vector<PolygonMesh*> meshArr[eNumLayerTypes]);
//bool LoadBin(const char* fname, TArray<GGeom>& geomArr);
//bool SaveBin(const char* fname, std::vector<PolygonMesh*> meshes);
//bool LoadBin(const char* fname, TArray<StreamGeom*>& geoms);// TArray<GGeom*>& geomArr);

//-----------------------------------------------------------------------------
// Separate all meshes in it's respective layer
// Merge as many small meshes together as possible (<65536 vertices)
//-----------------------------------------------------------------------------
/*int MergeLayerMeshes(const std::vector<PolygonMesh*>& meshes, std::vector<PolygonMesh*> meshArr[eNumLayerTypes])
{
int total = 0;
for (PolygonMesh* mesh : meshes)
{
std::vector<PolygonMesh*>& arr = meshArr[mesh->layerType];	// Choose correct layer
PolygonMesh* bigMesh = arr.empty() ? NULL : arr.back();
if (!bigMesh || !AddMeshToMesh(mesh, bigMesh))				// Try to add current mesh to our bigMesh
{
bigMesh = new PolygonMesh(mesh->layerType);
bigMesh->bigMeshIdx = arr.size();
arr.push_back(bigMesh);
AddMeshToMesh(mesh, bigMesh);

total++;
}
}

//for (int i = 0; i < eNumLayerTypes; i++)
//{
//	if(meshArr[i].size())
//		LOG("%s: %d meshes\n", layerNames[i], meshArr[i].size());
//}

return total;
}*/
// Merge as many small meshes together as possible (<65536 vertices)
//-----------------------------------------------------------------------------
/*void MergeMeshes(const std::vector<PolygonMesh*>& meshes, std::vector<PolygonMesh*>& merged)
{
ELayerType layer = (ELayerType)-1;
PolygonMesh* bigMesh = NULL;
for (PolygonMesh* mesh : meshes)
{
bool allocNew = false;
if (mesh->layerType != layer)
{
layer = mesh->layerType;
allocNew = true;
}
else if (!AddMeshToMesh(mesh, bigMesh))				// Try to add current mesh to our bigMesh
{
allocNew = true;
}

if (allocNew)
{
bigMesh = new PolygonMesh(layer);
bigMesh->bigMeshIdx = arr.size();
arr.push_back(bigMesh);
AddMeshToMesh(mesh, bigMesh);
}
}
}*/

//std::vector<EFeatureKind> kinds;

// Separate all meshes in it's respective layer
// Merge all meshes from same layer into a few big meshes (<=65536 vertices)
//std::vector<PolygonMesh*> meshArr[eNumLayerTypes];
//int nMerged = MergeLayerMeshes(meshes, meshArr);
//float t_MergeMeshes = sw.GetMs(true);
//LOG("Triangulated %d PolygonMeshes in %.1fms. Merged these meshes to %d meshes in %.1fms\n", meshes.size(), t_BuildMeshes, nMerged, t_MergeMeshes);

//std::vector<PolygonMesh*> merged;
//MergeMeshes(meshes, merged);
//float t_MergeMeshes = sw.GetMs(true);

//LOG("Triangulated %d PolygonMeshes in %.1fms. Merged these meshes to %d meshes in %.1fms\n", meshes.size(), t_BuildMeshes, merged.size(), t_MergeMeshes);

//-----------------------------------------------------------------------------
// TANGRAM-ES
#if 0
void buildPolygon(const Polygon2& polygon, const float height, std::vector<PolyVert>& outVertices, std::vector<unsigned int>& outIndices, const HeightData* elevation, float centroidHeight)
{
	// Run earcut
	mapbox::detail::Earcut<uint32_t> earcut;
	earcut(polygon);
	if (earcut.indices.size() == 0)
		return;

	// Count number of input vertices (not all may be used by earcut)
	int inVtxCount = 0;
	for (auto& line : polygon) {
		inVtxCount += line.size();
	}

	// Mark the points that are referenced by indices as used.
	size_t sumVertices = 0;
	std::vector<int> used;
	used.assign(inVtxCount, 0);
	for (auto i : earcut.indices) {
		if (used[i] == 0) {
			used[i] = 1;
			sumVertices++;
		}
	}

	unsigned int vtxOffset = outVertices.size();

	// Assume all vertices are used
	outVertices.reserve(outVertices.size() + inVtxCount);

	size_t ring = 0;
	size_t offset = 0;

	// Go through all points of the polyon.
	for (int src = 0, dst = 0; src < inVtxCount; src++)
	{
		// The points of the polygon rings are indexed linearly.
		// This maps the indices back to the original ring and point.
		if (src - offset >= polygon[ring].size()) {
			offset += polygon[ring].size();
			ring += 1;
		}

		// Add vertex only when the point is used.
		if (used[src] == 0) { continue; }

		// Keep track of skipped points to update indices
		used[src] = dst++;

		auto& p = polygon[ring][src - offset];
		PolyVert v;
		v.position = Vec3(p.x, p.y, height + centroidHeight);
		v.normal = Vec3(0, 0, 1);
		outVertices.push_back(v);
		/*glm::vec3 coord(p.x, p.y, _height);

		if (_ctx.useTexCoords) {
		glm::vec2 uv(mapValue(coord.x, min.x, max.x, 0., 1.),
		mapValue(coord.y, min.y, max.y, 1., 0.));

		_ctx.addVertex(coord, glm::vec3(0.0, 0.0, 1.0), uv);
		}
		else {
		_ctx.addVertex(coord, glm::vec3(0.0, 0.0, 1.0), glm::vec2(0));
		}*/
	}


	outIndices.reserve(outIndices.size() + earcut.indices.size());
	for (auto i : earcut.indices)
	{
		outIndices.push_back(vtxOffset + used[i]);
	}
}
#endif



//-----------------------------------------------------------------------------
#if 0
struct MergeInfo
{
	int bigMeshIdx;
	int voffset;
	int ioffset;
};
//-----------------------------------------------------------------------------
// Adds a mesh to a big mesh. Returns voffset & ioffs where the small mesh starts
//-----------------------------------------------------------------------------
bool AddMeshToMesh(const PolygonMesh* src, PolygonMesh* dst, int& voffset, int& ioffset)
{
	voffset = dst->vertices.size();
	ioffset = dst->indices.size();

	if (voffset + src->vertices.size() > 65536)
		return false;

	dst->vertices.insert(dst->vertices.end(), src->vertices.begin(), src->vertices.end());

	dst->indices.reserve(dst->indices.size() + src->indices.size());
	for (size_t i = 0; i < src->indices.size(); i++)
		dst->indices.push_back(voffset + src->indices[i]);

	return true;
}
//-----------------------------------------------------------------------------
// Merge as many small meshes together as possible (<65536 vertices)
//-----------------------------------------------------------------------------
MergeInfo* MergeMeshes(const std::vector<PolygonMesh*>& meshes, std::vector<PolygonMesh*>& merged)
{
	const int nMeshes = meshes.size();
	if (nMeshes == 0)
		return;

	MergeInfo* mergeInfos = new MergeInfo[nMeshes];

	ELayerType layer = (ELayerType)-1;
	PolygonMesh* bigMesh = NULL;

	for (int i = 0; i < nMeshes; i++)
	{
		const PolygonMesh* mesh = meshes[i];
		int voffs, ioffs;

		bool allocNew = false;
		if (mesh->layerType != layer)
		{
			layer = mesh->layerType;
			allocNew = true;
		}
		else if (!AddMeshToMesh(mesh, bigMesh, voffs, ioffs))				// Try to add current mesh to our bigMesh
		{
			allocNew = true;
		}

		if (allocNew)
		{
			bigMesh = new PolygonMesh(layer);
			merged.push_back(bigMesh);
			AddMeshToMesh(mesh, bigMesh, voffs, ioffs);
		}

		mergeInfos[i].bigMeshIdx = merged.size() - 1;
		mergeInfos[i].voffset = voffs;
		mergeInfos[i].ioffset = ioffs;
	}

	return mergeInfos;
}
//-----------------------------------------------------------------------------
/*
2	Ver
462 nMeshes
12	nMerged
----------------	// Big meshes
2			layer
65536		vertex count
33400		index count
v0, v1, ... vertices (PN)
i0, i1, ... indices (uint16)
----------------
...
---------------- // Meshes
0			big idx


*/
//-----------------------------------------------------------------------------
bool SaveBinMerged(const char* fname, std::vector<PolygonMesh*> meshes)
{
	CStopWatch sw;

	CFile f;
	if (!f.Open(fname, FILE_WRITE))
		return false;

	// Merge meshes
	std::vector<PolygonMesh*> merged;
	MergeInfo* mergeInfos = MergeMeshes(meshes, merged);

	TArray<uint16> idx16bit;

	const int nMeshes = meshes.size();
	const int nMerged = merged.size();

	f.WriteInt(VER);
	f.WriteInt(nMeshes);
	f.WriteInt(nMerged);

	for (int i = 0; i < nMerged; i++)
	{
		PolygonMesh* mesh = merged[i];
		f.WriteInt(mesh->layerType);
		f.WriteInt(mesh->vertices.size());
		f.WriteInt(mesh->indices.size());

		for (auto v : mesh->vertices)
		{
			// Flip
			float p[6];
			p[0] = v.position.x;
			p[1] = v.position.z;
			p[2] = -v.position.y;
			p[3] = v.normal.x;
			p[4] = v.normal.z;
			p[5] = -v.normal.y;
			f.Write(p, sizeof(float) * 6);
		}

		const int ni = mesh->indices.size();
		idx16bit.SetNum(ni);
		for (int i = 0; i < ni; i++)
			idx16bit[i] = mesh->indices[i];

		f.Write(idx16bit.Ptr(), ni * sizeof(uint16));
	}

	int statNumVertices = 0;
	int statNumTriangles = 0;

	CStrL meshname;
	for (int i = 0; i < nMeshes; i++)
	{
		const PolygonMesh* mesh = meshes[i];
		const Feature* feature = mesh->feature;
		MergeInfo* mi = &mergeInfos[i];

		f.WriteInt(mi->bigMeshIdx);

		int sort;
		int kind;
		if (feature)
		{
			meshname = feature->name;
			sort = feature->sort_rank;
			kind = feature->kind;
		}
		else
		{
			meshname = "";
			sort = 0;
			kind = eKindUnknown;
		}

		f.WriteInt(kind);
		f.WriteInt(sort);
		f.WriteInt(meshname.Len());
		f.Write(meshname.Str(), meshname.Len() * sizeof(char));
		f.WriteInt(mesh->vertices.size());
		f.WriteInt(mesh->indices.size());

		for (auto v : mesh->vertices)
		{
			// Flip
			float p[6];
			p[0] = v.position.x;
			p[1] = v.position.z;
			p[2] = -v.position.y;
			p[3] = v.normal.x;
			p[4] = v.normal.z;
			p[5] = -v.normal.y;
			f.Write(p, sizeof(float) * 6);
		}

		f.Write(mesh->indices.data(), mesh->indices.size() * sizeof(int));
		statNumVertices += mesh->vertices.size();
		statNumTriangles += mesh->indices.size() / 3;
	}

	LOG("Saved %s in %.1fms. Meshes: %d, Tris: %d, Vtx: %d\n", fname, sw.GetMs(), nMeshes, statNumTriangles, statNumVertices);

	return true;
}
#endif