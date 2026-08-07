// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_DATITEMLOADER_H
#define FS_DATITEMLOADER_H

#include <cstdint>

// Thing flags used by the Tibia 8.60 DAT item section.
enum class ItemDatFlag : uint8_t
{
	Ground = 0,
	GroundBorder = 1,
	OnBottom = 2,
	OnTop = 3,
	Container = 4,
	Stackable = 5,
	ForceUse = 6,
	MultiUse = 7,
	Writable = 8,
	WritableOnce = 9,
	FluidContainer = 10,
	Fluid = 11,
	IsUnpassable = 12,
	IsUnmoveable = 13,
	BlockMissiles = 14,
	BlockPathfinder = 15,
	Pickupable = 16,
	Hangable = 17,
	IsHorizontal = 18,
	IsVertical = 19,
	Rotatable = 20,
	HasLight = 21,
	DontHide = 22,
	Translucent = 23,
	HasOffset = 24,
	HasElevation = 25,
	Lying = 26,
	AnimateAlways = 27,
	Minimap = 28,
	LensHelp = 29,
	FullGround = 30,
	IgnoreLook = 31,
	Cloth = 32,
	MarketItem = 33,
	LastFlag = 255,
};

#endif
