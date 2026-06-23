// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_CONST_H
#define FS_CONST_H

inline constexpr int32_t NETWORKMESSAGE_MAXSIZE = 65500;
inline constexpr uint8_t TILE_MAX_ITEMS = 10;
inline constexpr int32_t TALK_ACTION_EXHAUST_MS = 2000;
inline constexpr uint16_t ACTIONID_NOT_MOVEABLE = 100;

enum MagicEffectClasses : uint16_t
{
	CONST_ME_NONE,

	CONST_ME_DRAWBLOOD = 1,
	CONST_ME_LOSEENERGY = 2,
	CONST_ME_POFF = 3,
	CONST_ME_BLOCKHIT = 4,
	CONST_ME_EXPLOSIONAREA = 5,
	CONST_ME_EXPLOSIONHIT = 6,
	CONST_ME_FIREAREA = 7,
	CONST_ME_YELLOW_RINGS = 8,
	CONST_ME_GREEN_RINGS = 9,
	CONST_ME_HITAREA = 10,
	CONST_ME_TELEPORT = 11,
	CONST_ME_ENERGYHIT = 12,
	CONST_ME_MAGIC_BLUE = 13,
	CONST_ME_MAGIC_RED = 14,
	CONST_ME_MAGIC_GREEN = 15,
	CONST_ME_HITBYFIRE = 16,
	CONST_ME_HITBYPOISON = 17,
	CONST_ME_MORTAREA = 18,
	CONST_ME_SOUND_GREEN = 19,
	CONST_ME_SOUND_RED = 20,
	CONST_ME_POISONAREA = 21,
	CONST_ME_SOUND_YELLOW = 22,
	CONST_ME_SOUND_PURPLE = 23,
	CONST_ME_SOUND_BLUE = 24,
	CONST_ME_SOUND_WHITE = 25,
	CONST_ME_BUBBLES = 26,
	CONST_ME_CRAPS = 27,
	CONST_ME_GIFT_WRAPS = 28,
	CONST_ME_FIREWORK_YELLOW = 29,
	CONST_ME_FIREWORK_RED = 30,
	CONST_ME_FIREWORK_BLUE = 31,
	CONST_ME_STUN = 32,
	CONST_ME_SLEEP = 33,
	CONST_ME_WATERCREATURE = 34,
	CONST_ME_GROUNDSHAKER = 35,
	CONST_ME_HEARTS = 36,
	CONST_ME_FIREATTACK = 37,
	CONST_ME_ENERGYAREA = 38,
	CONST_ME_SMALLCLOUDS = 39,
	CONST_ME_HOLYDAMAGE = 40,
	CONST_ME_BIGCLOUDS = 41,
	CONST_ME_ICEAREA = 42,
	CONST_ME_ICETORNADO = 43,
	CONST_ME_ICEATTACK = 44,
	CONST_ME_STONES = 45,
	CONST_ME_SMALLPLANTS = 46,
	CONST_ME_CARNIPHILA = 47,
	CONST_ME_PURPLEENERGY = 48,
	CONST_ME_YELLOWENERGY = 49,
	CONST_ME_HOLYAREA = 50,
	CONST_ME_BIGPLANTS = 51,
	CONST_ME_CAKE = 52,
	CONST_ME_GIANTICE = 53,
	CONST_ME_WATERSPLASH = 54,
	CONST_ME_PLANTATTACK = 55,
	CONST_ME_TUTORIALARROW = 56,
	CONST_ME_TUTORIALSQUARE = 57,
	CONST_ME_MIRRORHORIZONTAL = 58,
	CONST_ME_MIRRORVERTICAL = 59,
	CONST_ME_SKULLHORIZONTAL = 60,
	CONST_ME_SKULLVERTICAL = 61,
	CONST_ME_ASSASSIN = 62,
	CONST_ME_STEPSHORIZONTAL = 63,
	CONST_ME_BLOODYSTEPS = 64,
	CONST_ME_STEPSVERTICAL = 65,
	CONST_ME_YALAHARIGHOST = 66,
	CONST_ME_BATS = 67,
	CONST_ME_SMOKE = 68,
	CONST_ME_INSECTS = 69,
	CONST_ME_DRAGONHEAD = 70,
	// New Effect Include
	CONST_ME_ORCSHAMAN = 71,
	CONST_ME_ORCSHAMAN_FIRE = 72,
	CONST_ME_THUNDER = 73,
	CONST_ME_FERUMBRAS = 74,
	CONST_ME_CONFETTI_HORIZONTAL = 75,
	CONST_ME_CONFETTI_VERTICAL = 76,
	// 77-157 are empty
	CONST_ME_BLACKSMOKE = 158,
	// 159-166 are empty
	CONST_ME_REDSMOKE = 167,
	CONST_ME_YELLOWSMOKE = 168,
	CONST_ME_GREENSMOKE = 169,
	CONST_ME_PURPLESMOKE = 170,
	CONST_ME_EARLY_THUNDER = 171,
	CONST_ME_RAGIAZ_BONECAPSULE = 172,
	CONST_ME_CRITICAL_DAMAGE = 173,
	// 174 is empty
	CONST_ME_PLUNGING_FISH = 175,
	CONST_ME_BLUE_ENERGY_SPARK = 176,
	CONST_ME_ORANGE_ENERGY_SPARK = 177,
	CONST_ME_GREEN_ENERGY_SPARK = 178,
	CONST_ME_PINK_ENERGY_SPARK = 179,
	CONST_ME_WHITE_ENERGY_SPARK = 180,
	CONST_ME_YELLOW_ENERGY_SPARK = 181,
	CONST_ME_MAGIC_POWDER = 182,
	// 183 is empty
	CONST_ME_PIXIE_EXPLOSION = 184,
	CONST_ME_PIXIE_COMING = 185,
	CONST_ME_PIXIE_GOING = 186,
	// 187 is empty
	CONST_ME_STORM = 188,
	CONST_ME_STONE_STORM = 189,
	// 190 is empty
	CONST_ME_BLUE_GHOST = 191,
	// 192 is empty
	CONST_ME_PINK_VORTEX = 193,
	CONST_ME_TREASURE_MAP = 194,
	CONST_ME_PINK_BEAM = 195,
	CONST_ME_GREEN_FIREWORKS = 196,
	CONST_ME_ORANGE_FIREWORKS = 197,
	CONST_ME_PINK_FIREWORKS = 198,
	CONST_ME_BLUE_FIREWORKS = 199,
	// 200 is empty
	CONST_ME_SUPREME_CUBE = 201,
	CONST_ME_BLACK_BLOOD = 202,
	CONST_ME_PRISMATIC_SPARK = 203,
	CONST_ME_THAIAN = 204,
	CONST_ME_THAIAN_GHOST = 205,
	CONST_ME_GHOST_SMOKE = 206,
	// 207 is empty
	CONST_ME_WATER_BLOCK_FLOATING = 208,
	CONST_ME_WATER_BLOCK = 209,
	CONST_ME_ROOTS = 210,
	// 211-212 is empty
	CONST_ME_GHOSTLY_SCRATCH = 213,
	CONST_ME_GHOSTLY_BITE = 214,
	CONST_ME_BIG_SCRATCH = 215,
	CONST_ME_SLASH = 216,
	CONST_ME_BITE = 217,
	// 218 empty or debug
	CONST_ME_CHIVALRIOUS_CHALLENGE = 219,
	CONST_ME_DIVINE_DAZZLE = 220,
	CONST_ME_ELECTRICALSPARK = 221,
	CONST_ME_PURPLETELEPORT = 222,
	CONST_ME_REDTELEPORT = 223,
	CONST_ME_ORANGETELEPORT = 224,
	CONST_ME_GREYTELEPORT = 225,
	CONST_ME_LIGHTBLUETELEPORT = 226,
	// 227-229 are empty
	CONST_ME_FATAL = 230,
	CONST_ME_DODGE = 231,
	CONST_ME_HOURGLASS = 232,
	CONST_ME_DAZZLING = 233,
	CONST_ME_SPARKLING = 234,
	CONST_ME_FERUMBRAS_1 = 235,
	CONST_ME_GAZHARAGOTH = 236,
	CONST_ME_MAD_MAGE = 237,
	CONST_ME_HORESTIS = 238,
	CONST_ME_DEVOVORGA = 239,
	CONST_ME_FERUMBRAS_2 = 240,
	CONST_ME_WHITE_SMOKE = 241,
	CONST_ME_WHITE_SMOKES = 242,
	CONST_ME_WATER_DROP = 243,
	CONST_ME_AVATAR_APPEAR = 244,
	CONST_ME_DIVINE_GRENADE = 245,
	CONST_ME_DIVINE_EMPOWERMENT = 246,
	CONST_ME_WATER_FLOATING_THRASH = 247,
	// 248 are empty
	CONST_ME_AGONY = 249,
	// 250-251 are empty
	CONST_ME_LOOT_HIGHLIGHT = 252,

