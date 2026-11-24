#include "zelda_debug.h"

/* The Majora's Mask scene list does not describe Mega Man 64 rooms.
 * Research builds warp from the pad menu (L + D-pad Up). */

std::vector<zelda64::AreaWarps> zelda64::game_warps {
    { "Research build", {
        {
            0, "L + D-pad Up", {
                "In game, not this menu",
            }
        },
    } },
};
