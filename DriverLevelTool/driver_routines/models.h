#ifndef MODEL_H
#define MODEL_H

#include <nstd/String.hpp>
#include <nstd/Array.hpp>

#include "core/dktypes.h"
#include "math/psx_math_types.h"
#include "d2_types.h"

#define MAX_MODELS				1536	// maximum models (this is limited by PACKED_CELL_OBJECT)

//------------------------------------------------------------------------------------------------------------

// forward
class IVirtualStream;
struct ModelRef_t;
struct CarModelData_t;

//------------------------------------------------------------------------------------------------------------

typedef void (*OnModelLoaded_t)(ModelRef_t* tp);
typedef void (*OnModelFreed_t)(ModelRef_t* tp);

typedef void (*OnCarModelLoaded_t)(CarModelData_t* tp);
typedef void (*OnCarModelFreed_t)(CarModelData_t* tp);

struct dpoly_t
{
	ubyte	flags;
	ubyte	page;
	ubyte	detail;

	ubyte	vindices[4];
	ubyte	uv[4][2];
	ubyte	nindices[4];
	CVECTOR	color;
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
	ModelRef_t* baseInstance{ nullptr };

	const char* name{ nullptr };
	MODEL*		model{ nullptr };

	short		index{ -1 };
	ushort		size{ 0 };

	ushort		highDetailId{ 0xffff };
	ushort		lowDetailId{ 0xffff };

	float		lightingLevel{ 1.0f };
	
	void*		userData{ nullptr }; // might contain a hardware model pointer

	bool		enabled { true };
};

//------------------------------------------------------------------------------------------------------------

struct CarModelData_t
{
	MODEL* cleanmodel{ nullptr };
	MODEL* dammodel{ nullptr };
	MODEL* lowmodel{ nullptr };

	int cleanSize{ 0 };
	int damSize{ 0 };
	int lowSize{ 0 };
};

class CDriverLevelModels
{
	friend class CBaseLevelMap;
public:
	CDriverLevelModels();
	virtual ~CDriverLevelModels();

	// release all data
	void				FreeAll();

	//----------------------------------------------
	void				SetModelLoadingCallbacks(OnModelLoaded_t onLoaded, OnModelFreed_t onFreed);
	void				SetCarModelLoadingCallbacks(OnCarModelLoaded_t onLoaded, OnCarModelFreed_t onFreed);

	//----------------------------------------------
	
	void				LoadCarModelsLump(IVirtualStream* pFile, int size);
	void				LoadModelNamesLump(IVirtualStream* pFile, int size);
	void				LoadLevelModelsLump(IVirtualStream* pFile);
	void				LoadLowDetailTableLump(IVirtualStream* pFile, int size);

	ModelRef_t*			GetModelByIndex(int nIndex) const;
	int					FindModelIndexByName(const char* name) const;
	const char*			GetModelNameByIndex(int nIndex) const;

	CarModelData_t*		GetCarModel(int index) const;
	
protected:
	void				OnModelLoaded(ModelRef_t* ref);
	void				OnModelFreed(ModelRef_t* ref);

	void				OnCarModelLoaded(CarModelData_t* data);
	void				OnCarModelFreed(CarModelData_t* data);
	
	ModelRef_t			m_levelModels[MAX_MODELS];

	CarModelData_t		m_carModels[MAX_CAR_MODELS];

	Array<String>		m_model_names;

	OnModelLoaded_t		m_onModelLoaded{ nullptr };
	OnModelFreed_t		m_onModelFreed{ nullptr };
	OnCarModelLoaded_t	m_onCarModelLoaded{ nullptr };
	OnCarModelFreed_t	m_onCarModelFreed{ nullptr };

	int					m_numModelsInPack{0};
};

//------------------------------------------------------------------------------------------------------------

void			PrintUnknownPolys();
int				decode_poly(const char* face, dpoly_t* out, int forceType = -1);

//-------------------------------------------------------------------------------

#endif // MODEL_H
