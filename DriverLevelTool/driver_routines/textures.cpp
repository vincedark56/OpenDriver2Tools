﻿
#include "driver_level.h"

#include "core/cmdlib.h"
#include "core/IVirtualStream.h"
#include "math/Vector.h"

//-------------------------------------------------------------------------------

// 16 bit color to BGRA
// originalTransparencyKey makes it pink
TVec4D<ubyte> rgb5a1_ToBGRA8(ushort color, bool originalTransparencyKey /*= true*/)
{
	ubyte r = (color & 0x1F) * 8;
	ubyte g = ((color >> 5) & 0x1F) * 8;
	ubyte b = ((color >> 10) & 0x1F) * 8;

	ubyte a = (color >> 15);

	// restore source transparency key
	if (originalTransparencyKey && color == 0)
	{
		return TVec4D<ubyte>(255, 0, 255, 0);
	}

	return TVec4D<ubyte>(b, g, r, a);
}

// 16 bit color to RGBA
// originalTransparencyKey makes it pink
TVec4D<ubyte> rgb5a1_ToRGBA8(ushort color, bool originalTransparencyKey /*= true*/)
{
	ubyte r = (color & 0x1F) * 8;
	ubyte g = ((color >> 5) & 0x1F) * 8;
	ubyte b = ((color >> 10) & 0x1F) * 8;

	ubyte a = (color >> 15);

	// restore source transparency key
	if (originalTransparencyKey && color == 0)
	{
		return TVec4D<ubyte>(255, 0, 255, 0);
	}

	return TVec4D<ubyte>(r, g, b, a);
}

//-------------------------------------------------------------------------------

// unpacks texture, returns new source pointer
// there is something like RLE used
char* unpackTexture(char* src, char* dest)
{
	// start from the end
	char* ptr = dest + TEXPAGE_4BIT_SIZE - 1;

	do {
		char pix = *src++;

		if ((pix & 0x80) != 0)
		{
			char p = *src++;

			do (*ptr-- = p);
			while (pix++ <= 0);
		}
		else
		{
			do (*ptr-- = *src++);
			while (pix-- != 0);
		}
	} while (ptr >= dest);

	return src;
}

//-------------------------------------------------------------------------------

#define MAX_PAGE_CLUTS 63

struct SpooledTextureData_t
{
	int			numPalettes;
	TEXCLUT		palettes[63];
	ubyte		compTable[28];		// padding for 512-B alignment if uncompressed
	ubyte		texels[128 * 256];	// 256×256 four-bit indices
};

CTexturePage::CTexturePage()
{
}

CTexturePage::~CTexturePage()
{
	delete[] m_bitmap.data;
	delete[] m_bitmap.clut;
	delete[] m_details;
}

//-------------------------------------------------------------
// Conversion of indexed palettized texture to 32bit RGBA
//-------------------------------------------------------------
void CTexturePage::ConvertIndexedTextureToRGBA(uint* dest_color_data, int detail, TEXCLUT* clut, bool outputBGR, bool originalTransparencyKey)
{
	TexBitmap_t& bitmap = m_bitmap;

	if (!(detail < m_numDetails))
	{
		MsgError("Cannot apply palette to non-existent detail! Programmer error?\n");
		return;
	}

	if (clut == nullptr)
		clut = &bitmap.clut[detail];

	TEXINF& texInfo = m_details[detail].info;

	int ox = texInfo.x;
	int oy = texInfo.y;
	int w = texInfo.width;
	int h = texInfo.height;

	if (w == 0)
		w = 256;

	if (h == 0)
		h = 256;

	//char* textureName = g_textureNamesData + m_details[detail].nameoffset;
	//MsgWarning("Applying detail %d '%s' (xywh: %d %d %d %d)\n", detail, textureName, ox, oy, w, h);

	int tp_wx = ox + w;
	int tp_hy = oy + h;

	for (int y = oy; y < tp_hy; y++)
	{
		for (int x = ox; x < tp_wx; x++)
		{
			ubyte clindex = bitmap.data[y * 128 + x / 2];

			if (0 != (x & 1))
				clindex >>= 4;

			clindex &= 0xF;

			// flip texture by Y
			int ypos = (TEXPAGE_SIZE_Y - y - 1) * TEXPAGE_SIZE_Y;

			if(outputBGR)
			{
				TVec4D<ubyte> color = rgb5a1_ToBGRA8(clut->colors[clindex], originalTransparencyKey);
				dest_color_data[ypos + x] = *(uint*)(&color);
			}
			else
			{
				TVec4D<ubyte> color = rgb5a1_ToRGBA8(clut->colors[clindex], originalTransparencyKey);
				dest_color_data[ypos + x] = *(uint*)(&color);
			}

		}
	}
}

