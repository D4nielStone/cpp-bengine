-- Shared game logic for the FPP demo.

local orb = { active = true, collected = false }

function setup()
    bubble.log("FPP demo started.")
end

function update()
    if orb.collected then
        return
    end

    if bubble.entity.distance_to_player() < 1.5 then
        orb.collected = true
        bubble.log("Pickup collected.")
    end
end
