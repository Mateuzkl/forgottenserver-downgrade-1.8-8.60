if(NOT DEFINED PROJECT_SOURCE_DIR)
    message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()

file(READ "${PROJECT_SOURCE_DIR}/src/protocolgame.cpp" protocolgame_source)
file(READ "${PROJECT_SOURCE_DIR}/src/const.h" const_source)

function(extract_block source_text start_marker end_marker output_var)
    string(FIND "${source_text}" "${start_marker}" block_start)
    string(FIND "${source_text}" "${end_marker}" block_end)
    if(block_start EQUAL -1 OR block_end EQUAL -1 OR block_end LESS_EQUAL block_start)
        message(FATAL_ERROR "Unable to locate block: ${start_marker}")
    endif()
    math(EXPR block_length "${block_end} - ${block_start}")
    string(SUBSTRING "${source_text}" ${block_start} ${block_length} block)
    set(${output_var} "${block}" PARENT_SCOPE)
endfunction()

function(require_occurrences text needle expected context)
    string(REGEX MATCHALL "${needle}" matches "${text}")
    list(LENGTH matches count)
    if(NOT count EQUAL expected)
        message(FATAL_ERROR "${context}: expected ${expected} occurrence(s) of ${needle}, found ${count}")
    endif()
endfunction()

# 1. Check GameFeature::AstraShopCountU16 in const.h
require_occurrences("${const_source}" "AstraShopCountU16 = 148" 1 "const.h AstraShopCountU16 definition")

# 2. Check ProtocolGame::sendShop block
extract_block("${protocolgame_source}" "void ProtocolGame::sendShop" "void ProtocolGame::sendCloseShop" shop_block)
require_occurrences("${shop_block}" "if \\(isAstraClient\\)" 1 "sendShop isAstraClient branch")
require_occurrences("${shop_block}" "msg\\.add<uint16_t>\\(itemsToSend\\)" 1 "sendShop AstraClient uint16 count")
require_occurrences("${shop_block}" "msg\\.addByte\\(itemsToSend\\)" 1 "sendShop standard client uint8 count")
require_occurrences("${shop_block}" "written < itemsToSend" 2 "sendShop bounded loop invariant")

# 3. Check ProtocolGame::sendFeatures block
extract_block("${protocolgame_source}" "void ProtocolGame::sendFeatures" "void ProtocolGame::spectatorTurn" features_block)
require_occurrences("${features_block}" "GameFeature::AstraShopCountU16" 1 "sendFeatures AstraShopCountU16 negotiation")

# 4. Check ProtocolGame::sendMonsterPodiumWindow trailing serialization statements
extract_block("${protocolgame_source}" "void ProtocolGame::sendMonsterPodiumWindow" "void ProtocolGame::sendUpdatedVIPStatus" podium_block)
string(FIND "${podium_block}" "msg.addByte(static_cast<uint8_t>(getAttribute(\"LookDirection\"" dir_pos)
string(FIND "${podium_block}" "msg.addByte(static_cast<uint8_t>(getAttribute(\"PodiumVisible\"" pod_pos)
string(FIND "${podium_block}" "msg.addByte(static_cast<uint8_t>(getAttribute(\"MonsterVisible\"" mon_pos)

if(dir_pos EQUAL -1 OR pod_pos EQUAL -1 OR mon_pos EQUAL -1)
    message(FATAL_ERROR "sendMonsterPodiumWindow missing trailing msg.addByte serialization statements")
endif()

if(dir_pos GREATER pod_pos OR pod_pos GREATER mon_pos)
    message(FATAL_ERROR "sendMonsterPodiumWindow trailing serialization order must be: LookDirection, PodiumVisible, MonsterVisible")
endif()

message(STATUS "ProtocolGame shop and monster podium invariants verified successfully.")