void CTexturePage::InitFromFile(int id, TEXPAGE_POS& tp, IVirtualStream* pFile)
{
	m_id = id;
	m_tp = tp;

	pFile->Read(&m_numDetails, 1, sizeof(int));

	// don't load empty tpage details
	if (m_numDetails)
	{
		// read texture detail info
		m_details = new TexDetailInfo_t[m_numDetails];

		for(int i = 0; i < m_numDetails; i++)
		{
			m_details[i].numExtraCLUTs = 0;
			memset(m_details[i].extraCLUTs, 0, sizeof(m_details[i].extraCLUTs));
			
			pFile->Read(&m_details[i].info, 1, sizeof(TEXINF));
		}
	}
	else
		m_details = nullptr;
}


void CTexturePage::LoadCompressedTexture(IVirtualStream* pFile)
{
	pFile->Read( &m_bitmap.numPalettes, 1, sizeof(int) );

	// allocate palettes
	m_bitmap.clut = new TEXCLUT[m_bitmap.numPalettes];

	for(int i = 0; i < m_bitmap.numPalettes; i++)
	{
		// read 16 palettes
		pFile->Read(&m_bitmap.clut[i].colors, 16, sizeof(ushort));
	}

	int imageStart = pFile->Tell();

	// read compression data
	ubyte* compressedData = new ubyte[TEXPAGE_4BIT_SIZE];
	pFile->Read(compressedData, 1, TEXPAGE_4BIT_SIZE);

	char* unpackEnd = unpackTexture((char*)compressedData, (char*)m_bitmap.data);
	
	// unpack
	m_bitmap.rsize = (char*)unpackEnd - (char*)compressedData;

	// seek to the right position
	// this is necessary because it's aligned to CD block size
	pFile->Seek(imageStart + m_bitmap.rsize, VS_SEEK_SET);
}

//-------------------------------------------------------------------------------
// Loads Texture page itself with it's color lookup tables
//-------------------------------------------------------------------------------
bool CTexturePage::LoadTPageAndCluts(IVirtualStream* pFile, bool isSpooled)
{
	int rStart = pFile->Tell();

	if(m_bitmap.data)
	{
		// skip already loaded data
		pFile->Seek(m_bitmap.rsize, VS_SEEK_CUR);
		return true;
	}

	m_bitmap.data = new ubyte[TEXPAGE_4BIT_SIZE];

	if( isSpooled )
	{
		// non-compressed textures loads different way, with a fixed size
		SpooledTextureData_t* texData = new SpooledTextureData_t;
		pFile->Read( texData, 1, sizeof(SpooledTextureData_t) );

		// palettes are after them
		m_bitmap.numPalettes = texData->numPalettes;

		m_bitmap.clut = new TEXCLUT[m_bitmap.numPalettes];
		memcpy(m_bitmap.clut, texData->palettes, sizeof(TEXCLUT)* m_bitmap.numPalettes);

		memcpy(m_bitmap.data, texData->texels, TEXPAGE_4BIT_SIZE);

		// not need anymore
		delete texData;
	}
	else
	{
		// non-spooled are compressed
		LoadCompressedTexture(pFile);
	}

	m_bitmap.rsize = pFile->Tell() - rStart;
	Msg("PAGE %d (%s) datasize=%d\n", m_id, isSpooled ? "compr" : "spooled", m_bitmap.rsize);

	return true;
}

//-------------------------------------------------------------------------------
// searches for detail in this TPAGE
//-------------------------------------------------------------------------------
TexDetailInfo_t* CTexturePage::FindTextureDetail(const char* name) const
{
	for (int i = 0; i < m_numDetails; i++)
	{
		const char* pTexName = m_owner->GetTextureDetailName(&m_details[i].info);

		if (!strcmp(pTexName, name)) // FIXME: hashing and case insensitive?
			return &m_details[i];
	}

	return nullptr;
}

