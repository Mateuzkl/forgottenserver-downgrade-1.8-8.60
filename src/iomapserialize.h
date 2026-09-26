// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_IOMAPSERIALIZE_H
#define FS_IOMAPSERIALIZE_H

#include "database.h"
#include "house.h"
#include "map.h"

class IOMapSerialize
{
public:
	static void loadHouseItems(Map* map);
	static bool saveHouseItems();
	static bool loadHouseInfo();
	static bool saveHouseInfo();

	static bool saveHouse(const House* house);

	// Exact IDs always match. Different IDs only match when item metadata
	// proves that they are transform states of the same static fixture.
	static bool isSamePersistentFixtureFamily(const ItemType& mapType, const ItemType& persistedType);

private:
	static void saveItem(PropWriteStream& stream, const Item* item);
	static void saveTile(PropWriteStream& stream, const Tile* tile);

	static bool loadContainer(PropStream& propStream, Container* container);
	static bool loadItem(PropStream& propStream, Cylinder* parent);
};

#endif
