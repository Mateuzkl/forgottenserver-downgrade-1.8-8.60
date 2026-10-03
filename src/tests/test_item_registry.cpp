#include "../otpch.h"

#include "../item.h"
#include "../bed.h"
#include "../combat.h"
#include "../depotchest.h"
#include "../depotlocker.h"
#include "../game.h"
#include "../house.h"
#include "../inbox.h"
#include "../mailbox.h"
#include "../rewardchest.h"
#include "../storeinbox.h"
#include "../teleport.h"
#include "../trashholder.h"
#include "test_support.h"

#include <atomic>
#include <barrier>
#include <latch>
#include <thread>

extern Game g_game;

namespace {
class TrackedItem final : public Item
{
public:
	explicit TrackedItem(std::atomic<bool>& destroyed) : Item(0), destroyed(destroyed) {}
	~TrackedItem() override { destroyed.store(true); }

private:
	std::atomic<bool>& destroyed;
};

class ConstructorProbeItem final : public Item
{
public:
	explicit ConstructorProbeItem(bool& pinnedInConstructor) : Item(0)
	{
		pinnedInConstructor = static_cast<bool>(Item::pin(this));
	}
};

struct ItemTypeGuard
{
	ItemType& type = Item::items.getItemType(0);
	ItemType previous = std::move(type);
	ItemTypeGuard()
	{
		type = ItemType{};
		type.id = 32000;
	}
	~ItemTypeGuard() { type = std::move(previous); }
};

// Pause before Item::~Item erases metadata. Its expired control block must
// already reject pins, without any access to the partly destroyed Item.
class PausedDestructorItem final : public Item
{
public:
	PausedDestructorItem(std::latch& entered, std::latch& finish) : Item(0), entered(entered), finish(finish) {}
	~PausedDestructorItem() override
	{
		entered.count_down();
		finish.wait();
	}

private:
	std::latch& entered;
	std::latch& finish;
};

class RemovableCylinder final : public Container
{
public:
	RemovableCylinder() : Container(0, 1) {}
	ReturnValue queryRemove(const Thing&, uint32_t, uint32_t, Creature*) const override { return RETURNVALUE_NOERROR; }
};

struct FirstDereferenceState
{
	std::weak_ptr<Item> weak;
	std::latch releaseOwner{1};
	std::latch ownerReleased{1};
	RemovableCylinder cylinder;
	bool entered = false;
	bool hadPin = false;
	bool aliveAfterRelease = false;
};

class FirstDereferenceItem final : public Item
{
public:
	explicit FirstDereferenceItem(FirstDereferenceState& state) : Item(0), state(state) {}
	Cylinder* getParent() const override
	{
		// Fixture hook in the first virtual dereference, not production sleeps.
		// Capture locals before release; a missing entry pin is detected without
		// accessing this after dropping the original owner's strong reference.
		auto& observation = state;
		auto* cylinder = &observation.cylinder;
		observation.entered = true;
		observation.hadPin = observation.weak.use_count() >= 2;
		observation.releaseOwner.count_down();
		observation.ownerReleased.wait();
		observation.aliveAfterRelease = !observation.weak.expired();
		return observation.hadPin ? cylinder : nullptr;
	}

private:
	FirstDereferenceState& state;
};
} // namespace

TEST_CASE(item_registry_strong_pin_wins_before_concurrent_owner_release)
{
	std::atomic<bool> destroyed{false};
	auto owner = Item::make<TrackedItem>(destroyed);
	Item* raw = owner.get();
	std::weak_ptr<Item> weak = owner;
	std::latch release{1};
	std::latch released{1};
	std::jthread worker([owner = std::move(owner), &release, &released]() mutable {
		release.wait();
		owner.reset();
		released.count_down();
	});
	auto pin = Item::pin(raw);
	release.count_down();
	released.wait();
	CHECK(pin.get() == raw);
	CHECK(!weak.expired());
	CHECK(!destroyed.load());
	CHECK(pin->getID() == 0);
	pin.reset();
	CHECK(weak.expired());
	CHECK(destroyed.load());
	CHECK(!Item::pin(raw));
}

