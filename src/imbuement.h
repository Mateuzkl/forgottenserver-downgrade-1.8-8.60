#ifndef BT_IMBUEMENT_H
#define BT_IMBUEMENT_H

#include "otpch.h"
#include "fileloader.h"
#include "scheduler.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

enum ImbuementType : uint8_t {
    IMBUEMENT_TYPE_NONE,
    IMBUEMENT_TYPE_FIRE_DAMAGE,
    IMBUEMENT_TYPE_EARTH_DAMAGE,
    IMBUEMENT_TYPE_ICE_DAMAGE,
    IMBUEMENT_TYPE_ENERGY_DAMAGE,
    IMBUEMENT_TYPE_DEATH_DAMAGE,
    IMBUEMENT_TYPE_HOLY_DAMAGE,
    IMBUEMENT_TYPE_LIFE_LEECH,
    IMBUEMENT_TYPE_MANA_LEECH,
    IMBUEMENT_TYPE_CRITICAL_CHANCE,
    IMBUEMENT_TYPE_CRITICAL_AMOUNT,
    IMBUEMENT_TYPE_FIRE_RESIST,
    IMBUEMENT_TYPE_EARTH_RESIST,
    IMBUEMENT_TYPE_ICE_RESIST,
    IMBUEMENT_TYPE_ENERGY_RESIST,
    IMBUEMENT_TYPE_HOLY_RESIST,
    IMBUEMENT_TYPE_DEATH_RESIST,
    IMBUEMENT_TYPE_PARALYSIS_DEFLECTION,
    IMBUEMENT_TYPE_SPEED_BOOST,
    IMBUEMENT_TYPE_CAPACITY_BOOST,
    IMBUEMENT_TYPE_FIST_SKILL,
    IMBUEMENT_TYPE_AXE_SKILL,
    IMBUEMENT_TYPE_SWORD_SKILL,
    IMBUEMENT_TYPE_CLUB_SKILL,
    IMBUEMENT_TYPE_DISTANCE_SKILL,
    IMBUEMENT_TYPE_FISHING_SKILL,
    IMBUEMENT_TYPE_SHIELD_SKILL,
    IMBUEMENT_TYPE_MAGIC_LEVEL,
    IMBUEMENT_TYPE_LAST
};

enum ImbuementDecayType : uint8_t {
    IMBUEMENT_DECAY_NONE,
    IMBUEMENT_DECAY_EQUIPPED,
    IMBUEMENT_DECAY_INFIGHT,
    IMBUEMENT_DECAY_LAST
};

struct Imbuement : std::enable_shared_from_this<Imbuement> {
    Imbuement() = default;
    Imbuement(ImbuementType imbuetype, uint32_t value, uint32_t duration,
             ImbuementDecayType decayType = ImbuementDecayType::IMBUEMENT_DECAY_NONE,
             uint16_t baseId = 0)
        : imbuetype{ imbuetype }, value{ value }, duration{ duration },
          decaytype{ decayType }, baseId{ baseId } {};

    ImbuementType imbuetype = ImbuementType::IMBUEMENT_TYPE_NONE;
    uint32_t value = 0;
    uint32_t duration = 0;
    ImbuementDecayType decaytype = ImbuementDecayType::IMBUEMENT_DECAY_NONE;
    uint16_t baseId = 0; // 1=Basic, 2=Intricate, 3=Powerful

    bool isSkill() const;
    bool isSpecialSkill() const;
    bool isDamage() const;
    bool isResist() const;
    bool isStat() const;
    bool isEquipDecay() const;
    bool isInfightDecay() const;

    void serialize(PropWriteStream& propWriteStream) const {
        propWriteStream.write<uint32_t>(static_cast<uint32_t>(imbuetype));
        propWriteStream.write<uint32_t>(value);
        propWriteStream.write<uint32_t>(duration);
        // Pack baseId into high 16 bits of decay field (backward compatible)
        uint32_t packedDecay = (static_cast<uint32_t>(baseId) << 16) | static_cast<uint32_t>(decaytype);
        propWriteStream.write<uint32_t>(packedDecay);
    }

    bool unserialize(PropStream& propReadStream) {
        uint32_t type, val, dur, packedDecay;
        if (!propReadStream.read<uint32_t>(type) ||
            !propReadStream.read<uint32_t>(val) ||
            !propReadStream.read<uint32_t>(dur) ||
            !propReadStream.read<uint32_t>(packedDecay)) {
            return false;
        }

        imbuetype = static_cast<ImbuementType>(type);
        value = val;
        duration = dur;
        decaytype = static_cast<ImbuementDecayType>(packedDecay & 0xFFFF);
        baseId = static_cast<uint16_t>((packedDecay >> 16) & 0xFFFF);
        return true;
    }

    bool operator==(const Imbuement& other) const {
        return imbuetype == other.imbuetype &&
               value == other.value &&
               duration == other.duration &&
               decaytype == other.decaytype;
    }
};

// ============ XML Definitions loaded from imbuements.xml ============

struct ImbuementBase {
    uint16_t id = 0;
    std::string name;
    uint32_t price = 0;
    uint32_t removeCost = 0;
    uint32_t duration = 0;
};

struct ImbuementCategory {
    uint16_t id = 0;
    std::string name;
    bool agressive = false;
};

struct ImbuementItemReq {
    uint16_t itemId = 0;
    uint16_t count = 0;
};

struct ImbuementDefinition {
    std::string name;
    std::string description;
    uint16_t baseId = 0;
    uint16_t categoryId = 0;
    uint16_t iconId = 0;
    uint16_t scrollId = 0;
    uint32_t storage = 0;
    bool premium = false;

    // Effect
    std::string effectType;   // "skill", "damage", "reduction", "speed", "capacity", "vibrancy"
    std::string effectValue;  // "sword", "critical", "fire", "death", etc.
    std::string effectCombat; // "fire", "earth", etc. (for damage/reduction)
    uint32_t bonus = 0;
    uint32_t chance = 0;
    uint32_t value = 0;       // for damage/reduction/speed/capacity percentage

    std::vector<ImbuementItemReq> items;

    // Resolved from base
    uint32_t price = 0;
    uint32_t removeCost = 0;
    uint32_t duration = 0;
    std::string baseName;

    // Resolved ImbuementType and DecayType
    ImbuementType imbuementType = IMBUEMENT_TYPE_NONE;
    ImbuementDecayType decayType = IMBUEMENT_DECAY_NONE;
    uint32_t resolvedValue = 0;
};

class Imbuements {
public:
    static Imbuements& getInstance() {
        static Imbuements instance;
        return instance;
    }

    bool loadFromXml();

    const ImbuementBase* getBaseById(uint16_t id) const;
    const ImbuementCategory* getCategoryById(uint16_t id) const;
    const ImbuementDefinition* getDefinitionByScrollId(uint16_t scrollId) const;
    const std::vector<ImbuementDefinition>& getDefinitions() const { return definitions; }
    const std::unordered_map<uint16_t, ImbuementBase>& getBases() const { return bases; }
    const std::unordered_map<uint16_t, ImbuementCategory>& getCategories() const { return categories; }

private:
    Imbuements() = default;

    void resolveImbuementType(ImbuementDefinition& def);

    std::unordered_map<uint16_t, ImbuementBase> bases;
    std::unordered_map<uint16_t, ImbuementCategory> categories;
    std::vector<ImbuementDefinition> definitions;
    std::unordered_map<uint16_t, size_t> scrollIdIndex; // scrollId -> index in definitions
};

#endif // BT_IMBUEMENT_H