	// 13.40
	CONST_ME_MELTING_CREAM = 263,
	CONST_ME_REAPER = 264,
	CONST_ME_POWERFUL_HEARTS = 265,
	CONST_ME_CREAM = 266,
	CONST_ME_GENTLE_BUBBLE = 267,
	CONST_ME_STARBURST = 268,
	CONST_ME_SIURP = 269,
	CONST_ME_CACAO = 270,
	CONST_ME_CANDY_FLOSS = 271,

	// 15.00
	CONST_ME_GREEN_HITAREA = 272,
	CONST_ME_RED_HITAREA = 273,
	CONST_ME_BLUE_HITAREA = 274,
	CONST_ME_YELLOW_HITAREA = 275,
	CONST_ME_WHITE_FLURRYOFBLOWS = 276,
	CONST_ME_GREEN_FLURRYOFBLOWS = 277,
	CONST_ME_PINK_FLURRYOFBLOWS = 278,
	CONST_ME_WHITE_ENERGYPULSE = 279,
	CONST_ME_GREEN_ENERGYPULSE = 280,
	CONST_ME_PINK_ENERGYPULSE = 281,
	CONST_ME_WHITE_TIGERCLASH = 282,
	CONST_ME_GREEN_TIGERCLASH = 283,
	CONST_ME_PINK_TIGERCLASH = 284,
	CONST_ME_WHITE_EXPLOSIONHIT = 285,
	CONST_ME_GREEN_EXPLOSIONHIT = 286,
	CONST_ME_BLUE_EXPLOSIONHIT = 287,
	CONST_ME_PINK_EXPLOSIONHIT = 288,
	CONST_ME_WHITE_ENERGYSHOCK = 289,
	CONST_ME_GREEN_ENERGYSHOCK = 290,
	CONST_ME_YELLOW_ENERGYSHOCK = 291,

