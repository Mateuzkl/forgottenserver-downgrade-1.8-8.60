if not houseAutowrapItems then
	houseAutowrapItems = {}
	for i = 1, 65001 do
		local it = ItemType(i)
		if it and it:getId() ~= 0 then
			local w = it:getWrapableTo()
			if w and w ~= 0 then
				houseAutowrapItems[w] = i
			end
		end
	end
end
