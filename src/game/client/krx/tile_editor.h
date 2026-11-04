#ifndef GAME_CLIENT_KRX_TILE_EDITOR_H
#define GAME_CLIENT_KRX_TILE_EDITOR_H

#include <base/vmath.h>

#include <algorithm>
#include <vector>

namespace OpenKrx::TileEditor
{
	enum : unsigned char { TUNNEL = 1, FINISH = 2 };
	// 344230: painting one kind removes the other kind at the same tile.
	inline bool Paint(std::vector<unsigned char> &vTiles, int Width, int Height, int X, int Y, unsigned char Kind)
	{
		if(X < 0 || Y < 0 || X >= Width || Y >= Height || vTiles.size() != (size_t)Width * Height)
			return false;
		auto &Tile = vTiles[(size_t)Y * Width + X];
		const unsigned char Before = Tile;
		Tile = Kind;
		return Tile != Before;
	}
	inline std::vector<vec2> Goals(const std::vector<unsigned char> &vTiles, int Width, int Height)
	{
		std::vector<vec2> vGoals;
		if(Width <= 0 || Height <= 0 || vTiles.size() != (size_t)Width * Height)
			return vGoals;
		// Original std::map<pair<int,int>> traversal sorts X first, then Y.
		for(int X = 0; X < Width; ++X)
			for(int Y = 0; Y < Height; ++Y)
				if(vTiles[(size_t)Y * Width + X] & FINISH)
					vGoals.emplace_back(X * 32.0f, Y * 32.0f);
		return vGoals;
	}
	inline void ClearKind(std::vector<unsigned char> &vTiles, unsigned char Kind)
	{
		for(auto &Tile : vTiles)
			Tile &= ~Kind;
	}
}
#endif