	// 15.10
	CONST_ME_INK_SPLASH = 292,
	CONST_ME_PAPER_PLANE = 293,
	CONST_ME_SPIKES = 294,
	CONST_ME_BLOOD_RAIN = 295,
	CONST_ME_OPEN_BOOKMACHINE = 296,
	CONST_ME_OPEN_BOOKSPELL = 297,
	CONST_ME_SMALL_WHITE_ENERGYSHOCK = 298,
	CONST_ME_SMALL_GREEN_ENERGYSHOCK = 299,
	CONST_ME_SMALL_PINK_ENERGYSHOCK = 300,
	CONST_ME_SMALLWHITE_ENERGY_SPARK = 301,
	CONST_ME_SMALLGREEN_ENERGY_SPARK = 302,
	CONST_ME_SMALLPINK_ENERGY_SPARK = 303,
};

enum ShootType_t : uint16_t
{
	CONST_ANI_NONE,

	CONST_ANI_SPEAR = 1,
	CONST_ANI_BOLT = 2,
	CONST_ANI_ARROW = 3,
	CONST_ANI_FIRE = 4,
	CONST_ANI_ENERGY = 5,
	CONST_ANI_POISONARROW = 6,
	CONST_ANI_BURSTARROW = 7,
	CONST_ANI_THROWINGSTAR = 8,
	CONST_ANI_THROWINGKNIFE = 9,
	CONST_ANI_SMALLSTONE = 10,
	CONST_ANI_DEATH = 11,
	CONST_ANI_LARGEROCK = 12,
	CONST_ANI_SNOWBALL = 13,
	CONST_ANI_POWERBOLT = 14,
	CONST_ANI_POISON = 15,
	CONST_ANI_INFERNALBOLT = 16,
	CONST_ANI_HUNTINGSPEAR = 17,
	CONST_ANI_ENCHANTEDSPEAR = 18,
	CONST_ANI_REDSTAR = 19,
	CONST_ANI_GREENSTAR = 20,
	CONST_ANI_ROYALSPEAR = 21,
	CONST_ANI_SNIPERARROW = 22,
	CONST_ANI_ONYXARROW = 23,
	CONST_ANI_PIERCINGBOLT = 24,
	CONST_ANI_WHIRLWINDSWORD = 25,
	CONST_ANI_WHIRLWINDAXE = 26,
	CONST_ANI_WHIRLWINDCLUB = 27,
	CONST_ANI_ETHEREALSPEAR = 28,
	CONST_ANI_ICE = 29,
	CONST_ANI_EARTH = 30,
	CONST_ANI_HOLY = 31,
	CONST_ANI_SUDDENDEATH = 32,
	CONST_ANI_FLASHARROW = 33,
	CONST_ANI_FLAMMINGARROW = 34,
	CONST_ANI_SHIVERARROW = 35,
	CONST_ANI_ENERGYBALL = 36,
	CONST_ANI_SMALLICE = 37,
	CONST_ANI_SMALLHOLY = 38,
	CONST_ANI_SMALLEARTH = 39,
	CONST_ANI_EARTHARROW = 40,
	CONST_ANI_EXPLOSION = 41,
	CONST_ANI_CAKE = 42,

	// New Distance Include
	CONST_ANI_TARSALARROW = 44,
	CONST_ANI_VORTEXBOLT = 45,
	CONST_ANI_PRISMATICBOLT = 48,
	CONST_ANI_CRYSTALLINEARROW = 49,
	CONST_ANI_DRILLBOLT = 50,
	CONST_ANI_ENVENOMEDARROW = 51,
	CONST_ANI_GLOOTHSPEAR = 53,
	CONST_ANI_SIMPLEARROW = 54,
	CONST_ANI_LEAFSTAR = 56,
	CONST_ANI_DIAMONDARROW = 57,
	CONST_ANI_SPECTRALBOLT = 58,
	CONST_ANI_ROYALSTAR = 59,
	CONST_ANI_CANDYCANE = 61,
	CONST_ANI_CHERRYBOMB = 62,

	// for internal use, don't send to client
	CONST_ANI_WEAPONTYPE = 0xFE, // 254
};

enum SpeakClasses : uint8_t
{
	TALKTYPE_SAY = 1,
	TALKTYPE_WHISPER = 2,
	TALKTYPE_YELL = 3,
	TALKTYPE_PRIVATE_PN = 4,
	TALKTYPE_PRIVATE_NP = 5,
	TALKTYPE_PRIVATE = 6,
	TALKTYPE_CHANNEL_Y = 7,
	TALKTYPE_CHANNEL_W = 8,
	TALKTYPE_RVR_CHANNEL = 9,
	TALKTYPE_RVR_ANSWER = 10,
	TALKTYPE_RVR_CONTINUE = 11,
	TALKTYPE_BROADCAST = 12,
	TALKTYPE_CHANNEL_R1 = 13,  // red - #c text
	TALKTYPE_PRIVATE_RED = 14, //@name@text
	TALKTYPE_CHANNEL_O = 15,   //@name@text
	TALKTYPE_CHANNEL_R2 = 17,  // #d
	TALKTYPE_MONSTER_SAY = 19,
	TALKTYPE_MONSTER_YELL = 20,
};

