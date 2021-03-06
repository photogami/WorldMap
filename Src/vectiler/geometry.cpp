#include "Precompiled.h"
#include "geometry.h"
#include "tiledata.h"
//#include "earcut.hpp"
//#include "earcut.h"
//#include "../../../../Source/Engine/Framework/Geomgen.h"
//-----------------------------------------------------------------------------
#define EPSILON 1e-5f
//-----------------------------------------------------------------------------
static inline float GetWidth(const ELayerType l, const EFeatureKind k)
{
	static MapGeom default(Crgba(255, 0, 0), 0.1f, false);
	MapGeom* g = gGeomHash.GetValuePtr((l << 16 | k));
	return g ? g->width : default.width;
}
//-----------------------------------------------------------------------------
#ifdef KEEP_2D
	static inline v2 perp(const v2& v) { return Normalize(v2(-v.y, v.x)); }
	//-----------------------------------------------------------------------------
	v2 computeMiterVector(const v2& d0, const v2& d1, const v2& n0, const v2& n1)
	{
		v2 miter = Normalize(n0 + n1);
		float miterl2 = dot(miter, miter);

		if (miterl2 < std::numeric_limits<float>::epsilon()) {
			miter = v2(n1.y - n0.y, n0.x - n1.x);
		}
		else {
			float theta = atan2f(d1.y, d1.x) - atan2f(d0.y, d0.x);
			if (theta < 0.f)
				theta += 2.0f * FLOAT_PI;
			miter *= 1.f / std::max<float>(sin(theta * 0.5f), EPSILON);
		}

		return miter;
	}
#else
	static inline v3 perp(const v3& v) { return Normalize(v3(-v.y, v.x, 0.0)); }
	//-----------------------------------------------------------------------------
	v3 computeMiterVector(const v3& d0, const v3& d1, const v3& n0, const v3& n1)
	{
		v3 miter = Normalize(n0 + n1);
		float miterl2 = dot(miter, miter);

		if (miterl2 < std::numeric_limits<float>::epsilon()) {
			miter = v3(n1.y - n0.y, n0.x - n1.x, 0.0);
		}
		else {
			float theta = atan2f(d1.y, d1.x) - atan2f(d0.y, d0.x);
			if (theta < 0.f)
				theta += 2.0f * FLOAT_PI;
			miter *= 1.f / std::max<float>(sin(theta * 0.5f), EPSILON);
		}

		return miter;
	}
