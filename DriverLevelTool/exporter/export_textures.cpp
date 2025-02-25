#include "driver_level.h"
#include "core/cmdlib.h"
#include "core/VirtualStream.h"

#include "util/image.h"
#include "util/rnc2.h"

#include <stdio.h>
#include <string.h>

#include <nstd/File.hpp>
#include <nstd/Directory.hpp>
#include <nstd/Array.hpp>

extern String	g_levname_moddir;
extern String	g_levname_texdir;
extern String	g_levname;

extern bool g_export_textures;
extern bool g_export_overmap;
extern int g_overlaymap_width;
extern bool g_explode_tpages;
extern bool g_export_world;
extern bool	g_export_worldUnityScript;

void GetTPageDetailPalettes(Array<TEXCLUT*>& out, CTexturePage* tpage, TexDetailInfo_t* detail)
{
	const TexBitmap_t& bitmap = tpage->GetBitmap();

	// ofc, add default
	out.append(&bitmap.clut[detail->detailNum]);

	for (int i = 0; i < detail->numExtraCLUTs; i++)
		out.append(detail->extraCLUTs[i]);
}

//-------------------------------------------------------------
// writes 4-bit TIM image file from TPAGE
//-------------------------------------------------------------
void ExportTIM(CTexturePage* tpage, int detail)
{
	if (detail < 0 || detail >= tpage->GetDetailCount())
	{
		MsgError("Cannot apply palette to non-existent detail! Programmer error?\n");
		return;
	}

	const TexBitmap_t& bitmap = tpage->GetBitmap();
	TexDetailInfo_t* texdetail = tpage->GetTextureDetail(detail);
	
	int ox = texdetail->info.x;
	int oy = texdetail->info.y;
	int w = texdetail->info.width;
	int h = texdetail->info.height;

	if (w == 0)
		w = 256;

	if (h == 0)
		h = 256;

	Array<TEXCLUT*> palettes;
	GetTPageDetailPalettes(palettes, tpage, texdetail);

	const char* textureName = g_levTextures.GetTextureDetailName(&texdetail->info);

	Msg("Saving %s' %d (xywh: %d %d %d %d)\n", textureName, detail, ox, oy, w, h);

	int tp_wx = ox + w;
	int tp_hy = oy + h;

	int half_w = (w >> 1);
	int img_size = (half_w + ((half_w & 1) ? 0 : 1)) * h;

	half_w -= half_w & 1;

	// copy image
	ubyte* image_data = new ubyte[img_size];
	TEXCLUT* clut_data = new TEXCLUT[palettes.size()];

	for (int y = oy; y < tp_hy; y++)
	{
		for (int x = ox; x < tp_wx; x++)
		{
			int px, py;
			px = (x - ox);
			py = (y - oy);

			int half_x = (x >> 1);
			int half_px = (px >> 1);
			//half_px -= half_px & 1;

			if (py * half_w + half_px < img_size)
			{
				image_data[py * half_w + half_px] = bitmap.data[y * 128 + half_x];
			}
			else
			{
				if (py * half_w > h)
					MsgWarning("Y offset exceeds %d (%d)\n", py, h);

				if (half_px > half_w)
					MsgWarning("X offset exceeds %d (%d)\n", half_px, half_w);
			}
		}
	}

	// copy cluts
	for (usize i = 0; i < palettes.size(); i++)
	{
		if (!palettes[i])
		{
			clut_data[i] = *palettes[0];
			continue;
		}
		clut_data[i] = *palettes[i];
	}

	// compose TIMs
	SaveTIM_4bit(String::fromPrintf("%s/PAGE_%d/%s_%d.TIM", (char*)g_levname_texdir, tpage->GetId(), textureName, detail),
		image_data, img_size, ox, oy, w, h, 
		(ubyte*)clut_data, palettes.size() );

	delete[] image_data;
	delete[] clut_data;
}