enum MessageClasses : uint8_t
{
	MESSAGE_STATUS_CONSOLE_RED = 18,    /*Red message in the console*/
	MESSAGE_EVENT_ORANGE = 19,          /*Orange message in the console*/
	MESSAGE_STATUS_CONSOLE_ORANGE = 20, /*Orange message in the console*/
	MESSAGE_STATUS_WARNING = 21,        /*Red message in game window and in the console*/
	MESSAGE_EVENT_ADVANCE = 22,         /*White message in game window and in the console*/
	MESSAGE_EVENT_DEFAULT = 23,         /*White message at the bottom of the game window and in the console*/
	MESSAGE_STATUS_DEFAULT = 24,        /*White message at the bottom of the game window and in the console*/
	MESSAGE_INFO_DESCR = 25,            /*Green message in game window and in the console*/
	MESSAGE_STATUS_SMALL = 26,          /*White message at the bottom of the game window"*/
	MESSAGE_STATUS_CONSOLE_BLUE = 27,   /*FIXME Blue message in the console*/
};

enum FluidColors_t : uint8_t
{
	FLUID_EMPTY,
	FLUID_BLUE,
	FLUID_RED,
	FLUID_BROWN,
	FLUID_GREEN,
	FLUID_YELLOW,
	FLUID_WHITE,
	FLUID_PURPLE,
	FLUID_BLACK,
};

enum FluidTypes_t : uint8_t
{
	FLUID_NONE = FLUID_EMPTY,
	FLUID_WATER = FLUID_BLUE,
	FLUID_BLOOD = FLUID_RED,
	FLUID_BEER = FLUID_BROWN,
	FLUID_SLIME = FLUID_GREEN,
	FLUID_LEMONADE = FLUID_YELLOW,
	FLUID_MILK = FLUID_WHITE,
	FLUID_MANA = FLUID_PURPLE,
	FLUID_INK = FLUID_BLACK,

	FLUID_LIFE = FLUID_RED + 8,
	FLUID_OIL = FLUID_BROWN + 8,
	FLUID_URINE = FLUID_YELLOW + 8,
	FLUID_COCONUTMILK = FLUID_WHITE + 8,
	FLUID_WINE = FLUID_PURPLE + 8,

	FLUID_MUD = FLUID_BROWN + 16,
	FLUID_FRUITJUICE = FLUID_YELLOW + 16,

	FLUID_LAVA = FLUID_RED + 24,
	FLUID_RUM = FLUID_BROWN + 24,
	FLUID_SWAMP = FLUID_GREEN + 24,

	FLUID_TEA = FLUID_BROWN + 32,

	FLUID_MEAD = FLUID_BROWN + 40,
};

const uint8_t reverseFluidMap[] = {
    FLUID_EMPTY, FLUID_WATER, FLUID_MANA,  FLUID_BEER,     FLUID_EMPTY,
    FLUID_BLOOD, FLUID_SLIME, FLUID_EMPTY, FLUID_LEMONADE, FLUID_MILK,
};

const uint8_t clientToServerFluidMap[] = {
    FLUID_EMPTY, FLUID_WATER, FLUID_MANA,       FLUID_BEER,        FLUID_MUD,  FLUID_BLOOD,
    FLUID_SLIME, FLUID_RUM,   FLUID_LEMONADE,   FLUID_MILK,        FLUID_WINE, FLUID_LIFE,
    FLUID_URINE, FLUID_OIL,   FLUID_FRUITJUICE, FLUID_COCONUTMILK, FLUID_TEA,  FLUID_MEAD,
};

enum ClientFluidTypes_t : uint8_t
{
	CLIENTFLUID_EMPTY = 0,
	CLIENTFLUID_BLUE = 1,
	CLIENTFLUID_PURPLE = 2,
	CLIENTFLUID_BROWN_1 = 3,
	CLIENTFLUID_BROWN_2 = 4,
	CLIENTFLUID_RED = 5,
	CLIENTFLUID_GREEN = 6,
	CLIENTFLUID_BROWN = 7,
	CLIENTFLUID_YELLOW = 8,
	CLIENTFLUID_WHITE = 9,
};

const uint8_t fluidMap[] = {
    CLIENTFLUID_EMPTY, CLIENTFLUID_BLUE,   CLIENTFLUID_RED,   CLIENTFLUID_BROWN_1,
    CLIENTFLUID_GREEN, CLIENTFLUID_YELLOW, CLIENTFLUID_WHITE, CLIENTFLUID_PURPLE,
};

enum SquareColor_t : uint8_t
{
	SQ_COLOR_YELLOW = 210,
};

enum TextColor_t : uint8_t
{
	TEXTCOLOR_BLACK = 0,
	TEXTCOLOR_BLUE = 5,
	TEXTCOLOR_GREEN = 18,
	TEXTCOLOR_LIGHTGREEN = 66,
	TEXTCOLOR_DARKBROWN = 78,
	TEXTCOLOR_DARKGREY = 86,
	TEXTCOLOR_LIGHTBLUE = 89,
	TEXTCOLOR_MAYABLUE = 95,
	TEXTCOLOR_DARKRED = 108,
	TEXTCOLOR_DARKPURPLE = 112,
	TEXTCOLOR_BROWN = 120,
	TEXTCOLOR_GREY = 129,
	TEXTCOLOR_TEAL = 143,
	TEXTCOLOR_DARKPINK = 152,
	TEXTCOLOR_PURPLE = 154,
	TEXTCOLOR_DARKORANGE = 156,
	TEXTCOLOR_LIGHTORANGE = 158,
	TEXTCOLOR_RED = 180,
	TEXTCOLOR_PINK = 190,
	TEXTCOLOR_ORANGE = 192,
	TEXTCOLOR_DARKYELLOW = 205,
	TEXTCOLOR_YELLOW = 210,
	TEXTCOLOR_WHITE = 215,
	TEXTCOLOR_NONE = 255,
};