TexDetailInfo_t* CTexturePage::GetTextureDetail(int num) const
{
	return &m_details[num];
}

int CTexturePage::GetDetailCount() const
{
	return m_numDetails;
}

const TexBitmap_t& CTexturePage::GetBitmap() const
{
	return m_bitmap;
}

int CTexturePage::GetId() const
{
	return m_id;
}

int CTexturePage::GetFlags() const
{
	return m_tp.flags;
}

//-------------------------------------------------------------------------------

CDriverLevelTextures::CDriverLevelTextures()
{
}

CDriverLevelTextures::~CDriverLevelTextures()
{
}

//
// loads global textures (pre-loading stage)
//
void CDriverLevelTextures::LoadPermanentTPages(IVirtualStream* pFile)
{
	pFile->Seek( g_levInfo.texdata_offset, VS_SEEK_SET );
	
	//-----------------------------------

	Msg("Loading permanent texture pages (%d)\n", m_numPermanentPages);

	// simulate sectors
	// convert current file offset to sectors
	long sector = pFile->Tell() / 2048; 
	int nsectors = 0;

	for (int i = 0; i < m_numPermanentPages; i++)
		nsectors += (m_permsList[i].y + 2047) / 2048;
	
	// load permanent pages
	for(int i = 0; i < m_numPermanentPages; i++)
	{
		long curOfs = pFile->Tell(); 
		int tpage = m_permsList[i].x;

		// permanents are also compressed
		m_texPages[tpage].LoadTPageAndCluts(pFile, false);

		pFile->Seek(curOfs + ((m_permsList[i].y + 2047) & -2048), VS_SEEK_SET);
	}

	// simulate sectors
	sector += nsectors;
	pFile->Seek(sector * 2048, VS_SEEK_SET);

	// Driver 2 - special cars only
	// Driver 1 - only player cars
	Msg("Loading special/car texture pages (%d)\n", m_numSpecPages);

	// load compressed spec pages
	// those are non-spooled ones
	for (int i = 0; i < m_numSpecPages; i++)
	{
		TexBitmap_t* newTexData = new TexBitmap_t;

		long curOfs = pFile->Tell();
		int tpage = m_specList[i].x;

		// permanents are compressed
		m_texPages[tpage].LoadTPageAndCluts(pFile, false);

		pFile->Seek(curOfs + ((m_specList[i].y + 2047) & -2048), VS_SEEK_SET);
	}
}

//-------------------------------------------------------------
// parses texture info lumps. Quite simple
//-------------------------------------------------------------
void CDriverLevelTextures::LoadTextureInfoLump(IVirtualStream* pFile)
{
	int l_ofs = pFile->Tell();

	int numPages;
	pFile->Read(&numPages, 1, sizeof(int));

	int numTextures;
	pFile->Read(&numTextures, 1, sizeof(int));

	Msg("TPage count: %d\n", numPages);
	Msg("Texture amount: %d\n", numTextures);

	// read array of texutre page info
	TEXPAGE_POS* tpage_position = new TEXPAGE_POS[numPages + 1];
	pFile->Read(tpage_position, numPages+1, sizeof(TEXPAGE_POS));

	// read page details
	m_numTexPages = numPages;
	m_texPages = new CTexturePage[numPages];

	for(int i = 0; i < numPages; i++) 
	{
		CTexturePage& tp = m_texPages[i];
		tp.m_owner = this;

		tp.InitFromFile(i, tpage_position[i], pFile);
	}

	pFile->Read(&m_numPermanentPages, 1, sizeof(int));
	Msg("Permanent TPages = %d\n", m_numPermanentPages);
	pFile->Read(m_permsList, 16, sizeof(XYPAIR));

	// Driver 2 - special cars only
	// Driver 1 - only player cars
	pFile->Read(&m_numSpecPages, 1, sizeof(int));
	pFile->Read(m_specList, 16, sizeof(XYPAIR));

	Msg("Special/Car TPages = %d\n", m_numSpecPages);

	pFile->Seek(l_ofs, VS_SEEK_SET);

	// not needed
	delete tpage_position;
}

