if not houseAutowrapItems then
	local dynamicDecorationKitId = ITEM_DECORATION_KIT or 23398
	houseAutowrapItems = {}
	for i = 1, 65001 do
		local it = ItemType(i)
		if it and it:getId() ~= 0 then
			local w = it:getWrapableTo()
			if w and w ~= 0 and w ~= dynamicDecorationKitId then
				houseAutowrapItems[w] = i
			end
		end
	end
end