enum Icons_t
{
	ICON_POISON = 1 << 0,
	ICON_BURN = 1 << 1,
	ICON_ENERGY = 1 << 2,
	ICON_DRUNK = 1 << 3,
	ICON_MANASHIELD = 1 << 4,
	ICON_PARALYZE = 1 << 5,
	ICON_HASTE = 1 << 6,
	ICON_SWORDS = 1 << 7,
	ICON_DROWNING = 1 << 8,
	ICON_FREEZING = 1 << 9,
	ICON_DAZZLED = 1 << 10,
	ICON_CURSED = 1 << 11,
	ICON_PARTY_BUFF = 1 << 12,
	ICON_REDSWORDS = 1 << 13,
	ICON_PIGEON = 1 << 14,
	ICON_BLEEDING = 1 << 15,
};

enum PlayerIcon_t : uint64_t
{
	PlayerIcon_Poison        = 1ULL << 0,
	PlayerIcon_Burn          = 1ULL << 1,
	PlayerIcon_Energy        = 1ULL << 2,
	PlayerIcon_Drunk         = 1ULL << 3,
	PlayerIcon_ManaShield    = 1ULL << 4,
	PlayerIcon_Paralyze      = 1ULL << 5,
	PlayerIcon_Haste         = 1ULL << 6,
	PlayerIcon_Swords        = 1ULL << 7,
	PlayerIcon_Drowning      = 1ULL << 8,
	PlayerIcon_Freezing      = 1ULL << 9,
	PlayerIcon_Dazzled       = 1ULL << 10,
	PlayerIcon_Cursed        = 1ULL << 11,
	PlayerIcon_PartyBuff     = 1ULL << 12,
	PlayerIcon_RedSwords     = 1ULL << 13,
	PlayerIcon_Pigeon        = 1ULL << 14,
	PlayerIcon_Bleeding      = 1ULL << 15,
	PlayerIcon_LesserHex     = 1ULL << 16,
	PlayerIcon_IntenseHex    = 1ULL << 17,
	PlayerIcon_GreaterHex    = 1ULL << 18,
	PlayerIcon_Rooted        = 1ULL << 19,
	PlayerIcon_Feared        = 1ULL << 20,
	PlayerIcon_GoshnarTaint1 = 1ULL << 21,
	PlayerIcon_GoshnarTaint2 = 1ULL << 22,
	PlayerIcon_GoshnarTaint3 = 1ULL << 23,
	PlayerIcon_GoshnarTaint4 = 1ULL << 24,
	PlayerIcon_GoshnarTaint5 = 1ULL << 25,
	PlayerIcon_NewManaShield = 1ULL << 26,
	PlayerIcon_Agony         = 1ULL << 27,
	PlayerIcon_Powerless     = 1ULL << 28,
	PlayerIcon_MentorOther   = 1ULL << 29,
};

enum IconBakragore_t : uint8_t
{
	IconBakragore_None  = 0,
	IconBakragore_Taint1 = 1,
	IconBakragore_Taint2 = 2,
	IconBakragore_Taint3 = 3,
	IconBakragore_Taint4 = 4,
	IconBakragore_Taint5 = 5,
	IconBakragore_Taint6 = 6,
	IconBakragore_Taint7 = 7,
	IconBakragore_Taint8 = 8,
	IconBakragore_Taint9 = 9,
};

enum CreatureIconCategory_t : uint8_t
{
	CreatureIconCategory_Quests = 0,
	CreatureIconCategory_Modifications = 1,
};

enum CreatureIconModifications_t : uint8_t
{
	CreatureIconModifications_None = 0,
	CreatureIconModifications_HigherDamageReceived,
	CreatureIconModifications_LowerDamageDealt,
	CreatureIconModifications_TurnedMelee,
	CreatureIconModifications_Influenced,
	CreatureIconModifications_Fiendish,
	CreatureIconModifications_ReducedHealth,
	CreatureIconModifications_ReducedHealthExclamation,
};

enum CreatureIconQuests_t : uint8_t
{
	CreatureIconQuests_None = 0,
	CreatureIconQuests_WhiteCross,
	CreatureIconQuests_RedCross,
	CreatureIconQuests_RedBall,
	CreatureIconQuests_GreenBall,
	CreatureIconQuests_RedGreenBall,
	CreatureIconQuests_GreenShield,
	CreatureIconQuests_YellowShield,
	CreatureIconQuests_BlueShield,
	CreatureIconQuests_PurpleShield,
	CreatureIconQuests_RedShield,
	CreatureIconQuests_Dove,
	CreatureIconQuests_Energy,
	CreatureIconQuests_Earth,
	CreatureIconQuests_Water,
	CreatureIconQuests_Fire,
	CreatureIconQuests_Ice,
	CreatureIconQuests_ArrowUp,
	CreatureIconQuests_ArrowDown,
	CreatureIconQuests_ExclamationMark,
	CreatureIconQuests_QuestionMark,
	CreatureIconQuests_CancelMark,
	CreatureIconQuests_Hazard,
	CreatureIconQuests_BrownSkull,
	CreatureIconQuests_BloodDrop,
	CreatureIconQuests_Familiar,
};

struct CreatureIcon
{
	CreatureIcon() = default;

	explicit CreatureIcon(CreatureIconModifications_t mod, uint16_t count = 0) :
		category(CreatureIconCategory_Modifications), modification(mod), count(count) {}

	explicit CreatureIcon(CreatureIconQuests_t q, uint16_t count = 0) :
		category(CreatureIconCategory_Quests), quest(q), count(count) {}

