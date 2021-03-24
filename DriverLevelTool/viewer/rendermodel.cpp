#include "driver_routines/models.h"
#include "rendermodel.h"

#include "gl_renderer.h"
#include "core/cmdlib.h"

#include <assert.h>

void AddExtentVertex(Vector3D& minPoint, Vector3D& maxPoint, const Vector3D& v)
{
	if (v.x < minPoint.x)
		minPoint.x = v.x;

	if (v.x > maxPoint.x)
		maxPoint.x = v.x;

	if (v.y < minPoint.y)
		minPoint.y = v.y;

	if (v.y > maxPoint.y)
		maxPoint.y = v.y;

	if (v.z < minPoint.z)
		minPoint.z = v.z;

	if (v.z > maxPoint.z)
		maxPoint.z = v.z;
}

CRenderModel::CRenderModel()
{
}

CRenderModel::~CRenderModel()
{
	Destroy();
}

bool CRenderModel::Initialize(ModelRef_t* model)
{
	if (!model)
		return false;

	if (!model->model)
		return false;

	m_extMin = Vector3D(V_MAX_COORD);
	m_extMax = Vector3D(-V_MAX_COORD);
	
	m_sourceModel = model;
	GenerateBuffers();

	return true;
}

void CRenderModel::Destroy()
{
	GR_DestroyVAO(m_vao);
	m_vao = nullptr;
	m_sourceModel = nullptr;
	m_batches.clear();
}

struct vertexTuple_t
{
	int		flags;				// store few face flags here

	short	grVertexIndex;

	short	vertexIndex;
	short	normalIndex;		// point normal index
	ushort	uvs;
};

int FindGrVertexIndex(const Array<vertexTuple_t>& whereFind, int flags, int vertexIndex, int normalIndex, ushort uvs)
{
	for(usize i = 0; i < whereFind.size(); i++)
	{
		if (whereFind[i].flags != flags)
			continue;
		
		if (flags & FACE_VERT_NORMAL)
		{
			if (whereFind[i].normalIndex != normalIndex)
				continue;
		}
		else
		{
			return -1;
		}

		if (flags & FACE_TEXTURED)
		{
			if (whereFind[i].uvs != uvs)
				continue;
		}

		if (whereFind[i].vertexIndex == vertexIndex)
			return whereFind[i].grVertexIndex;
	}

	return -1;
}

struct genBatch_t
{
	Array<int>				indices;
	
	int tpage;
};

genBatch_t* FindBatch(Array<genBatch_t*>& batches, int tpageId)
{
	for (usize i = 0; i < batches.size(); i++)
	{
		if (batches[i]->tpage == tpageId)
			return batches[i];
	}
	return nullptr;
}