TEST_CASE(item_registry_owner_release_wins_before_pin)
{
	std::atomic<bool> destroyed{false};
	auto owner = Item::make<TrackedItem>(destroyed);
	Item* raw = owner.get();
	std::latch released{1};
	std::jthread worker([owner = std::move(owner), &released]() mutable {
		owner.reset();
		released.count_down();
	});
	released.wait();
	CHECK(destroyed.load());
	CHECK(!Item::pin(raw));
	CHECK(!Item::pin(nullptr));
}

TEST_CASE(item_registry_pin_fails_during_derived_destruction)
{
	std::latch entered{1};
	std::latch finish{1};
	auto owner = Item::make<PausedDestructorItem>(entered, finish);
	Item* raw = owner.get();
	std::jthread worker([owner = std::move(owner)]() mutable { owner.reset(); });
	entered.wait();
	auto pin = Item::pin(raw);
	finish.count_down();
	worker.join();
	CHECK(!pin);
	CHECK(!Item::pin(raw));
}

TEST_CASE(item_registry_internal_remove_pins_before_first_dereference)
{
	FirstDereferenceState state;
	auto owner = Item::make<FirstDereferenceItem>(state);
	state.weak = owner;
	Item* raw = owner.get();
	std::jthread worker([owner = std::move(owner), &state]() mutable {
		state.releaseOwner.wait();
		owner.reset();
		state.ownerReleased.count_down();
	});
	// test=true covers the formerly unpinned query-only path as well.
	const auto result = g_game.internalRemoveItem(raw, -1, true);
	if (!state.entered) {
		state.releaseOwner.count_down();
	}
	worker.join();
	CHECK(state.entered);
	CHECK(state.hadPin);
	CHECK(state.aliveAfterRelease);
	CHECK(result == RETURNVALUE_NOERROR);
	CHECK(state.weak.expired());
	CHECK(!Item::pin(raw));
	CHECK(g_game.internalRemoveItem(raw) == RETURNVALUE_NOTPOSSIBLE);
	CHECK(g_game.transformItem(raw, 0) == nullptr);
}

TEST_CASE(item_registry_factory_registers_derived_and_copy_ownership)
{
	std::vector<std::shared_ptr<Item>> owners{
	    Item::make<Item>(0),       Item::make<Container>(0),   Item::make<BedItem>(0),
	    Item::make<Door>(0),       Item::make<Teleport>(0),    Item::make<MagicField>(0),
	    Item::make<Mailbox>(0),    Item::make<TrashHolder>(0), Item::make<DepotLocker>(0),
	    Item::make<DepotChest>(0), Item::make<DepotBox>(0),    Item::make<RewardChest>(0),
	    Item::make<Inbox>(0),      Item::make<StoreInbox>(0),  Item::make<HouseTransferItem>(nullptr)};
	for (const auto& owner : owners) {
		CHECK(Item::pin(owner.get()) == owner);
		CHECK(!Item::pin(owner.get()).owner_before(owner));
		CHECK(!owner.owner_before(Item::pin(owner.get())));
	}
	auto copy = Item::make<Item>(*owners.front());
	CHECK(Item::pin(copy.get()) == copy);
	CHECK(copy.get() != owners.front().get());
	// House::transfer_container is embedded, not a shared-ownership item.
	Item embedded(0);
	CHECK(!Item::pin(&embedded));
	CHECK(g_game.internalRemoveItem(&embedded) == RETURNVALUE_NOTPOSSIBLE);
}

TEST_CASE(item_registry_registers_only_after_shared_construction)
{
	bool pinnedInConstructor = true;
	auto item = Item::make<ConstructorProbeItem>(pinnedInConstructor);
	CHECK(!pinnedInConstructor);
	CHECK(Item::pin(item.get()) == item);
	// Bypassing the shared-item factory fails closed, rather than attempting
	// weak_from_this through a raw candidate. Embedded objects remain supported.
	auto unregistered = std::make_shared<Item>(0);
	CHECK(!Item::pin(unregistered.get()));
}

