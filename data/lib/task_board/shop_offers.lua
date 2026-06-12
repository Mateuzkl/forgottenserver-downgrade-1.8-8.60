-- Hunting Task Shop Offers Configuration
-- Types: 0=Item, 1=Mount, 2=Outfit, 3=ItemDouble, 4=BonusPromotion, 5=WeeklyExpansion
-- Customize these IDs for your server.

return {
	-- ===== Items =====
	{ id = 1,  name = "Bronze Hunter Trophy",   type = 0, itemId = 32754, count = 1,  price = 100 },
	{ id = 2,  name = "Silver Hunter Trophy",   type = 0, itemId = 32755, count = 1,  price = 250 },
	{ id = 3,  name = "Gold Hunter Trophy",     type = 0, itemId = 32756, count = 1,  price = 500 },
	{ id = 4,  name = "Expedition Backpack",    type = 0, itemId = 10324, count = 1,  price = 150 },
	{ id = 5,  name = "Fairy Wings",            type = 0, itemId = 25694, count = 1,  price = 300 },
	{ id = 6,  name = "Dwarven Armor",          type = 0, itemId = 3397,  count = 1,  price = 400 },

	-- ===== Mounts =====
	{ id = 7,  name = "Donkey Mount",           type = 1, mountId = 13, count = 1,  price = 1000 },
	{ id = 8,  name = "Black Sheep Mount",      type = 1, mountId = 4,  count = 1,  price = 1500 },
	{ id = 9,  name = "Dragonling Mount",       type = 1, mountId = 31, count = 1,  price = 3000 },
	{ id = 10, name = "Ursagrodon Mount",       type = 1, mountId = 38, count = 1,  price = 5000 },

	-- ===== Outfits =====
	{ id = 11, name = "Hunter Outfit",           type = 2, outfitId = 129, addons = 0, count = 1, price = 2000 },
	{ id = 12, name = "Hunter Outfit Addon 1",   type = 2, outfitId = 129, addons = 1, count = 1, price = 1500 },
	{ id = 13, name = "Hunter Outfit Addon 2",   type = 2, outfitId = 129, addons = 2, count = 1, price = 1500 },
	{ id = 14, name = "Ranger Outfit",           type = 2, outfitId = 684, addons = 0, count = 1, price = 3000 },

	-- ===== Item Double (double the count!) =====
	{ id = 15, name = "Double Hunter Trophy",    type = 3, itemId = 32756, count = 1,  price = 800 },

	-- ===== Weekly Expansion (more tasks) =====
	{ id = 16, name = "Weekly Task Expansion",   type = 5, count = 1,  price = 2500 },
}