//-------------------------------------------------------------
// Exports entire texture page
//-------------------------------------------------------------
void ExportTexturePage(CTexturePage* tpage)
{
	if (!tpage)
		return;

	//
	// This is where texture is converted from paletted BGR5A1 to BGRA8
	// Also saving to LEVEL_texture/PAGE_*
	//

	const TexBitmap_t& bitmap = tpage->GetBitmap();

	if (!bitmap.data)
		return;		// NO DATA

	int numDetails = tpage->GetDetailCount();

	// Write an INI file with texture page info
	{
		FILE* pIniFile = fopen(String::fromPrintf("%s/PAGE_%d.ini", (char*)g_levname_texdir, tpage->GetId()), "wb");

		if (pIniFile)
		{
			fprintf(pIniFile, "[tpage]\r\n");
			fprintf(pIniFile, "details=%d\r\n", numDetails);
			fprintf(pIniFile, "\r\n");

			for (int i = 0; i < numDetails; i++)
			{
				TexDetailInfo_t* detail = tpage->GetTextureDetail(i);
				
				int x = detail->info.x;
				int y = detail->info.y;
				int w = detail->info.width;
				int h = detail->info.height;

				if (w == 0)
					w = 256;

				if (h == 0)
					h = 256;

				fprintf(pIniFile, "[detail_%d]\r\n", i);
				fprintf(pIniFile, "id=%d\r\n", detail->info.id);
				fprintf(pIniFile, "name=%s\r\n", g_levTextures.GetTextureDetailName(&detail->info));
				fprintf(pIniFile, "xywh=%d,%d,%d,%d\r\n",x, y, w, h);
				fprintf(pIniFile, "\r\n");
			}

			fclose(pIniFile);
		}
	}

	if (g_explode_tpages)
	{
		MsgInfo("Exploding texture '%s/PAGE_%d'\n", (char*)g_levname_texdir, tpage->GetId());

		// make folder and place all tims in there
		Directory::create(String::fromPrintf("%s/PAGE_%d", (char*)g_levname_texdir, tpage->GetId()));

		for (int i = 0; i < numDetails; i++)
		{
			ExportTIM(tpage, i);
		}

		return;
	}

#define TEX_CHANNELS 4

	int imgSize = TEXPAGE_SIZE * TEX_CHANNELS;

	uint* color_data = (uint*)malloc(imgSize);

	memset(color_data, 0, imgSize);

	// Dump whole TPAGE indexes
	for (int y = 0; y < 256; y++)
	{
		for (int x = 0; x < 256; x++)
		{
			ubyte clindex = bitmap.data[y * 128 + (x >> 1)];

			if (0 != (x & 1))
				clindex >>= 4;

			clindex &= 0xF;

			int ypos = (TEXPAGE_SIZE_Y - y - 1) * TEXPAGE_SIZE_Y;

			color_data[ypos + x] = clindex * 32;
		}
	}

	for (int i = 0; i < numDetails; i++)
	{
		tpage->ConvertIndexedTextureToRGBA(color_data, i, nullptr, true, !g_export_worldUnityScript);
	}

	MsgInfo("Writing texture '%s/PAGE_%d.tga'\n", (char*)g_levname_texdir, tpage->GetId());
	SaveTGA(String::fromPrintf("%s/PAGE_%d.tga", (char*)g_levname_texdir, tpage->GetId()), (ubyte*)color_data, TEXPAGE_SIZE_Y, TEXPAGE_SIZE_Y, TEX_CHANNELS);

	int numPalettes = 0;
	for (int pal = 0; pal < 16; pal++)
	{
		bool anyMatched = false;

		for (int j = 0; j < numDetails; j++)
		{
			TexDetailInfo_t* detail = tpage->GetTextureDetail(j);
			
			if(detail->extraCLUTs[pal])
			{
				tpage->ConvertIndexedTextureToRGBA(color_data, j, detail->extraCLUTs[pal], true, !g_export_worldUnityScript);
				anyMatched = true;
			}
		}

		if (anyMatched)
		{
			MsgInfo("Writing texture %s/PAGE_%d_%d.tga\n", (char*)g_levname_texdir, tpage->GetId(), numPalettes);
			SaveTGA(String::fromPrintf("%s/PAGE_%d_%d.tga", (char*)g_levname_texdir, tpage->GetId(), numPalettes), (ubyte*)color_data, TEXPAGE_SIZE_Y, TEXPAGE_SIZE_Y, TEX_CHANNELS);
			numPalettes++;
		}
	}

	free(color_data);
}

//-------------------------------------------------------------
// Exports all texture pages
//-------------------------------------------------------------
void ExportAllTextures()
{
	// preload region data if needed
	if (!g_export_world)
	{
		MsgInfo("Preloading area TPages (%d)\n", g_levMap->GetAreaDataCount());

		// Open file stream
		FILE* fp = fopen(g_levname, "rb");
		if (fp)
		{
			CFileStream stream(fp);

			SPOOL_CONTEXT spoolContext;
			spoolContext.dataStream = &stream;
			spoolContext.lumpInfo = &g_levInfo;

			int numAreas = g_levMap->GetAreaDataCount();

			for (int i = 0; i < numAreas; i++)
			{
				g_levMap->LoadInAreaTPages(spoolContext, i);
			}

			fclose(fp);
		}
		else
			MsgError("Unable to preload spooled area TPages!\n");
	}

	MsgInfo("Exporting texture data\n");
	for (int i = 0; i < g_levTextures.GetTPageCount(); i++)
	{
		ExportTexturePage(g_levTextures.GetTPage(i));
	}
}

//-------------------------------------------------------------
// converts and writes TGA file of overlay map
//-------------------------------------------------------------
void ExportOverlayMap()
{
	int numValid = g_levTextures.GetOverlayMapSegmentCount();

	MsgWarning("overlay map segment count: %d\n", numValid);

	if (!numValid)
		return;

	int overmapWidth = g_overlaymap_width * 32;
	int overmapHeight = (numValid / g_overlaymap_width) * 32;

	TVec4D<ubyte> tempSegment[32 * 32];

	TVec4D<ubyte>* rgba = new TVec4D<ubyte>[overmapWidth * overmapHeight];

	int x, y, xx, yy;
	int wide, tall;

	wide = overmapWidth / 32;
	tall = overmapHeight / 32;

	int numTilesProcessed = 0;

	for (x = 0; x < wide; x++)
	{
		for (y = 0; y < tall; y++)
		{
			int idx = x + y * wide;

			g_levTextures.GetOverlayMapSegmentRGBA(tempSegment, x + y * wide);

			numTilesProcessed++;

			// place segment
			for (yy = 0; yy < 32; yy++)
			{
				for (xx = 0; xx < 32; xx++)
				{
					int px = x * 32 + xx;
					int py = (tall - 1 - y) * 32 + (31-yy);

					rgba[px + py * overmapWidth] = tempSegment[yy * 32 + xx];
				}
			}
		}
	}

	if(numTilesProcessed != numValid)
	{
		MsgWarning("Missed tiles: %d\n", numValid-numTilesProcessed);
	}

	SaveTGA(String::fromPrintf("%s/MAP.tga", (char*)g_levname_texdir), (ubyte*)rgba, overmapWidth, overmapHeight, TEX_CHANNELS);

	delete[] rgba;
}