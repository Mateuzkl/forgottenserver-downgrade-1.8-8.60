local action = Action()

local foods = {
	[3250] = {5, "Crunch."}, -- carrot
	[3577] = {15, "Munch."}, -- meat
	[3578] = {12, "Munch."}, -- fish
	[3579] = {10, "Mmmm."}, -- salmon
	[3580] = {17, "Munch."}, -- northern pike
	[3581] = {4, "Gulp."}, -- shrimp
	[3582] = {30, "Chomp."}, -- ham
	[3583] = {60, "Chomp."}, -- dragon ham
	[3584] = {5, "Yum."}, -- pear
	[3585] = {6, "Yum."}, -- red apple
	[3586] = {13, "Yum."}, -- orange
	[3587] = {8, "Yum."}, -- banana
	[3588] = {1, "Yum."}, -- blueberry
	[3589] = {18, "Slurp."}, -- coconut
	[3590] = {1, "Yum."}, -- cherry
	[3591] = {2, "Yum."}, -- strawberry
	[3592] = {9, "Yum."}, -- grapes
	[3593] = {20, "Yum."}, -- melon
	[3594] = {17, "Munch."}, -- pumpkin
	[3595] = {5, "Crunch."}, -- carrot
	[3596] = {6, "Munch."}, -- tomato
	[3597] = {9, "Crunch."}, -- corncob
	[3598] = {2, "Crunch."}, -- cookie
	[3599] = {2, "Munch."}, -- candy cane
	[3600] = {10, "Crunch."}, -- bread
	[3601] = {3, "Crunch."}, -- roll
	[3602] = {8, "Crunch."}, -- brown bread
	[3606] = {6, "Gulp."}, -- egg
	[3607] = {9, "Smack."}, -- cheese
	[3723] = {9, "Munch."}, -- white mushroom
	[3724] = {4, "Munch."}, -- red mushroom
	[3725] = {22, "Munch."}, -- brown mushroom
	[3726] = {30, "Munch."}, -- orange mushroom
	[3727] = {9, "Munch."}, -- wood mushroom
	[3728] = {6, "Munch."}, -- dark mushroom
	[3729] = {12, "Munch."}, -- some mushrooms
	[3730] = {3, "Munch."}, -- some mushrooms
	[3731] = {36, "Munch."}, -- fire mushroom
	[3732] = {5, "Munch."}, -- green mushroom
	[5096] = {4, "Yum."}, -- mango
	[5678] = {8, "Gulp."}, -- tortoise egg
	[6125] = {8, "Gulp."}, -- tortoise egg from Nargor
	[6277] = {10, "Mmmm."}, -- cake
	[6278] = {15, "Mmmm."}, -- decorated cake
	[6392] = {12, "Mmmm."}, -- valentine's cake
	[6393] = {15, "Mmmm."}, -- cream cake
	[6500] = {20, "Mmmm."}, -- gingerbread man
	[6541] = {6, "Gulp."}, -- coloured egg (yellow)
	[6542] = {6, "Gulp."}, -- coloured egg (red)
	[6543] = {6, "Gulp."}, -- coloured egg (blue)
	[6544] = {6, "Gulp."}, -- coloured egg (green)
	[6545] = {6, "Gulp."}, -- coloured egg (purple)
	[6569] = {1, "Mmmm."}, -- candy
	[6574] = {5, "Mmmm."}, -- bar of chocolate
	[7158] = {15, "Munch."}, -- rainbow trout
	[7159] = {13, "Munch."}, -- green perch
	[229] = {2, "Yum."}, -- ice cream cone (crispy chocolate chips)
	[7373] = {2, "Yum."}, -- ice cream cone (velvet vanilla)
	[7374] = {2, "Yum."}, -- ice cream cone (sweet strawberry)
	[7375] = {2, "Yum."}, -- ice cream cone (chilly cherry)
	[7376] = {2, "Yum."}, -- ice cream cone (mellow melon)
	[7377] = {2, "Yum."}, -- ice cream cone (blue-barian)
	[836] = {4, "Crunch."}, -- walnut
	[841] = {4, "Crunch."}, -- peanut
	[901] = {60, "Munch."}, -- marlin
	[169] = {9, "Urgh."}, -- scarab cheese
	[8010] = {10, "Gulp."}, -- potato
	[8011] = {5, "Yum."}, -- plum
	[8012] = {1, "Yum."}, -- raspberry
	[8013] = {1, "Urgh."}, -- lemon
	[8014] = {7, "Munch."}, -- cucumber
	[8015] = {5, "Crunch."}, -- onion
	[8016] = {1, "Gulp."}, -- jalapeño pepper
	[8017] = {5, "Munch."}, -- beetroot
	[8019] = {11, "Yum."}, -- chocolate cake
	[8177] = {7, "Slurp."}, -- yummy gummy worm
	[8197] = {5, "Crunch."}, -- bulb of garlic
	[9083] = {0, "Slurp."}, -- banana chocolate shake
	[9537] = {0, "Your head begins to feel better."}, -- headache pill
	[10329] = {15, "Yum."}, -- rice ball
	[10453] = {3, "Urgh."}, -- terramite eggs
	[10219] = {10, "Mmmm."}, -- crocodile steak
	[11459] = {20, "Yum."}, -- pineapple
	[11460] = {10, "Munch."}, -- aubergine
	[11461] = {8, "Crunch."}, -- broccoli
	[11462] = {9, "Crunch."}, -- cauliflower
	[11681] = {55, "Gulp."}, -- ectoplasmic sushi
	[11682] = {18, "Yum."}, -- dragonfruit
	[11683] = {2, "Munch."} -- peas
}

function action.onUse(player, item, fromPosition, target, toPosition, isHotkey)
	local food = foods[item.itemid]
	if not food then return false end

	local condition = player:getCondition(CONDITION_REGENERATION,
	                                      CONDITIONID_DEFAULT)
	if condition and math.floor(condition:getTicks() / 1000 + (food[1] * 12)) >=
		1200 then
		player:sendTextMessage(MESSAGE_STATUS_SMALL, "You are full.")
	else
		player:feed(food[1] * 12)
		player:say(food[2], TALKTYPE_MONSTER_SAY)
		item:remove(1)
	end
	return true
end

action:id(3250, 3577, 3578, 3579, 3580, 3581, 3582, 3583, 3584, 3585, 3586, 3587, 3588, 3589, 3590, 3591, 3592, 3593, 3594, 3595, 3596, 3597, 3598, 3599, 3600, 3601, 3602, 3606, 3607, 3723, 3724, 3725, 3726, 3727, 3728, 3729, 3730, 3731, 3732, 5096, 5678, 6125, 6277, 6278, 6392, 6393, 6500, 6541, 6542, 6543, 6544, 6545, 6569, 6574, 7158, 7159, 229, 7373, 7374, 7375, 7376, 7377, 836, 841, 901, 169, 8010, 8011, 8012, 8013, 8014, 8015, 8016, 8017, 8019, 8177, 8197, 9083, 9537, 10329, 10453, 10219, 11459, 11460, 11461, 11462, 11681, 11682, 11683)
action:register()