//-------------------------------------------------------------
// load texture names, same as model names
//-------------------------------------------------------------
void CDriverLevelTextures::LoadTextureNamesLump(IVirtualStream* pFile, int size)
{
	int l_ofs = pFile->Tell();

	m_textureNamesData = new char[size];

	pFile->Read(m_textureNamesData, size, 1);

	int len = strlen(m_textureNamesData);
	int sz = 0;

	do
	{
		char* str = m_textureNamesData + sz;

		len = strlen(str);

		sz += len + 1;
	} while (sz < size);

	pFile->Seek(l_ofs, VS_SEEK_SET);
}

//-------------------------------------------------------------
// Loads car and pedestrians palletes
//-------------------------------------------------------------
void CDriverLevelTextures::ProcessPalletLump(IVirtualStream* pFile)
{
	ushort* clutTablePtr;
	int total_cluts;
	int l_ofs = pFile->Tell();

	pFile->Read(&total_cluts, 1, sizeof(int));

	if (total_cluts == 0)
		return;

	m_extraPalettes = new ExtClutData_t[total_cluts + 1];
	memset(m_extraPalettes, 0, sizeof(ExtClutData_t) * total_cluts);

	Msg("total_cluts: %d\n", total_cluts);

	int added_cluts = 0;
	while (true)
	{
		PALLET_INFO info;

		if (g_format == LEV_FORMAT_DRIVER1)
		{
			PALLET_INFO_D1 infod1;
			pFile->Read(&infod1, 1, sizeof(info) - sizeof(int));
			
			info.clut_number = -1; // D1 doesn't have that
			info.tpage = infod1.tpage;
			info.texnum = infod1.texnum;
			info.palette = infod1.palette;
		}
		else
		{
			pFile->Read(&info, 1, sizeof(info));
		}

		if (info.palette == -1)
			break;

		if (info.clut_number == -1)
		{
			ExtClutData_t& data = m_extraPalettes[added_cluts];
			data.texnum[data.texcnt++] = info.texnum;
			data.tpage = info.tpage;
			data.palette = info.palette;

			clutTablePtr = data.clut.colors;

			pFile->Read(clutTablePtr, 16, sizeof(ushort));

			// reference
			TexDetailInfo_t& detail = m_texPages[data.tpage].m_details[info.texnum];
			detail.extraCLUTs[data.palette] = &data.clut;
			
			added_cluts++;

			// only in D1 we need to check count
			if (g_format == LEV_FORMAT_DRIVER1)
			{
				if (added_cluts >= total_cluts)
					break;
			}
		}
		else
		{
			ExtClutData_t& data = m_extraPalettes[info.clut_number];

			// add texture number to existing clut
			data.texnum[data.texcnt++] = info.texnum;

			// reference
			TexDetailInfo_t& detail = m_texPages[data.tpage].m_details[info.texnum];
			detail.extraCLUTs[data.palette] = &data.clut;
		}
	}

	Msg("    added: %d\n", added_cluts);
	m_numExtraPalettes = added_cluts;

	pFile->Seek(l_ofs, VS_SEEK_SET);
}

//----------------------------------------------------------------------------------------------------

TexDetailInfo_t* CDriverLevelTextures::FindTextureDetail(const char* name) const
{
	for(int i = 0; i < m_numTexPages; i++)
	{
		TexDetailInfo_t* found = m_texPages[i].FindTextureDetail(name);

		if (found)
			return found;
	}

	return nullptr;
}

// returns texture name
const char* CDriverLevelTextures::GetTextureDetailName(TEXINF* info) const
{
	return m_textureNamesData + info->nameoffset;
}

// release all data
void CDriverLevelTextures::FreeAll()
{
	delete[] m_textureNamesData;
	delete[] m_texPages;
	delete[] m_extraPalettes;

	m_textureNamesData = nullptr;
	m_texPages = nullptr;
	m_extraPalettes = nullptr;

	m_numTexPages = 0;
	m_numPermanentPages = 0;
	m_numSpecPages = 0;
	m_numExtraPalettes = 0;
}

// getters
CTexturePage* CDriverLevelTextures::GetTPage(int page) const
{
	return &m_texPages[page];
}

int CDriverLevelTextures::GetTPageCount() const
{
	return m_numTexPages;
}