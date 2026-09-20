#include "../otpch.h"

#include "../chat.h"
#include "../condition.h"
#include "../configmanager.h"
#include "../container.h"
#include "../creature.h"
#include "../events.h"
#include "../game.h"
#include "../globalevent.h"
#include "../groups.h"
#include "../item.h"
#include "../movement.h"
#include "../player.h"
#include "../performance_metrics.h"
#include "../reactor.h"
#include "../scheduler.h"
#include "../scriptmanager.h"
#include "../tasks.h"
#include "../tile.h"

#include "test_support.h"

#include <filesystem>

extern LuaEnvironment g_luaEnvironment;

struct EventsTestAccess
{
	static bool installMovementRecorder(Events& events)
	{
		if (!events.scriptInterface.initState()) {
			return false;
		}

		lua_State* L = events.scriptInterface.getLuaState();
		constexpr const char* script = R"lua(
			movementHookCalls = 0
			movementHookFromX = 0
			movementHookFromY = 0
			movementHookFromZ = 0
			movementHookToX = 0
			movementHookToY = 0
			movementHookToZ = 0
			movementHookFlags = 0
			function Player:onStepTile(fromPosition, toPosition, movementSessionFlags)
				movementHookCalls = movementHookCalls + 1
				movementHookFromX = fromPosition.x
				movementHookFromY = fromPosition.y
				movementHookFromZ = fromPosition.z
				movementHookToX = toPosition.x
				movementHookToY = toPosition.y
				movementHookToZ = toPosition.z
				movementHookFlags = movementSessionFlags
			end
		)lua";
		if (luaL_dostring(L, script) != LUA_OK) {
			lua_pop(L, 1);
			return false;
		}

		events.info.playerOnStepTile = events.scriptInterface.getMetaEvent("Player", "onStepTile");
		return events.info.playerOnStepTile != -1;
	}

	static int64_t globalInteger(const Events& events, const char* name)
	{
		lua_State* L = events.scriptInterface.getLuaState();
		lua_getglobal(L, name);
		const int64_t value = lua_tointeger(L, -1);
		lua_pop(L, 1);
		return value;
	}
};

struct TaskReactorTestAccess
{
	static void makeScheduledTasksReady(TaskReactor& reactor)
	{
		std::scoped_lock lock(reactor.mutex);
		const auto now = std::chrono::steady_clock::now();
		for (auto& task : reactor.scheduleInbox) {
			task.fireAt = now;
		}
		for (auto& task : reactor.sendInbox) {
			if (task.identifier != 0) {
				task.fireAt = now;
			}
		}
		for (auto& task : reactor.taskHeap) {
			task.fireAt = now;
		}
		std::make_heap(reactor.taskHeap.begin(), reactor.taskHeap.end(), TaskReactor::taskComesAfter);
	}

	static std::optional<std::chrono::steady_clock::time_point> scheduledFireAt(TaskReactor& reactor,
	                                                                          uint32_t identifier)
	{
		std::scoped_lock lock(reactor.mutex);
		auto findTask = [identifier](const auto& tasks) {
			return std::find_if(tasks.begin(), tasks.end(), [identifier](const auto& task) {
				return task.identifier == identifier;
			});
		};

		if (auto it = findTask(reactor.scheduleInbox); it != reactor.scheduleInbox.end()) {
			return it->fireAt;
		}
		if (auto it = findTask(reactor.sendInbox); it != reactor.sendInbox.end()) {
			return it->fireAt;
		}
		if (auto it = findTask(reactor.taskHeap); it != reactor.taskHeap.end()) {
			return it->fireAt;
		}
		return std::nullopt;
	}
};

struct CreatureWalkTestAccess
{
	static uint32_t eventId(const Creature& creature) { return creature.eventWalk; }
	static size_t pendingDirections(const Creature& creature) { return creature.listWalkDir.size(); }

	static void finishCooldown(Creature& creature)
	{
		const int64_t duration = std::max<int64_t>(1, creature.getStepDuration());
		creature.lastStep = OTSYS_TIME() - static_cast<uint64_t>(duration + 1);
	}