#endif
//-----------------------------------------------------------------------------
void computeNormals(PolygonMesh* mesh)
{
	for (int i = 0; i < mesh->indices.size(); i += 3)
	{
		int i1 = mesh->indices[i + 0];
		int i2 = mesh->indices[i + 1];
		int i3 = mesh->indices[i + 2];

		const v3& p1 = mesh->vertices[i1].position;
		const v3& p2 = mesh->vertices[i2].position;
		const v3& p3 = mesh->vertices[i3].position;

		v3 d = Normalize(cross(p2 - p1, p3 - p1));

		mesh->vertices[i1].normal += d;
		mesh->vertices[i2].normal += d;
		mesh->vertices[i3].normal += d;
	}

	for (auto& v : mesh->vertices) {
		v.normal = Normalize(v.normal);
	}
}
/*-----------------------------------------------------------------------------
* Sample elevation using bilinear texture sampling
* - position: must lie within tile range [-1.0, 1.0]
* - textureData: the elevation tile data, may be null
//-----------------------------------------------------------------------------*/
float sampleElevation(v2 position, const HeightData* heightMap)
{
	if (!heightMap)
		return 0.0f;
	
	position.x = Clamp(position.x, -1.0f, 1.0f);
	position.y = Clamp(position.y, -1.0f, 1.0f);

	// Normalize vertex coordinates into the texture coordinates range
	float u = (position.x * 0.5f + 0.5f) * heightMap->width;
	float v = (position.y * 0.5f + 0.5f) * heightMap->height;

	// Flip v coordinate according to tile coordinates
	v = heightMap->height - v;

	float alpha = u - floor(u);
	float beta = v - floor(v);

	int ii0 = floor(u);
	int jj0 = floor(v);
	int ii1 = ii0 + 1;
	int jj1 = jj0 + 1;

	// Clamp on borders
	ii0 = Min(ii0, heightMap->width - 1);
	jj0 = Min(jj0, heightMap->height - 1);
	ii1 = Min(ii1, heightMap->width - 1);
	jj1 = Min(jj1, heightMap->height - 1);

	// Sample four corners of the current texel
	float s0 = heightMap->elevation[ii0][jj0];
	float s1 = heightMap->elevation[ii0][jj1];
	float s2 = heightMap->elevation[ii1][jj0];
	float s3 = heightMap->elevation[ii1][jj1];

	// Sample the bilinear height from the elevation texture
	float bilinearHeight = (1 - beta) * (1 - alpha) * s0
		+ (1 - beta) * alpha       * s1
		+ beta       * (1 - alpha) * s2
		+ alpha      * beta        * s3;

	return bilinearHeight;
}
//-----------------------------------------------------------------------------
v2 centroid(const Polygon2& polygon)
{
	v2 centroid(0, 0);
	int n = 0;

	for (auto& l : polygon)
	{
		for (auto& p : l)
		{
			centroid.x += p.x;
			centroid.y += p.y;
			n++;
		}
	}

	if (n > 0)
		centroid /= n;

	return centroid;
}
float t_extrusion = 0; 
// 64.2, 69.8
// 60.0, 62.0, 62.63
// 56.75, 57.23
// 58.42
//-----------------------------------------------------------------------------
float buildPolygonExtrusion(const Polygon2& polygon, const float minHeight,	const float height,	VtxArr& outVertices, IdxArr& outIndices, const HeightData* elevation)
{
	CStopWatch sw;
	int voffs = outVertices.size();
	float minz = 0.f;
	float cz = 0.f;

	// Compute min and max height of the polygon
	if (elevation)
	{
		// The polygon centroid height
		cz = sampleElevation(centroid(polygon), elevation);
		minz = F32_MOST_POS;// std::numeric_limits<float>::max();

		for (auto& linestring : polygon)
		{
			for (size_t i = 0; i < linestring.size(); i++)
			{
				Point p(linestring[i]);
				float pz = sampleElevation(v2(p.x, p.y), elevation);
				minz = Min(minz, pz);
			} 
		}
	}

	const v3 upVector(0.0f, 0.0f, 1.0f);
	for (auto& linestring : polygon)
	{
		const size_t lineSize = linestring.size();

		for (size_t i = 0; i < lineSize - 1; i++)
		{
			//v3 a(linestring[i]);
			//v3 b(linestring[i + 1]);
			v3 a = Vec3(linestring[i].x, linestring[i].y, 0.0f);		// No automatic Vec3(Vec2) constructor
			v3 b = Vec3(linestring[i+1].x, linestring[i+1].y, 0.0f);

			//if (a == b) { continue; } // Should never happen since we deal with this when creating GeoJson structures

			const v3 normalVector = Normalize(cross(upVector, b - a));

			PolyVert* _v = outVertices.AddEmpty(4);
			a.z = height + cz;		_v[0] = { a, normalVector };
			b.z = height + cz;		_v[1] = { b, normalVector };
			a.z = minHeight + minz;	_v[2] = { a, normalVector };
			b.z = minHeight + minz;	_v[3] = { b, normalVector };
	
			uint32* _i = outIndices.AddEmpty(6);
			_i[0] = voffs + 0;
			_i[1] = voffs + 1;
			_i[2] = voffs + 2;
			_i[3] = voffs + 1;
			_i[4] = voffs + 3;
			_i[5] = voffs + 2;

			voffs += 4;
		}
	}

	t_extrusion += sw.GetMs();
	return cz;
}
//-----------------------------------------------------------------------------
// OLD EARCUT
#if 0
void buildPolygon(const Polygon2& polygon, const float height, std::vector<PolyVert>& outVertices, std::vector<unsigned int>& outIndices, const HeightData* elevation, float centroidHeight)
{
	mapbox::Earcut<float, unsigned int> earcut;
	earcut(polygon);
	
	if (earcut.indices.size() == 0)
		return;

	unsigned int vertexDataOffset = outVertices.size();

	if (vertexDataOffset == 0)
	{
		outIndices = std::move(earcut.indices);
	}
	else
	{
		outIndices.reserve(outIndices.size() + earcut.indices.size());
		for (auto i : earcut.indices)
		{
			outIndices.push_back(vertexDataOffset + i);
		}
	}

	static v3 normal(0.0, 0.0, 1.0);
	outVertices.reserve(outVertices.size() + earcut.vertices.size());

	for (auto& p : earcut.vertices)
	{
		v2 position(p[0], p[1]);
		v3 coord(position.x, position.y, height + centroidHeight);
		outVertices.push_back({ coord, normal });
	}
}
#endif

