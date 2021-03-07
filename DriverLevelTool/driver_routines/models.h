#ifndef MODEL_H
#define MODEL_H

#include <string>

#include "math/dktypes.h"
#include "math/psx_math_types.h"
#include "d2_types.h"
#include "util/DkList.h"

#define MAX_MODELS				1536	// maximum models (this is limited by PACKED_CELL_OBJECT)

//------------------------------------------------------------------------------------------------------------

// forward
class IVirtualStream;
struct RegionModels_t;

struct dpoly_t
{
	ubyte	flags;
	ubyte	page;
	ubyte	detail;

	ubyte	vindices[4];
	ubyte	uv[4][2];
	ubyte	nindices[4];
	CVECTOR	color[4];
	// something more?
};

enum EFaceFlags_e
{
	FACE_IS_QUAD			= (1 << 0),
	FACE_RGB				= (1 << 1),	// this face has a color data
	FACE_TEXTURED			= (1 << 2),	// face is textured
	FACE_VERT_NORMAL		= (1 << 3),
};

struct ModelRef_t
{
	ModelRef_t()
	{
		model = nullptr;
		userData = nullptr;
	}

	MODEL*	model;
	int		index;
	int		size;
	bool	swap;
	
	void*	userData; // might contain a hardware model pointer
};

//------------------------------------------------------------------------------------------------------------

struct CarModelData_t
{
	MODEL* cleanmodel;
	MODEL* dammodel;
	MODEL* lowmodel;

	int cleanSize;
	int damSize;
	int lowSize;
};

class CDriverLevelModels
{
public:
	CDriverLevelModels();
	virtual ~CDriverLevelModels();

	// release all data
	void				FreeAll();

	void				LoadCarModelsLump(IVirtualStream* pFile, int size);
	void				LoadModelNamesLump(IVirtualStream* pFile, int size);
	void				LoadLevelModelsLump(IVirtualStream* pFile);

	ModelRef_t*			GetModelByIndex(int nIndex, RegionModels_t* models) const;
	int					FindModelIndexByName(const char* name) const;
	const char*			GetModelName(ModelRef_t* model) const;

	CarModelData_t*		GetCarModel(int index) const;
	
protected:
	ModelRef_t			m_levelModels[MAX_MODELS];
	CarModelData_t		m_carModels[MAX_CAR_MODELS];
	DkList<std::string>	m_model_names;
};

//------------------------------------------------------------------------------------------------------------

void			PrintUnknownPolys();
int				decode_poly(const char* face, dpoly_t* out);

//-------------------------------------------------------------------------------

#endif // MODEL_H