	static void setWalkAction(Player& player, std::unique_ptr<SchedulerTask> task)
	{
		player.setNextWalkActionTask(std::move(task));
	}

	static void clearPlayerTasks(Player& player)
	{
		player.setNextWalkActionTask(nullptr);
		player.setNextWalkTask(nullptr);
	}

	static void setPzLocked(Player& player, bool value) { player.pzLocked = value; }
};

namespace {

class WalkCreature final : public Creature
{
public:
	const std::string& getName() const override { return name; }
	const std::string& getNameDescription() const override { return name; }
	std::string getDescription(int32_t) const override { return name; }
	CreatureType_t getType() const override { return CREATURETYPE_MONSTER; }
	void setID() override
	{
		if (id == 0) {
			id = nextId++;
		}
	}
	void addList() override {}
	void removeList() override {}

	uint32_t generation() const { return walkGeneration; }
	uint32_t eventId() const { return eventWalk; }

	void onWalk() override
	{
		++walkCalls;
		Creature::onWalk();
	}

	bool getNextStep(Direction&, uint32_t&) override
	{
		if (clearEventBeforeStop) {
			// Match Monster::getNextStep() when movement is blocked or idle.
			eventWalk = 0;
		}
		return false; // exhaust the current path without needing map movement
	}

	void onWalkComplete() override
	{
		++completionCalls;
		if (rearmOnComplete) {
			// Match Monster::onWalkComplete -> walkToSpawn -> startAutoWalk.
			startAutoWalk();
			replacementId = eventWalk;
		}
	}

	bool rearmOnComplete = false;
	bool clearEventBeforeStop = false;
	int walkCalls = 0;
	int completionCalls = 0;
	uint32_t replacementId = 0;

private:
	inline static uint32_t nextId = 0x51000000;
	std::string name = "walk scheduler test creature";
};

class WalkFixture
{
public:
	WalkFixture() : creature(std::make_shared<WalkCreature>()), oldEvents(g_events)
	{
		g_events = &events;
		g_dispatcher.start();
		g_scheduler.start();
		const Position position{950, 950, 7};
		if (!g_game.map.getTile(position)) {
			g_game.map.setTile(position.x, position.y, position.z,
			                   std::make_unique<StaticTile>(position.x, position.y, position.z));
		}
		CHECK(g_game.internalPlaceCreature(creature.get(), position, false, true));
	}

	~WalkFixture()
	{
		creature->rearmOnComplete = false;
		creature->stopEventWalk();
		if (!creature->isRemoved()) {
			g_game.removeCreature(creature.get(), false);
		}
		// Drain cancelled scheduler callbacks while their referenced globals live.
		g_reactor.drain();
		g_scheduler.shutdown();
		g_dispatcher.shutdown();
		g_events = oldEvents;
	}