float timetmp[7] = { 0,0,0,0,0,0,0 };
//-----------------------------------------------------------------------------
// JJ NEW EARCUT
#if 1
void buildPolygon(const Polygon2& polygon, const float height, VtxArr& outVertices, IdxArr& outIndices, const HeightData* elevation, const float centroidHeight, PolyMeshBuilder& ctx)
{
	ctx.Clear();

	CStopWatch sw;
	// Run earcut
	ctx.earcut(polygon);
	const int ni = ctx.earcut.indices.size();
	
	timetmp[0] += sw.GetMs(true);
	
	if (ni == 0)
		return;

	// 6.43
	// 6.62
	
	// Count number of input vertices (not all may be used by earcut)
	/*int inVtxCount = 0;
	for (auto& line : polygon) {
		inVtxCount += line.size();
	}*/

	// Flatten. An array wich maps from a linear idx to specific ring/point
	//ctx.flatten.SetNum(inVtxCount);
	int n = 0;
	for (size_t r=0; r<polygon.size(); r++)
	{
		const auto& ring = polygon[r];
		for (size_t p = 0; p < ring.size(); p++)
			ctx.flatten.Add(Vec2i(r, p));
			//ctx.flatten[n++] = Vec2i(r, p);
	}

	timetmp[1] += sw.GetMs(true);

	const int inVtxCount = ctx.flatten.Num();
	const int vtxOffset = outVertices.size();

	timetmp[2] += sw.GetMs(true);

	// Remap
	ctx.remap.SetNum(inVtxCount);
	for (int i = 0; i < inVtxCount; i++)
		ctx.remap[i] = -1;

	timetmp[3] += sw.GetMs(true);
	
	int newVerts = 0;
	for (int i = 0; i < ni; i++)
	{
		const uint32_t old_idx = ctx.earcut.indices[i];
		if (ctx.remap[old_idx] == -1)
			ctx.remap[old_idx] = newVerts++;
	}

	timetmp[4] += sw.GetMs(true);

	// Move used vertices
#if 1
	PolyVert* _v = outVertices.AddEmpty(newVerts);
	for (int oldidx = 0; oldidx < inVtxCount; oldidx++)
	{
		const int newIdx = ctx.remap[oldidx];
		if (newIdx >= 0)
		{
			const Vec2i& ii = ctx.flatten[oldidx];
			const Point& p = polygon[ii.x][ii.y];
			PolyVert& v = _v[newIdx];
			v.position = Vec3(p.x, p.y, height + centroidHeight);
			v.normal = Vec3(0, 0, 1);
		}
	}
#endif

	timetmp[5] += sw.GetMs(true);

	// 13.34
	// 12.76
	// 12.45
	//7.63
	//8.39
	//outIndices.reserve(outIndices.size() + ni);
	//for (auto i : ctx.earcut.indices)
	//{
	//	outIndices.push_back(vtxOffset + ctx.remap[i]);
	//}
	uint32* _idx = outIndices.AddEmpty(ni);
	for (int i = 0; i < ni; i++)
		_idx[i] = vtxOffset + ctx.remap[ctx.earcut.indices[i]];
	

	timetmp[6] += sw.GetMs(true);
}
#endif
//-----------------------------------------------------------------------------
#if 0
void addPolygonPolylinePoint(LineString& linestring,
	const Point& curr,
	const Point& next,
	const Point& last,
	const float extrude,
	const size_t lineDataSize,
	const size_t i,
	const bool forward)
{
	const Point n0 = perp(curr - last);
	const Point n1 = perp(next - curr);
	bool right = cross(n1, n0).z > 0.0;

	if ((i == 1 && forward) || (i == lineDataSize - 2 && !forward))
	{
		linestring.push_back(last + n0 * extrude);
		linestring.push_back(last - n0 * extrude);
	}

	if (right)
	{
		Point d0 = Normalize(last - curr);
		Point d1 = Normalize(next - curr);
		Point miter = computeMiterVector(d0, d1, n0, n1);
		linestring.push_back(curr - miter * extrude);
	}
	else
	{
		linestring.push_back(curr - n0 * extrude);
		linestring.push_back(curr - n1 * extrude);
	}
}
//-----------------------------------------------------------------------------
void Old_BuildPolyLine(const ELayerType layerType, const Feature* f, const float sortHeight, PolygonMesh* mesh, const HeightData* heightMap)
{
	CStopWatch sw, sw1;
	float t1 = 0;
	float t2 = 0;
	const float extrudeW = GetWidth(layerType, f->kind);
	const float extrudeH = sortHeight;
	//if (extrudeW == 0.1f)
	//{
	//	int abba = 10;
	//}

	Polygon2 polygon;
	polygon.emplace_back();
	LineString& polygonLine = polygon.back();

	for (const LineString& linestring : f->lineStrings)
	{
		polygonLine.clear();
		const size_t n = linestring.size();

		sw1.Start();
		if (n == 2)
		{
			const Point& curr = linestring[0];
			const Point& next = linestring[1];
			Point n0 = perp(next - curr);
			n0 *= extrudeW;

			polygonLine.push_back(curr - n0);
			polygonLine.push_back(curr + n0);
			polygonLine.push_back(next + n0);
			polygonLine.push_back(next - n0);
		}
		else
		{
			Point last = linestring[0];
			for (size_t i = 1; i < n - 1; ++i)
			{
				const Point& curr = linestring[i];
				const Point& next = linestring[i + 1];
				addPolygonPolylinePoint(polygonLine, curr, next, last, extrudeW, n, i, true);
				last = curr;
			}

			last = linestring[n - 1];
			for (int i = n - 2; i > 0; --i)
			{
				const Point& curr = linestring[i];
				const Point& next = linestring[i - 1];
				addPolygonPolylinePoint(polygonLine, curr, next, last, extrudeW, n, i, false);
				last = curr;
			}

			std::reverse(polygonLine.begin(), polygonLine.end());
		}

		if (polygonLine.size() < 4) { continue; }

		/*int count = 0;
		for (size_t i = 0; i < polygonLine.size(); i++)
		{
		int j = (i + 1) % polygonLine.size();
		int k = (i + 2) % polygonLine.size();
		double z = (polygonLine[j].x - polygonLine[i].x)
		* (polygonLine[k].y - polygonLine[j].y)
		- (polygonLine[j].y - polygonLine[i].y)
		* (polygonLine[k].x - polygonLine[j].x);
		if (z < 0) { count--; }
		else if (z > 0) { count++; }
		}

		if (line.size() == 2 && count > 0)
		{
		int abba = 10;
		}
		if (line.size() > 2 && count <= 0)
		{
		int abba = 10;
		}

		if (count > 0) { // CCW

		if (line.size() == 2)
		{
		int abba = 10;
		}

		std::reverse(polygonLine.begin(), polygonLine.end());
		}*/

		// Close the polygon
		polygonLine.push_back(polygonLine[0]);

		const size_t offset = mesh->vertices.size();
		t1 += sw1.GetMs(true);

		if (extrudeH > 0)
			buildPolygonExtrusion(polygon, 0.0f, extrudeH, mesh->vertices, mesh->indices, nullptr);

		buildPolygon(polygon, extrudeH, mesh->vertices, mesh->indices, nullptr, 0.f);

		t2 += sw1.GetMs();

		if (heightMap)
		{
			for (auto it = mesh->vertices.begin() + offset; it != mesh->vertices.end(); ++it)
			{
				it->position.z += sampleElevation(v2(it->position.x, it->position.y), heightMap);
			}
		}
	}

	//if (params.terrain)
	if (heightMap)
		computeNormals(mesh);

	float time = sw.GetMs();
	if (time > 50)
	{
		LOG("Built mesh from featId(%I64d). Time %.1fms. V: %d, T: %d\n", f->id, time, mesh->vertices.size(), mesh->indices.size() / 3);
		LOG("LineBuilder: %.1f. PolyBuilder: %.1f\n", t1, t2);
		//LOG("FeatureType: %d\n", feature.geometryType);
	}
}
#endif