	CreatureIconCategory_t category = CreatureIconCategory_Quests;
	CreatureIconModifications_t modification = CreatureIconModifications_None;
	CreatureIconQuests_t quest = CreatureIconQuests_None;
	uint16_t count = 0;

	bool isNone() const {
		return modification == CreatureIconModifications_None && quest == CreatureIconQuests_None;
	}

	bool isSet() const {
		return !isNone();
	}

	uint8_t serialize() const {
		if (category == CreatureIconCategory_Modifications) {
			return static_cast<uint8_t>(modification);
		}
		return static_cast<uint8_t>(quest);
	}
};

enum WeaponType_t : uint8_t
{
	WEAPON_NONE,
	WEAPON_SWORD,
	WEAPON_CLUB,
	WEAPON_AXE,
	WEAPON_SHIELD,
	WEAPON_DISTANCE,
	WEAPON_WAND,
	WEAPON_AMMO,
	WEAPON_QUIVER,
	WEAPON_FIST,
};

enum Ammo_t : uint8_t
{
	AMMO_NONE,
	AMMO_BOLT,
	AMMO_ARROW,
	AMMO_SPEAR,
	AMMO_THROWINGSTAR,
	AMMO_THROWINGKNIFE,
	AMMO_STONE,
	AMMO_SNOWBALL,
};

enum WeaponAction_t : uint8_t
{
	WEAPONACTION_NONE,
	WEAPONACTION_REMOVECOUNT,
	WEAPONACTION_REMOVECHARGE,
	WEAPONACTION_MOVE,
};

enum WieldInfo_t
{
	WIELDINFO_NONE = 0 << 0,
	WIELDINFO_LEVEL = 1 << 0,
	WIELDINFO_MAGLV = 1 << 1,
	WIELDINFO_VOCREQ = 1 << 2,
	WIELDINFO_PREMIUM = 1 << 3,
	WIELDINFO_RESETS = 1 << 4,
};

enum Skulls_t : uint8_t
{
	SKULL_NONE = 0,
	SKULL_YELLOW = 1,
	SKULL_GREEN = 2,
	SKULL_WHITE = 3,
	SKULL_RED = 4,
	SKULL_BLACK = 5
};

enum PartyShields_t : uint8_t
{
	SHIELD_NONE = 0,
	SHIELD_WHITEYELLOW = 1,
	SHIELD_WHITEBLUE = 2,
	SHIELD_BLUE = 3,
	SHIELD_YELLOW = 4,
	SHIELD_BLUE_SHAREDEXP = 5,
	SHIELD_YELLOW_SHAREDEXP = 6,
	SHIELD_BLUE_NOSHAREDEXP_BLINK = 7,
	SHIELD_YELLOW_NOSHAREDEXP_BLINK = 8,
	SHIELD_BLUE_NOSHAREDEXP = 9,
	SHIELD_YELLOW_NOSHAREDEXP = 10,
};

enum GuildEmblems_t : uint8_t
{
	GUILDEMBLEM_NONE = 0,
	GUILDEMBLEM_ALLY = 1,
	GUILDEMBLEM_ENEMY = 2,
	GUILDEMBLEM_NEUTRAL = 3,
	GUILDEMBLEM_MEMBER = 4,
	GUILDEMBLEM_OTHER = 5
};

enum InspectObjectTypes_t : uint8_t
{
	INSPECT_NORMALOBJECT = 0,
	INSPECT_NPCTRADE = 1,
	INSPECT_PLAYERTRADE = 2,
	INSPECT_CYCLOPEDIA = 3,
	INSPECT_PROFICIENCY = 4
};

enum item_t : uint16_t
{
	ITEM_FIREFIELD_PVP_FULL = 2118,
	ITEM_FIREFIELD_PVP_MEDIUM = 2119,
	ITEM_FIREFIELD_PVP_SMALL = 2120,
	ITEM_FIREFIELD_PERSISTENT_FULL = 2123,
	ITEM_FIREFIELD_PERSISTENT_MEDIUM = 2124,
	ITEM_FIREFIELD_PERSISTENT_SMALL = 2125,
	ITEM_FIREFIELD_NOPVP = 21465,

	ITEM_POISONFIELD_PVP = 105,
	ITEM_POISONFIELD_PERSISTENT = 2121,
	ITEM_POISONFIELD_NOPVP = 2134,

	ITEM_ENERGYFIELD_PVP = 2122,
	ITEM_ENERGYFIELD_PERSISTENT = 2126,
	ITEM_ENERGYFIELD_NOPVP = 2135,

	ITEM_MAGICWALL = 2129,
	ITEM_MAGICWALL_PERSISTENT = 2128,
	ITEM_MAGICWALL_SAFE = 10181,
	ITEM_MAGICWALL_NOPVP = 20669,

	ITEM_WILDGROWTH = 2130,
	ITEM_WILDGROWTH_PERSISTENT = 3635,
	ITEM_WILDGROWTH_SAFE = 10182,
	ITEM_WILDGROWTH_NOPVP = 20670,

	ITEM_BAG = 2853,
	ITEM_BACKPACK = 21411,

	ITEM_GOLD_COIN = 3031,
	ITEM_PLATINUM_COIN = 3035,
	ITEM_CRYSTAL_COIN = 3043,
	ITEM_GOLD_NUGGET = 32724,