	std::shared_ptr<WalkCreature> creature;

private:
	Events* oldEvents;
	Events events;
};

void ensureItemTypes()
{
	static const bool loaded = [] {
		if (Item::items.size() != 0) {
			return true;
		}
		const auto itemFile = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
		                      "data/items/items.otb";
		return Item::items.loadFromOtb(itemFile.string());
	}();
	CHECK(loaded);
}

void ensureWalkTile(const Position& position)
{
	if (!g_game.map.getTile(position)) {
		g_game.map.setTile(position.x, position.y, position.z,
		                   std::make_unique<StaticTile>(position.x, position.y, position.z));
	}
	Tile* tile = g_game.map.getTile(position);
	if (!tile->getGround()) {
		tile->setGround(std::make_shared<Item>(0));
	}
}

class PlayerWalkFixture
{
public:
	PlayerWalkFixture()
	    : oldChat(g_chat), oldEvents(g_events), oldGlobalEvents(g_globalEvents),
	      oldMinSpeed(ConfigManager::getInteger(ConfigManager::PLAYER_MIN_SPEED)),
	      oldMaxSpeed(ConfigManager::getInteger(ConfigManager::PLAYER_MAX_SPEED)),
	      oldPostMovementSessionCleanup(
	          ConfigManager::getBoolean(ConfigManager::POST_MOVEMENT_SESSION_CLEANUP_ENABLED)),
	      previousMoveEvents(std::make_unique<MoveEvents>())
	{
		ensureItemTypes();
		g_chat = &chat;
		g_events = &events;
		g_globalEvents = &globalEvents;
		g_moveEvents.swap(previousMoveEvents);
		ConfigManager::setInteger(ConfigManager::PLAYER_MIN_SPEED, 10);
		ConfigManager::setInteger(ConfigManager::PLAYER_MAX_SPEED, 5000);
		ConfigManager::setBoolean(ConfigManager::POST_MOVEMENT_SESSION_CLEANUP_ENABLED, true);

		g_dispatcher.start();
		g_scheduler.start();

		for (uint16_t x = start.x - 2; x <= start.x + 2; ++x) {
			for (uint16_t y = start.y - 2; y <= start.y + 2; ++y) {
				ensureWalkTile(Position{x, y, start.z});
			}
		}

		player = std::make_shared<Player>(nullptr);
		player->setName("walk scheduler player");
		player->setGroup(std::make_shared<Group>());
		player->setBaseSpeed(2000);
		CHECK(g_game.internalPlaceCreature(player.get(), start, false, true));
		processedAtStart = g_dispatcher.getTotalTasksProcessed();
	}

	~PlayerWalkFixture()
	{
		CreatureWalkTestAccess::clearPlayerTasks(*player);
		player->stopEventWalk();
		if (!player->isRemoved()) {
			g_game.removeCreature(player.get(), false);
		}

		for (size_t attempts = 0; attempts < 8 && g_reactor.hasPendingTasks(); ++attempts) {
			TaskReactorTestAccess::makeScheduledTasksReady(g_reactor);
			g_reactor.runOnce();
		}
		g_scheduler.shutdown();
		g_dispatcher.shutdown();
		if (luaInitialized) {
			g_luaEnvironment.closeState();
		}

		g_moveEvents.swap(previousMoveEvents);
		ConfigManager::setInteger(ConfigManager::PLAYER_MIN_SPEED, oldMinSpeed);
		ConfigManager::setInteger(ConfigManager::PLAYER_MAX_SPEED, oldMaxSpeed);
		ConfigManager::setBoolean(ConfigManager::POST_MOVEMENT_SESSION_CLEANUP_ENABLED,
		                          oldPostMovementSessionCleanup);
		g_globalEvents = oldGlobalEvents;
		g_events = oldEvents;
		g_chat = oldChat;
	}

	void runScheduledTasks()
	{
		TaskReactorTestAccess::makeScheduledTasksReady(g_reactor);
		g_reactor.runOnce();
	}

	uint64_t processedTasks() const { return g_dispatcher.getTotalTasksProcessed() - processedAtStart; }

	void installMovementRecorder()
	{
		CHECK(!luaInitialized);
		CHECK(g_luaEnvironment.initState());
		luaInitialized = true;
		CHECK(EventsTestAccess::installMovementRecorder(events));
	}

	int64_t movementHookCalls() const { return EventsTestAccess::globalInteger(events, "movementHookCalls"); }
	int64_t movementHookFlags() const { return EventsTestAccess::globalInteger(events, "movementHookFlags"); }
	Position movementHookFrom() const
	{
		return Position{
			static_cast<uint16_t>(EventsTestAccess::globalInteger(events, "movementHookFromX")),
			static_cast<uint16_t>(EventsTestAccess::globalInteger(events, "movementHookFromY")),
			static_cast<uint8_t>(EventsTestAccess::globalInteger(events, "movementHookFromZ"))};
	}
	Position movementHookTo() const
	{
		return Position{
			static_cast<uint16_t>(EventsTestAccess::globalInteger(events, "movementHookToX")),
			static_cast<uint16_t>(EventsTestAccess::globalInteger(events, "movementHookToY")),
			static_cast<uint8_t>(EventsTestAccess::globalInteger(events, "movementHookToZ"))};
	}

