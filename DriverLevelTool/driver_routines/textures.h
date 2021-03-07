
// Some containers

#ifndef TEXTURES_H
#define TEXTURES_H

#include "level.h"
#include "math/Vector.h"
#include "math/psx_math_types.h"

// forward
class IVirtualStream;
class CDriverLevelTextures;

//---------------------------------------------------------------------------------------------------------------------------------

struct TexBitmap_t
{
	ubyte*			data { nullptr };	// 4 bit texture data
	int				rsize{ 0 };

	TEXCLUT*		clut {nullptr};
	int				numPalettes{ 0 };
};

struct ExtClutData_t
{
	TEXCLUT clut;
	int texnum[32];
	int texcnt;
	int palette;
	int tpage;
};

struct TexDetailInfo_t
{
	TEXINF			info;
	TEXCLUT*		extraCLUTs[32];
	int				numExtraCLUTs;
};

//---------------------------------------------------------------------------------------------------------------------------------
class CTexturePage
{
	friend class CDriverLevelTextures;
public:
	CTexturePage();
	virtual ~CTexturePage();

	// loading texture page properties from file
	void					InitFromFile(int id, TEXPAGE_POS& tp, IVirtualStream* pFile);
	
	// loading texture page from lump
	bool					LoadTPageAndCluts(IVirtualStream* pFile, bool isSpooled);

	// converting 4bit texture page to 32 bit full color RGBA/BGRA
	void					ConvertIndexedTextureToRGBA(uint* dest_color_data, 
												int detail, TEXCLUT* clut = nullptr,
												bool outputBGR = false, bool originalTransparencyKey = true);

	// searches for detail in this TPAGE
	TexDetailInfo_t*		FindTextureDetail(const char* name) const;
	TexDetailInfo_t*		GetTextureDetail(int num) const;
	int						GetDetailCount() const;

	// returns the 4bit map data
	const TexBitmap_t&		GetBitmap() const;

	int						GetId() const;
	int						GetFlags() const;
protected:

	void					LoadCompressedTexture(IVirtualStream* pFile);

	TexBitmap_t				m_bitmap;
	TEXPAGE_POS				m_tp;

	TexDetailInfo_t*		m_details{ nullptr };
	
	CDriverLevelTextures*	m_owner;
	
	int						m_id{ -1 };
	int						m_numDetails{ 0 };
};

//---------------------------------------------------------------------------------------------------------------------------------

class CDriverLevelTextures
{
	friend class CTexturePage;
public:
	CDriverLevelTextures();
	virtual ~CDriverLevelTextures();

	// loaders
	void				LoadTextureInfoLump(IVirtualStream* pFile);
	void				LoadPermanentTPages(IVirtualStream* pFile);
	void				LoadTextureNamesLump(IVirtualStream* pFile, int size);
	void				ProcessPalletLump(IVirtualStream* pFile);

	// release all data
	void				FreeAll();

	// getters
	CTexturePage*		GetTPage(int page) const;
	int					GetTPageCount() const;

	TexDetailInfo_t*	FindTextureDetail(const char* name) const;
	const char*			GetTextureDetailName(TEXINF* info) const;

protected:
	char*				m_textureNamesData{ nullptr };

	CTexturePage*		m_texPages{ nullptr };
	int					m_numTexPages{ 0 };

	XYPAIR				m_permsList[16];
	int					m_numPermanentPages{ 0 };

	int					m_numSpecPages{ 0 };
	XYPAIR				m_specList[16];

	ExtClutData_t*		m_extraPalettes{ nullptr };
	int					m_numExtraPalettes{ 0 };
};

//---------------------------------------------------------------------------------------------------------------------------------

TVec4D<ubyte> rgb5a1_ToBGRA8(ushort color, bool originalTransparencyKey = true);
TVec4D<ubyte> rgb5a1_ToRGBA8(ushort color, bool originalTransparencyKey = true);

#endif // TEXTURES_H