	ITEM_DEPOT = 3502,
	ITEM_LOCKER = 3497,
	ITEM_INBOX = 12902,
	ITEM_MARKET = 12903,
	ITEM_STORE_INBOX = 23396,
	ITEM_SUPPLY_STASH = 28750,
	ITEM_DEPOT_BOX_1 = 22797,
	ITEM_DEPOT_BOX_17 = 22813,

	ITEM_MALE_CORPSE = 4240,
	ITEM_FEMALE_CORPSE = 4247,

	ITEM_FULLSPLASH = 2886,
	ITEM_SMALLSPLASH = 2889,

	ITEM_PARCEL = 3503,
	ITEM_LETTER = 3505,
	ITEM_LETTER_STAMPED = 3506,
	ITEM_LABEL = 3507,

	ITEM_AMULETOFLOSS = 3057,

	ITEM_REWARD_CHEST = 19250,
	ITEM_REWARD_CONTAINER = 19202,

	ITEM_GOLD_POUCH = 23721,

	ITEM_DOCUMENT_RO = 2834, // read-only
	ITEM_RECEIPT_SUCCESS = 21932,
	ITEM_RECEIPT_FAIL = 21933,
};

enum AvatarLookType_t : uint16_t
{
	AVATAR_LOOKTYPE_STEEL = 1593,    // Knight - Avatar of Steel
	AVATAR_LOOKTYPE_LIGHT = 1594,    // Paladin - Avatar of Light
	AVATAR_LOOKTYPE_STORM = 1595,    // Sorcerer - Avatar of Storm
	AVATAR_LOOKTYPE_NATURE = 1596,   // Druid - Avatar of Nature
	AVATAR_LOOKTYPE_BALANCE = 1823,  // Monk - Avatar of Balance
};

enum PlayerFlags : uint64_t
{
	PlayerFlag_CannotUseCombat = 1 << 0,
	PlayerFlag_CannotAttackPlayer = 1 << 1,
	PlayerFlag_CannotAttackMonster = 1 << 2,
	PlayerFlag_CannotBeAttacked = 1 << 3,
	PlayerFlag_CanConvinceAll = 1 << 4,
	PlayerFlag_CanSummonAll = 1 << 5,
	PlayerFlag_CanIllusionAll = 1 << 6,
	PlayerFlag_CanSenseInvisibility = 1 << 7,
	PlayerFlag_IgnoredByMonsters = 1 << 8,
	PlayerFlag_NotGainInFight = 1 << 9,
	PlayerFlag_HasInfiniteMana = 1 << 10,
	PlayerFlag_HasInfiniteSoul = 1 << 11,
	PlayerFlag_HasNoExhaustion = 1 << 12,
	PlayerFlag_CannotUseSpells = 1 << 13,
	PlayerFlag_CannotPickupItem = 1 << 14,
	PlayerFlag_CanAlwaysLogin = 1 << 15,
	PlayerFlag_CanBroadcast = 1 << 16,
	PlayerFlag_CanEditHouses = 1 << 17,
	PlayerFlag_CannotBeBanned = 1 << 18,
	PlayerFlag_CannotBePushed = 1 << 19,
	PlayerFlag_HasInfiniteCapacity = 1 << 20,
	PlayerFlag_CanPushAllCreatures = 1 << 21,
	PlayerFlag_CanTalkRedPrivate = 1 << 22,
	PlayerFlag_CanTalkRedChannel = 1 << 23,
	PlayerFlag_TalkOrangeHelpChannel = 1 << 24,
	PlayerFlag_NotGainExperience = 1 << 25,
	PlayerFlag_NotGainMana = 1 << 26,
	PlayerFlag_NotGainHealth = 1 << 27,
	PlayerFlag_NotGainSkill = 1 << 28,
	PlayerFlag_SetMaxSpeed = 1 << 29,
	PlayerFlag_SpecialVIP = 1 << 30,
	PlayerFlag_NotGenerateLoot = static_cast<uint64_t>(1) << 31,
	PlayerFlag_CanTalkRedChannelAnonymous = static_cast<uint64_t>(1) << 32,
	PlayerFlag_IgnoreProtectionZone = static_cast<uint64_t>(1) << 33,
	PlayerFlag_IgnoreSpellCheck = static_cast<uint64_t>(1) << 34,
	PlayerFlag_IgnoreWeaponCheck = static_cast<uint64_t>(1) << 35,
	PlayerFlag_CannotBeMuted = static_cast<uint64_t>(1) << 36,
	PlayerFlag_IsAlwaysPremium = static_cast<uint64_t>(1) << 37,
	PlayerFlag_IgnoreYellCheck = static_cast<uint64_t>(1) << 38,
	PlayerFlag_IgnoreSendPrivateCheck = static_cast<uint64_t>(1) << 39,
	PlayerFlag_CanThrowFar = static_cast<uint64_t>(1) << 40,
};

enum Exhaust_t : uint32_t
{
	EXHAUST_TALKACTION = 14,
	EXHAUST_USEITEM = 20,
	EXHAUST_OPENCONTAINER = 21,
	EXHAUST_POTION = 30,
	EXHAUST_RUNE = 32,
	EXHAUST_MOVEITEM = 40,
};