	static constexpr Position start{1050, 1050, 7};
	std::shared_ptr<Player> player;

private:
	Chat* oldChat;
	Events* oldEvents;
	GlobalEvents* oldGlobalEvents;
	int64_t oldMinSpeed;
	int64_t oldMaxSpeed;
	bool oldPostMovementSessionCleanup;
	std::unique_ptr<MoveEvents> previousMoveEvents;
	Chat chat;
	Events events;
	GlobalEvents globalEvents;
	uint64_t processedAtStart = 0;
	bool luaInitialized = false;
};

} // namespace

TEST_CASE(creature_walk_rejects_stale_generation_after_stop_and_restart)
{
	WalkFixture world;
	auto& creature = *world.creature;
	creature.addEventWalk();
	CHECK(creature.eventId() != 0);
	const auto oldGeneration = creature.generation();
	creature.stopEventWalk();
	creature.addEventWalk();
	const auto replacementId = creature.eventId();
	CHECK(replacementId != 0);
	CHECK(creature.generation() != oldGeneration);

	g_game.checkCreatureWalk(creature.getID(), oldGeneration);
	CHECK(creature.walkCalls == 0);
	CHECK(creature.eventId() == replacementId);
}

TEST_CASE(creature_walk_current_generation_executes_and_completes)
{
	WalkFixture world;
	auto& creature = *world.creature;
	creature.addEventWalk();
	CHECK(creature.eventId() != 0);

	g_game.checkCreatureWalk(creature.getID(), creature.generation());
	CHECK(creature.walkCalls == 1);
	CHECK(creature.completionCalls == 1);
	CHECK(creature.eventId() == 0);
}

TEST_CASE(creature_walk_completion_rearm_keeps_its_single_tracked_event)
{
	WalkFixture world;
	auto& creature = *world.creature;
	creature.rearmOnComplete = true;
	creature.addEventWalk();
	const auto initialGeneration = creature.generation();

	g_game.checkCreatureWalk(creature.getID(), initialGeneration);
	CHECK(creature.walkCalls == 1);
	CHECK(creature.completionCalls == 1);
	CHECK(creature.generation() != initialGeneration);
	CHECK(creature.replacementId != 0);
	CHECK(creature.eventId() == creature.replacementId);
}

TEST_CASE(creature_walk_completion_rearm_after_event_id_was_cleared)
{
	WalkFixture world;
	auto& creature = *world.creature;
	creature.clearEventBeforeStop = true;
	creature.rearmOnComplete = true;
	creature.addEventWalk();
	const auto initialGeneration = creature.generation();

	g_game.checkCreatureWalk(creature.getID(), initialGeneration);
	CHECK(creature.generation() != initialGeneration);
	CHECK(creature.replacementId != 0);
	CHECK(creature.eventId() == creature.replacementId);
}

TEST_CASE(creature_walk_callback_ignores_removed_creature)
{
	WalkFixture world;
	auto& creature = *world.creature;
	const auto generation = creature.generation();
	CHECK(g_game.removeCreature(&creature, false));

	g_game.checkCreatureWalk(creature.getID(), generation);
	CHECK(creature.walkCalls == 0);
}

TEST_CASE(player_isolated_manual_step_does_not_rearm_empty_walk)
{
	PlayerWalkFixture world;
	auto& player = *world.player;

	g_game.playerMove(player.getID(), DIRECTION_NORTH);
	world.runScheduledTasks();

	CHECK(player.getPosition() == (Position{1050, 1049, 7}));
	CHECK(CreatureWalkTestAccess::pendingDirections(player) == 0);
	CHECK(CreatureWalkTestAccess::eventId(player) == 0);
	CHECK(world.processedTasks() == 1);
	CHECK(!g_reactor.hasPendingTasks());

	world.runScheduledTasks();
	CHECK(world.processedTasks() == 1);
}

