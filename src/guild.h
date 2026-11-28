// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_GUILD_H
#define FS_GUILD_H

class Player;

using GuildWarVector = std::vector<uint32_t>;

struct GuildRank
{
	uint32_t id;
	std::string name;
	uint8_t level;

	GuildRank(uint32_t id, std::string_view name, uint8_t level) : id{id}, name{name}, level{level} {}
};

using GuildRank_ptr = std::shared_ptr<GuildRank>;
using PlayerWeakList = std::list<std::weak_ptr<Player>>;
using PlayerRefVector = std::vector<std::shared_ptr<Player>>;

class Guild
{
public:
	static constexpr uint8_t MEMBER_RANK_LEVEL_DEFAULT = 1;

	Guild(uint32_t id, std::string_view name) : name{name}, id{id} {}

	uint32_t getId() const { return id; }
	const std::string& getName() const { return name; }

	void addMember(Player* player);
	void removeMember(Player* player);
	PlayerRefVector getMembersOnlineRefs() const;
	uint32_t getMemberCount() const { return memberCount; }
	void setMemberCount(uint32_t count) { memberCount = count; }

	uint64_t getBankBalance() const { return bankBalance; }
	bool setBankBalance(uint64_t balance);
	void loadBankBalance(uint64_t balance) { bankBalance = balance; }

	uint32_t getHouseId() const { return houseId; }
	void setHouseId(uint32_t id) { houseId = id; }

	uint32_t getOwnerGUID() const { return ownerGUID; }
	void setOwnerGUID(uint32_t guid) { ownerGUID = guid; }

	void addRank(uint32_t rankId, std::string_view rankName, uint8_t level);
	const std::vector<GuildRank_ptr>& getRanks() const { return ranks; }
	GuildRank_ptr getRankById(uint32_t rankId) const;
	GuildRank_ptr getRankByName(std::string_view name) const;
	GuildRank_ptr getRankByLevel(uint8_t level) const;

	const std::string& getMotd() const { return motd; }
	void setMotd(std::string_view motd) { this->motd = motd; }

private:
	
	PlayerWeakList membersOnline;
	std::vector<GuildRank_ptr> ranks;
	std::string name;
	std::string motd;
	uint32_t id;
	uint32_t memberCount = 0;
	uint64_t bankBalance = 0;
	uint32_t ownerGUID = 0;
	uint32_t houseId = 0;
};

using Guild_ptr = std::shared_ptr<Guild>;

namespace IOGuild {
void blockBankBalance(uint32_t guildId);
bool isBankBalanceBlocked(uint32_t guildId);
Guild_ptr loadGuild(uint32_t guildId);
uint32_t getGuildIdByName(std::string_view name);
std::string getGuildNameById(uint32_t id);
void guildWar(Player* player, const std::string& param);
void guildBalance(Player* player, const std::string& param);
void registerGuildWarKill(Player* killer, Player* victim);
} // namespace IOGuild

#endif