TEST_CASE(item_registry_create_stream_container_and_clone_publish_pins)
{
	ItemTypeGuard guard;
	constexpr uint16_t id = 32000;
	auto item = Item::CreateItem(id);
	CHECK(item);
	CHECK(Item::pin(item.get()) == item);
	auto clone = item->clone();
	CHECK(clone);
	CHECK(clone != item);
	CHECK(Item::pin(clone.get()) == clone);
	auto container = Item::CreateItemAsContainer(id, 4);
	CHECK(container);
	CHECK(Item::pin(container.get()) == container);
	PropStream stream;
	stream.init(reinterpret_cast<const char*>(&id), sizeof(id));
	auto decoded = Item::CreateItem(stream);
	CHECK(decoded);
	CHECK(Item::pin(decoded.get()) == decoded);
}

TEST_CASE(item_registry_handles_parallel_construction_copy_lookup_and_destruction)
{
	ItemTypeGuard guard;
	constexpr uint16_t id = 32000;
	constexpr size_t workers = 8;
	auto sentinel = Item::make<Item>(id);
	std::barrier start(static_cast<std::ptrdiff_t>(workers + 1));
	std::atomic<bool> valid{true};
	{
		std::vector<std::jthread> threads;
		for (size_t worker = 0; worker < workers; ++worker) {
			threads.emplace_back([&]() {
				start.arrive_and_wait();
				for (size_t iteration = 0; iteration < 200; ++iteration) {
					std::vector<std::shared_ptr<Item>> batch;
					for (size_t entry = 0; entry < 32; ++entry) {
						auto item = Item::make<Item>(id);
						auto copy = Item::make<Item>(*item);
						auto clone = item->clone();
						auto pin = Item::pin(item.get());
						if (pin != item || Item::pin(copy.get()) != copy || !clone || Item::pin(clone.get()) != clone ||
						    Item::pin(sentinel.get()) != sentinel || Item::pin(nullptr)) {
							valid.store(false);
						}
						const std::weak_ptr<Item> weak = item;
						item.reset();
						if (weak.expired() || !pin || pin->getID() != id) {
							valid.store(false);
						}
						batch.push_back(std::move(pin));
						batch.push_back(std::move(copy));
						batch.push_back(std::move(clone));
					}
				}
			});
		}
		start.arrive_and_wait();
	}
	CHECK(valid.load());
	CHECK(Item::pin(sentinel.get()) == sentinel);
	Item* address = sentinel.get();
	sentinel.reset();
	CHECK(!Item::pin(address));
}

// Retirement is deliberately irreversible. Keep this case last in this process.
TEST_CASE(item_registry_retirement_is_atomic_with_live_worker_items)
{
	constexpr size_t workers = 8;
	std::barrier ready(static_cast<std::ptrdiff_t>(workers + 1));
	std::latch retired{1};
	std::atomic<bool> valid{true};
	auto sentinel = Item::make<Item>(0);
	auto existingPin = Item::pin(sentinel.get());
	CHECK(existingPin);
	{
		std::vector<std::jthread> threads;
		for (size_t worker = 0; worker < workers; ++worker) {
			threads.emplace_back([&]() {
				std::vector<std::shared_ptr<Item>> held;
				for (size_t entry = 0; entry < 128; ++entry) {
					held.push_back(Item::make<Item>(0));
				}
				ready.arrive_and_wait();
				// Exercise construction/pinning/destruction concurrently with retire.
				for (size_t entry = 0; entry < 128; ++entry) {
					auto temporary = Item::make<Item>(0);
					auto pin = Item::pin(temporary.get());
				}
				retired.wait();
				for (const auto& item : held) {
					if (Item::pin(item.get()) || item->getID() != 0) {
						valid.store(false);
					}
				}
				auto after = Item::make<Item>(0);
				auto copy = Item::make<Item>(*after);
				if (Item::pin(after.get()) || Item::pin(copy.get())) {
					valid.store(false);
				}
			});
		}
		ready.arrive_and_wait();
		Item::clearGlobalRegistry();
		retired.count_down();
	}
	CHECK(valid.load());
	CHECK(!Item::pin(sentinel.get()));
	sentinel.reset();
	CHECK(existingPin->getID() == 0);
	const std::weak_ptr<Item> weak = existingPin;
	existingPin.reset();
	CHECK(weak.expired());
}

TFS_TEST_MAIN()