TEST_CASE(player_manual_step_during_cooldown_uses_remaining_delay_once)
{
	PlayerWalkFixture world;
	auto& player = *world.player;

	g_game.playerMove(player.getID(), DIRECTION_NORTH);
	world.runScheduledTasks();
	const int32_t remainingDelay = player.getWalkDelay();
	CHECK(remainingDelay > 0);

	const auto beforeSchedule = std::chrono::steady_clock::now();
	g_game.playerMove(player.getID(), DIRECTION_EAST);
	const auto afterSchedule = std::chrono::steady_clock::now();
	const uint32_t eventId = CreatureWalkTestAccess::eventId(player);
	CHECK(eventId != 0);
	const auto fireAt = TaskReactorTestAccess::scheduledFireAt(g_reactor, eventId);
	CHECK(fireAt.has_value());
	CHECK(*fireAt >= beforeSchedule + std::chrono::milliseconds(remainingDelay));
	CHECK(*fireAt <= afterSchedule + std::chrono::milliseconds(remainingDelay));
	CHECK(player.getPosition() == (Position{1050, 1049, 7}));

	CreatureWalkTestAccess::finishCooldown(player);
	world.runScheduledTasks();
	CHECK(player.getPosition() == (Position{1051, 1049, 7}));
	CHECK(CreatureWalkTestAccess::eventId(player) == 0);
	CHECK(world.processedTasks() == 2);
	CHECK(!g_reactor.hasPendingTasks());
}

TEST_CASE(player_pending_walk_task_runs_once_after_completion)
{
	PlayerWalkFixture world;
	auto& player = *world.player;
	int actionRuns = 0;
	CreatureWalkTestAccess::setWalkAction(
	    player, createSchedulerTask(1, ([&actionRuns]() { ++actionRuns; })));
	player.startAutoWalk(DIRECTION_NORTH);

	world.runScheduledTasks();
	CHECK(player.getPosition() == (Position{1050, 1049, 7}));
	CHECK(actionRuns == 0);
	CHECK(CreatureWalkTestAccess::eventId(player) != 0);

	CreatureWalkTestAccess::finishCooldown(player);
	world.runScheduledTasks();
	CHECK(actionRuns == 0);
	CHECK(CreatureWalkTestAccess::eventId(player) == 0);

	world.runScheduledTasks();
	CHECK(actionRuns == 1);
	CHECK(world.processedTasks() == 3);
	CHECK(!g_reactor.hasPendingTasks());

	world.runScheduledTasks();
	CHECK(actionRuns == 1);
}

TEST_CASE(player_multistep_autowalk_keeps_each_physical_step)
{
	PlayerWalkFixture world;
	auto& player = *world.player;

	g_game.playerAutoWalk(player.getID(), {DIRECTION_EAST, DIRECTION_NORTH});
	world.runScheduledTasks();
	CHECK(player.getPosition() == (Position{1050, 1049, 7}));
	CHECK(CreatureWalkTestAccess::pendingDirections(player) == 1);
	CHECK(CreatureWalkTestAccess::eventId(player) != 0);

	CreatureWalkTestAccess::finishCooldown(player);
	world.runScheduledTasks();
	CHECK(player.getPosition() == (Position{1051, 1049, 7}));
	CHECK(CreatureWalkTestAccess::pendingDirections(player) == 0);
	CHECK(CreatureWalkTestAccess::eventId(player) == 0);
	CHECK(world.processedTasks() == 2);
	CHECK(!g_reactor.hasPendingTasks());
}

TEST_CASE(player_movement_sessions_use_native_flags)
{
	PlayerWalkFixture world;
	auto& player = *world.player;

	CHECK(player.getMovementSessionFlags() == 0);
	player.setMovementSessionActive(MovementSessionFlag::Exercise, true);
	player.setMovementSessionActive(MovementSessionFlag::Market, true);
	CHECK(player.hasMovementSession(MovementSessionFlag::Exercise));
	CHECK(player.hasMovementSession(MovementSessionFlag::Market));
	CHECK(!player.hasMovementSession(MovementSessionFlag::Forge));

	player.setMovementSessionActive(MovementSessionFlag::Exercise, false);
	CHECK(!player.hasMovementSession(MovementSessionFlag::Exercise));
	CHECK(player.hasMovementSession(MovementSessionFlag::Market));
}