float t_poly = 0;
float t_line = 0;
//-----------------------------------------------------------------------------
PolygonMesh* CreatePolygonMeshFromFeature(const ELayerType layerType, const Feature* f, const HeightData* heightMap, PolyMeshBuilder& ctx)
{
	CStopWatch sw;

	//if (strstr(f->name.Str(), "Deutsches Patent") != 0)
	//if (f->min_height > f->height)
	//if (f->lineStrings.size() > 1000)//3000)
	//{
	//	int abba = 10;
	//}

	PolygonMesh* mesh = new PolygonMesh(layerType, f);
	const float sortHeight = 0;// f->sort_rank / MAX_SORT_RANK;

	if (f->geometryType == GeometryType::polygons)
	{
		CStopWatch sw;
		for (const Polygon2& polygon : f->polygons)
		{
			float centroidHeight = 0.f;
			if (f->min_height != f->height)
			{
				centroidHeight = buildPolygonExtrusion(polygon, f->min_height, f->height, mesh->vertices, mesh->indices, heightMap);
				buildPolygon(polygon, f->height, mesh->vertices, mesh->indices, heightMap, centroidHeight, ctx);
			}
			else
				buildPolygon(polygon, f->height + sortHeight, mesh->vertices, mesh->indices, heightMap, centroidHeight, ctx);
		}

		t_poly += sw.GetMs();
	}
	else if (f->geometryType == GeometryType::lines)
	{
		CStopWatch sw;
		//Old_BuildPolyLine(layerType, f, sortHeight, mesh, heightMap);


		// First pass to estimate lower bounds for verts & idxs
		int vc = 0;
		int ic = 0;
		for (const LineString& linestring : f->lineStrings)
		{
			int _vc, _ic;
			CGeomGen::PolyLine_MinimumStats(linestring.size(), _vc, _ic);
			vc += _vc;
			ic += _ic;
		}

		const float extrudeW = GetWidth(layerType, f->kind);
		TArray<Vec2> verts(vc);
		TArray<uint16> idxs(ic);
		for (const LineString& linestring : f->lineStrings)
		{
			CGeomGen::PolyLine(&linestring.front(), linestring.size(), extrudeW, verts, idxs);
		}

		assert(verts.Num() < 65536);

		vc = verts.Num();
		mesh->vertices.SetNum(vc);
		for (int i = 0; i < vc; i++)
		{
			mesh->vertices[i].position = Vec3(verts[i].x, verts[i].y, 0.0f);
			mesh->vertices[i].normal = Vec3(0, 0, 1);
		}

		ic = idxs.Num();
		mesh->indices.SetNum(ic);
		for (int i = 0; i < ic; i++)
			mesh->indices[i] = idxs[i];

		t_line += sw.GetMs();
	}

	if (mesh->vertices.size() == 0)
	{
		delete mesh;
		mesh = NULL;
	}

	return mesh;
}