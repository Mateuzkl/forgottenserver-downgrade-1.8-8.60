GcDebug = GcDebug or {}

local snapshots = {}

function GcDebug.memory()
	local kb = Game.getLuaMemoryUsage()
	if kb >= 1048576 then
		return string.format("%.2f GB", kb / 1048576)
	elseif kb >= 1024 then
		return string.format("%.2f MB", kb / 1024)
	end
	return string.format("%d KB", kb)
end

function GcDebug.collect(reason)
	local before = Game.getLuaMemoryUsage()
	Game.collectLuaGarbage()
	local after = Game.getLuaMemoryUsage()
	local freed = before - after
	logger.info("[GcDebug] GC collect ({}) {} -> {} KB (freed {})", reason or "manual", before, after, freed > 0 and freed or 0)
end

function GcDebug.step(size)
	Game.stepLuaGarbage(size or 200)
end

function GcDebug.snapshot(label)
	label = label or "default"
	snapshots[label] = {
		memory = Game.getLuaMemoryUsage(),
		time = os.time()
	}
end

function GcDebug.diff(labelA, labelB)
	local snapA = snapshots[labelA or "default"]
	local snapB = snapshots[labelB or "default"]
	if not snapA or not snapB then
		return nil, "One or both snapshots not found"
	end
	local diff = snapB.memory - snapA.memory
	if diff >= 0 then
		return string.format("+%d KB", diff)
	end
	return string.format("%d KB", diff)
end