TEST_CASE(player_successful_step_hook_runs_once_with_real_positions)
{
	PlayerWalkFixture world;
	world.installMovementRecorder();
	auto& player = *world.player;
	player.setMovementSessionActive(MovementSessionFlag::Market, true);

	CHECK(g_game.internalMoveCreature(&player, DIRECTION_NORTH) == RETURNVALUE_NOERROR);
	CHECK(world.movementHookCalls() == 1);
	CHECK(world.movementHookFrom() == PlayerWalkFixture::start);
	CHECK(world.movementHookTo() == player.getPosition());
	CHECK(world.movementHookFlags() == static_cast<uint8_t>(MovementSessionFlag::Market));
}

TEST_CASE(player_successful_step_hook_preserves_generic_callback_without_session)
{
	PlayerWalkFixture world;
	world.installMovementRecorder();
	auto& player = *world.player;

	CHECK(player.getMovementSessionFlags() == 0);
	CHECK(g_game.internalMoveCreature(&player, DIRECTION_NORTH) == RETURNVALUE_NOERROR);
	CHECK(world.movementHookCalls() == 1);
	CHECK(world.movementHookFrom() == PlayerWalkFixture::start);
	CHECK(world.movementHookTo() == player.getPosition());
	CHECK(world.movementHookFlags() == 0);
}

TEST_CASE(player_post_movement_session_cleanup_can_be_disabled)
{
	PlayerWalkFixture world;
	world.installMovementRecorder();
	ConfigManager::setBoolean(ConfigManager::POST_MOVEMENT_SESSION_CLEANUP_ENABLED, false);

	CHECK(g_game.internalMoveCreature(world.player.get(), DIRECTION_NORTH) == RETURNVALUE_NOERROR);
	CHECK(world.movementHookCalls() == 0);
}

TEST_CASE(player_failed_step_does_not_run_success_hook)
{
	PlayerWalkFixture world;
	world.installMovementRecorder();
	auto& player = *world.player;
	player.setMovementSessionActive(MovementSessionFlag::Exercise, true);

	auto blocker = std::make_shared<Player>(nullptr);
	blocker->setName("movement blocker");
	blocker->setGroup(std::make_shared<Group>());
	const Position destination{PlayerWalkFixture::start.x, static_cast<uint16_t>(PlayerWalkFixture::start.y - 1),
	                           PlayerWalkFixture::start.z};
	CHECK(g_game.internalPlaceCreature(blocker.get(), destination, false, true));

	CHECK(g_game.internalMoveCreature(&player, DIRECTION_NORTH) != RETURNVALUE_NOERROR);
	CHECK(player.getPosition() == PlayerWalkFixture::start);
	CHECK(world.movementHookCalls() == 0);
	CHECK(player.hasMovementSession(MovementSessionFlag::Exercise));

	CHECK(g_game.removeCreature(blocker.get(), false));
}

TEST_CASE(player_failed_step_without_session_does_not_run_generic_callback)
{
	PlayerWalkFixture world;
	world.installMovementRecorder();
	auto& player = *world.player;

	auto blocker = std::make_shared<Player>(nullptr);
	blocker->setName("movement blocker without session");
	blocker->setGroup(std::make_shared<Group>());
	const Position destination{PlayerWalkFixture::start.x, static_cast<uint16_t>(PlayerWalkFixture::start.y - 1),
	                           PlayerWalkFixture::start.z};
	CHECK(g_game.internalPlaceCreature(blocker.get(), destination, false, true));

	CHECK(g_game.internalMoveCreature(&player, DIRECTION_NORTH) != RETURNVALUE_NOERROR);
	CHECK(player.getPosition() == PlayerWalkFixture::start);
	CHECK(world.movementHookCalls() == 0);

	CHECK(g_game.removeCreature(blocker.get(), false));
}