enum ReloadTypes_t : uint8_t
{
	RELOAD_TYPE_ALL,
	RELOAD_TYPE_ACTIONS,
	RELOAD_TYPE_CHAT,
	RELOAD_TYPE_CONFIG,
	RELOAD_TYPE_CREATURESCRIPTS,
	RELOAD_TYPE_EVENTS,
	RELOAD_TYPE_GLOBAL,
	RELOAD_TYPE_GLOBALEVENTS,
	RELOAD_TYPE_ITEMS,
	RELOAD_TYPE_MONSTERS,
	RELOAD_TYPE_OUTFITS,
	RELOAD_TYPE_MOUNTS,
	RELOAD_TYPE_MOVEMENTS,
	RELOAD_TYPE_NPCS,
	RELOAD_TYPE_QUESTS,
	RELOAD_TYPE_RAIDS,
	RELOAD_TYPE_SCRIPTS,
	RELOAD_TYPE_SPELLS,
	RELOAD_TYPE_TALKACTIONS,
	RELOAD_TYPE_WEAPONS,
};

// OTCv8 features (from src/client/const.h)
enum class GameFeature : uint8_t {
	ProtocolChecksum = 1,
	AccountNames = 2,
	ChallengeOnLogin = 3,
	PenalityOnDeath = 4,
	NameOnNpcTrade = 5,
	DoubleFreeCapacity = 6,
	DoubleExperience = 7,
	TotalCapacity = 8,
	SkillsBase = 9,
	PlayerRegenerationTime = 10,
	ChannelPlayerList = 11,
	PlayerMounts = 12,
	EnvironmentEffect = 13,
	CreatureEmblems = 14,
	ItemAnimationPhase = 15,
	MagicEffectU16 = 16,
	PlayerMarket = 17,
	SpritesU32 = 18,
	TileAddThingWithStackpos = 19,
	OfflineTrainingTime = 20,
	PurseSlot = 21,
	FormatCreatureName = 22,
	SpellList = 23,
	ClientPing = 24,
	ExtendedClientPing = 25,
	DoubleHealth = 28,
	DoubleSkills = 29,
	ChangeMapAwareRange = 30,
	MapMovePosition = 31,
	AttackSeq = 32,
	BlueNpcNameColor = 33,
	DiagonalAnimatedText = 34,
	LoginPending = 35,
	NewSpeedLaw = 36,
	ForceFirstAutoWalkStep = 37,
	MinimapRemove = 38,
	DoubleShopSellAmount = 39,
	ContainerPagination = 40,
	ThingMarks = 41,
	LooktypeU16 = 42,
	PlayerStamina = 43,
	PlayerAddons = 44,
	MessageStatements = 45,
	MessageLevel = 46,
	NewFluids = 47,
	PlayerStateU16 = 48,
	NewOutfitProtocol = 49,
	PVPMode = 50,
	WritableDate = 51,
	AdditionalVipInfo = 52,
	BaseSkillU16 = 53,
	CreatureIcons = 54,
	HideNpcNames = 55,
	SpritesAlphaChannel = 56,
	PremiumExpiration = 57,
	BrowseField = 58,
	EnhancedAnimations = 59,
	OGLInformation = 60,
	MessageSizeCheck = 61,
	PreviewState = 62,
	LoginPacketEncryption = 63,
	ClientVersion = 64,
	ContentRevision = 65,
	ExperienceBonus = 66,
	Authenticator = 67,
	UnjustifiedPoints = 68,
	SessionKey = 69,
	DeathType = 70,
	IdleAnimations = 71,
	KeepUnawareTiles = 72,
	IngameStore = 73,
	IngameStoreHighlights = 74,
	IngameStoreServiceType = 75,
	AdditionalSkills = 76,
	DistanceEffectU16 = 77,
	Prey = 78,
	DoubleMagicLevel = 79,

	ExtendedOpcode = 80,
	MinimapLimitedToSingleFloor = 81,
	SendWorldName = 82,

	DoubleLevel = 83,
	DoubleSoul = 84,
	DoublePlayerGoodsMoney = 85,
	CreatureWalkthrough = 86,
	DoubleTradeMoney = 87,
	SequencedPackets = 88,
	Tibia12Protocol = 89,

	// 90-99 otclientv8 features
	NewWalking = 90,
	SlowerManualWalking = 91,

	ItemTooltip = 93,

	Bot = 95,
	BiggerMapCache = 96,
	ForceLight = 97,
	NoDebug = 98,
	BotProtection = 99,

	// Custom features for customer
	FasterAnimations = 101,
	CenteredOutfits = 102,
	SendIdentifiers = 103,
	WingsAndAura = 104,
	PlayerStateU32 = 105,
	OutfitShaders = 106,

	// advanced features
	PacketSizeU32 = 110,
	PacketCompression = 111,

	QuickLootFlags = 123,
	DisplayItemDuration = 129,
	ThingUpgradeClassification = 130,
	ItemTierByte = 131,
	AstraCreatureIcons = 133,
	PlayerFamiliars = 138,
	DisplayItemCharges = 139,
	PackedPlayerInventory = 140,
	AstraQuiverCountU16 = 141,
	AstraOutfitStoreMode = 142,
	AstraItemMetadata = 143,

	Last = 143
};

inline constexpr int32_t CHANNEL_GUILD = 0x00;
inline constexpr int32_t CHANNEL_PARTY = 0x01;
inline constexpr int32_t CHANNEL_PRIVATE = 0xFFFF;
inline constexpr int32_t CHANNEL_CAST = 0xFF;

// Guild rank level constants
enum GuildLevel_t : uint8_t
{
	GUILDLEVEL_MEMBER = 1,
	GUILDLEVEL_VICE = 2,
	GUILDLEVEL_LEADER = 3,
};

// Forge storage constants
inline constexpr int32_t FORGE_DUST_STORAGE = 10000;
inline constexpr int32_t FORGE_DUST_LIMIT_STORAGE = 10001;

#endif