void CRenderModel::GenerateBuffers()
{
	Array<genBatch_t*>		batches;
	Array<GrVertex>		vertices;
	Array<vertexTuple_t>	verticesMap;
	
	MODEL* model = m_sourceModel->model;
	MODEL* vertex_ref = model;

	if (m_sourceModel->baseInstance) // car models have vertex_ref=0
	{
		vertex_ref = m_sourceModel->baseInstance->model;
	}

	genBatch_t* batch = nullptr;
	
	int modelSize = m_sourceModel->size;
	int face_ofs = 0;
	dpoly_t dec_face;

	vertices.reserve(model->num_vertices);

	// go through all polygons
	for (int i = 0; i < model->num_polys; i++)
	{
		char* facedata = model->pPolyAt(face_ofs);

		// check offset
		if ((ubyte*)facedata >= (ubyte*)model + modelSize)
		{
			MsgError("poly id=%d type=%d ofs=%d bad offset!\n", i, *facedata & 31, model->poly_block + face_ofs);
			break;
		}

		int poly_size = decode_poly(facedata, &dec_face);

		// check poly size
		if (poly_size == 0)
		{
			MsgError("poly id=%d type=%d ofs=%d zero size!\n", i, *facedata & 31, model->poly_block + face_ofs);
			break;
		}

		face_ofs += poly_size;

		int numPolyVerts = (dec_face.flags & FACE_IS_QUAD) ? 4 : 3;
		bool bad_face = false;

		// perform vertex checks
		for (int v = 0; v < numPolyVerts; v++)
		{
			if (dec_face.vindices[v] >= vertex_ref->num_vertices)
			{
				bad_face = true;
				break;
			}

			// also check normals
			if (dec_face.flags & FACE_VERT_NORMAL)
			{
				if (dec_face.nindices[v] >= vertex_ref->num_point_normals)
				{
					bad_face = true;
					break;
				}
			}
		}

		if (bad_face)
		{
			MsgError("poly id=%d type=%d ofs=%d has invalid indices (or format is unknown)\n", i, *facedata & 31, model->poly_block + face_ofs);
			
			continue;
		}

		// find or create new batch
		int tpageId = (dec_face.flags & FACE_TEXTURED) ? dec_face.page : -1;

		if (tpageId == 255)
			tpageId = -1;
		
		if(batch && batch->tpage != tpageId)
			batch = FindBatch(batches, tpageId);
		
		if (!batch)
		{
			batch = new genBatch_t;
			batch->tpage = tpageId;
	
			batches.append(batch);
		}
		
		// Gouraud-shaded poly smoothing
		bool smooth = (dec_face.flags & FACE_VERT_NORMAL);

		int faceIndices[4];

		// add vertices and generate faces
		for (int v = 0; v < numPolyVerts; v++)
		{
			// NOTE: Vertex indexes is reversed here
#define VERT_IDX v//numPolyVerts - 1 - v

			int vflags = dec_face.flags & ~(FACE_IS_QUAD | FACE_RGB);
			
			// try searching for vertex
			int index = FindGrVertexIndex(verticesMap,
				vflags,
				dec_face.vindices[VERT_IDX], 
				dec_face.nindices[VERT_IDX], 
				*(ushort*)dec_face.uv[VERT_IDX]);

			// add new vertex
			if(index == -1)
			{
				GrVertex newVert;
				vertexTuple_t vertMap;

				vertMap.flags = vflags;
				vertMap.normalIndex = -1;
				vertMap.vertexIndex = dec_face.vindices[VERT_IDX];
				vertMap.uvs = *(ushort*)dec_face.uv[VERT_IDX];
				
				// get the vertex
				SVECTOR* vert = vertex_ref->pVertex(dec_face.vindices[VERT_IDX]);
				Vector3D fVert = Vector3D(vert->x * RENDER_SCALING, vert->y * -RENDER_SCALING, vert->z * RENDER_SCALING);
				
				(*(Vector3D*)&newVert.vx) = fVert;

				AddExtentVertex(m_extMin, m_extMax, fVert);

				if (smooth)
				{
					vertMap.normalIndex = dec_face.nindices[VERT_IDX];
					
					SVECTOR* norm = vertex_ref->pPointNormal(vertMap.normalIndex);
					*(Vector3D*)&newVert.nx = Vector3D(norm->x * RENDER_SCALING, norm->y * -RENDER_SCALING, norm->z * RENDER_SCALING);
				}
				
				if (dec_face.flags & FACE_TEXTURED)
				{
					UV_INFO uv = *(UV_INFO*)dec_face.uv[VERT_IDX];

					// map to 0..1
					newVert.tc_u = ((float)uv.u + 0.5f) / 256.0f;
					newVert.tc_v = ((float)uv.v + 0.5f) / 256.0f;
				}

				index = vertMap.grVertexIndex = vertices.size();
				vertices.append(newVert);

				// add vertex and a map
				verticesMap.append(vertMap);

				// vertices and verticesMap should be equal
				assert(verticesMap.size() == vertices.size());
			}
			
			// add index
			faceIndices[v] = index;
		}

		// if not gouraud shaded we just compute face normal
		// FIXME: make it like game does?
		if(!smooth)
		{
			// it takes only triangle
			Vector3D v0 = *(Vector3D*)&vertices[faceIndices[0]].vx;
			Vector3D v1 = *(Vector3D*)&vertices[faceIndices[1]].vx;
			Vector3D v2 = *(Vector3D*)&vertices[faceIndices[2]].vx;
			
			Vector3D normal = normalize(cross(v2 - v1, v0 - v1));

			// set to each vertex
			for (int v = 0; v < numPolyVerts; v++)
				*(Vector3D*)&vertices[faceIndices[v]].nx = normal;
		}

		// triangulate quads
		if(numPolyVerts == 4)
		{
			batch->indices.append(faceIndices[0]);
			batch->indices.append(faceIndices[1]);
			batch->indices.append(faceIndices[2]);

			batch->indices.append(faceIndices[2]);
			batch->indices.append(faceIndices[3]);
			batch->indices.append(faceIndices[0]);
		}
		else
		{
			batch->indices.append(faceIndices[0]);
			batch->indices.append(faceIndices[1]);
			batch->indices.append(faceIndices[2]);
		}
	}

	Array<int> indices;

	// merge batches
	for(usize i = 0; i < batches.size(); i++)
	{	
		//int startVertex = vertices.numElem();
		int startIndex = indices.size();
		
		//vertices.append(batches[i]->vertices);

		for(usize j = 0; j < batches[i]->indices.size(); j++)
			indices.append(batches[i]->indices[j]);

		modelBatch_t batch;
		batch.startIndex = startIndex;
		batch.numIndices = batches[i]->indices.size();
		batch.tpage = batches[i]->tpage;

		m_batches.append(batch);
		
		delete batches[i];
	}
	
	m_vao = GR_CreateVAO(vertices.size(), indices.size(), (GrVertex*)vertices, (int*)indices, 0);

	if(!m_vao)
	{
		MsgError("Cannot create Model VAO!\n");
	}
}

void CRenderModel::Draw()
{
	extern TextureID GetHWTexture(int tpage, int pal);
	extern void SetupModelShader();
	
	SetupModelShader();
	GR_SetVAO(m_vao);
	
	for(usize i = 0; i < m_batches.size(); i++)
	{
		modelBatch_t& batch = m_batches[i];
		
		GR_SetTexture(GetHWTexture(batch.tpage, 0));
		GR_DrawIndexed(PRIM_TRIANGLES, batch.startIndex, batch.numIndices);
	}
}

void CRenderModel::GetExtents(Vector3D& outMin, Vector3D& outMax) const
{
	outMin = m_extMin;
	outMax = m_extMax;
}