TEST_CASE(player_blocked_tile_does_not_run_success_hook)
{
	PlayerWalkFixture world;
	world.installMovementRecorder();
	auto& player = *world.player;
	player.setMovementSessionActive(MovementSessionFlag::Market, true);

	const Position destination{PlayerWalkFixture::start.x, static_cast<uint16_t>(PlayerWalkFixture::start.y - 1),
	                           PlayerWalkFixture::start.z};
	Tile* destinationTile = g_game.map.getTile(destination);
	destinationTile->setFlag(TILESTATE_BLOCKSOLID);
	CHECK(g_game.internalMoveCreature(&player, DIRECTION_NORTH) != RETURNVALUE_NOERROR);
	destinationTile->resetFlag(TILESTATE_BLOCKSOLID);

	CHECK(player.getPosition() == PlayerWalkFixture::start);
	CHECK(world.movementHookCalls() == 0);
}

TEST_CASE(player_pz_restriction_does_not_run_success_hook)
{
	PlayerWalkFixture world;
	world.installMovementRecorder();
	auto& player = *world.player;
	player.setMovementSessionActive(MovementSessionFlag::Imbuing, true);
	CreatureWalkTestAccess::setPzLocked(player, true);

	const Position destination{PlayerWalkFixture::start.x, static_cast<uint16_t>(PlayerWalkFixture::start.y - 1),
	                           PlayerWalkFixture::start.z};
	Tile* destinationTile = g_game.map.getTile(destination);
	destinationTile->setFlag(TILESTATE_PROTECTIONZONE);
	CHECK(g_game.internalMoveCreature(&player, DIRECTION_NORTH) == RETURNVALUE_PLAYERISPZLOCKED);
	destinationTile->resetFlag(TILESTATE_PROTECTIONZONE);
	CreatureWalkTestAccess::setPzLocked(player, false);

	CHECK(player.getPosition() == PlayerWalkFixture::start);
	CHECK(world.movementHookCalls() == 0);
}

TEST_CASE(player_drunk_step_hook_reports_the_actual_destination)
{
	PlayerWalkFixture world;
	world.installMovementRecorder();
	auto& player = *world.player;
	player.setMovementSessionActive(MovementSessionFlag::Forge, true);

	auto drunk = Condition::createCondition(CONDITIONID_COMBAT, CONDITION_DRUNK, 60'000, 100);
	CHECK(drunk != nullptr);
	CHECK(player.addCondition(std::move(drunk)));

	Direction actualDirection = DIRECTION_NORTH;
	player.onWalk(actualDirection);
	const Position expectedDestination = getNextPosition(actualDirection, PlayerWalkFixture::start);
	CHECK(g_game.internalMoveCreature(&player, actualDirection) == RETURNVALUE_NOERROR);
	CHECK(player.getPosition() == expectedDestination);
	CHECK(world.movementHookCalls() == 1);
	CHECK(world.movementHookFrom() == PlayerWalkFixture::start);
	CHECK(world.movementHookTo() == expectedDestination);
}

TEST_CASE(player_nested_map_moves_emit_one_final_position_hook)
{
	PlayerWalkFixture world;
	world.installMovementRecorder();
	auto& player = *world.player;
	player.setMovementSessionActive(MovementSessionFlag::Imbuing, true);

	const Position east{static_cast<uint16_t>(PlayerWalkFixture::start.x + 1), PlayerWalkFixture::start.y,
	                    PlayerWalkFixture::start.z};
	const Position southeast{east.x, static_cast<uint16_t>(east.y + 1), east.z};
	{
		PlayerMovementEventScope logicalMove(&player);
		g_game.map.moveCreature(player, *g_game.map.getTile(east));
		g_game.map.moveCreature(player, *g_game.map.getTile(southeast));
	}

	CHECK(world.movementHookCalls() == 1);
	CHECK(world.movementHookFrom() == PlayerWalkFixture::start);
	CHECK(world.movementHookTo() == southeast);
}

TEST_CASE(player_teleport_emits_one_final_position_hook)
{
	PlayerWalkFixture world;
	world.installMovementRecorder();
	auto& player = *world.player;
	player.setMovementSessionActive(MovementSessionFlag::Market, true);
	const Position destination{static_cast<uint16_t>(PlayerWalkFixture::start.x + 2),
	                           static_cast<uint16_t>(PlayerWalkFixture::start.y + 2),
	                           PlayerWalkFixture::start.z};

	g_performanceMetrics.setEnabled(true);
	const uint64_t spectatorsBefore = g_performanceMetrics.getMetricCalls(PerformanceMetric::MapGetSpectators);
	CHECK(g_game.internalTeleport(&player, destination, false, 0, CONST_ME_NONE) == RETURNVALUE_NOERROR);
	const uint64_t spectatorsAfter = g_performanceMetrics.getMetricCalls(PerformanceMetric::MapGetSpectators);
	g_performanceMetrics.setEnabled(false);
	// Teleports use fresh post-move notification snapshots in addition to the
	// pre-move snapshots used for movement visibility and stack positions.
	CHECK(spectatorsAfter - spectatorsBefore >= 4);
	CHECK(world.movementHookCalls() == 1);
	CHECK(world.movementHookFrom() == PlayerWalkFixture::start);
	CHECK(world.movementHookTo() == destination);
}

TEST_CASE(player_long_teleport_closes_world_containers_out_of_range)
{
	PlayerWalkFixture world;
	auto& player = *world.player;
	Tile* oldTile = player.getTile();
	CHECK(oldTile != nullptr);

	auto container = std::make_shared<Container>(ITEM_BAG, 8);
	oldTile->addThing(container.get());
	player.addContainer(0, container.get());
	CHECK(player.getContainerByID(0) == container.get());

	const Position destination{static_cast<uint16_t>(PlayerWalkFixture::start.x + 10),
	                           static_cast<uint16_t>(PlayerWalkFixture::start.y + 10),
	                           PlayerWalkFixture::start.z};
	ensureWalkTile(destination);
	CHECK(g_game.internalTeleport(&player, destination, false, 0, CONST_ME_NONE) == RETURNVALUE_NOERROR);
	CHECK(player.getContainerByID(0) == nullptr);

	oldTile->removeThing(container.get(), 0);
}

TEST_CASE(player_diagonal_step_reports_the_exact_destination)
{
	PlayerWalkFixture world;
	world.installMovementRecorder();
	auto& player = *world.player;
	player.setMovementSessionActive(MovementSessionFlag::Forge, true);
	const Position destination{static_cast<uint16_t>(PlayerWalkFixture::start.x + 1),
	                           static_cast<uint16_t>(PlayerWalkFixture::start.y - 1),
	                           PlayerWalkFixture::start.z};

	CHECK(g_game.internalMoveCreature(&player, DIRECTION_NORTHEAST) == RETURNVALUE_NOERROR);
	CHECK(world.movementHookCalls() == 1);
	CHECK(world.movementHookFrom() == PlayerWalkFixture::start);
	CHECK(world.movementHookTo() == destination);
}

TEST_CASE(player_map_move_reuses_tile_spectator_snapshots)
{
	PlayerWalkFixture world;
	auto& player = *world.player;
	g_performanceMetrics.setEnabled(true);
	const uint64_t before = g_performanceMetrics.getMetricCalls(PerformanceMetric::MapGetSpectators);

	CHECK(g_game.internalMoveCreature(&player, DIRECTION_EAST) == RETURNVALUE_NOERROR);

	const uint64_t after = g_performanceMetrics.getMetricCalls(PerformanceMetric::MapGetSpectators);
	g_performanceMetrics.setEnabled(false);
	CHECK(after - before == 2);
}

TFS_TEST_MAIN()
