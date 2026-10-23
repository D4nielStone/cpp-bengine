-- Shared game logic for the FPP demo.

local orb = { active = true, collected = false }
local player_id = 100

function setup()
    bubble.log("FPP demo started.")
end

function update()
    if orb.collected then
        return
    end

    local px, py, pz = bubble.entity.get_position(player_id)
    local ox, oy, oz = bubble.transform.get_position()
    local dx = px - ox
    local dy = py - oy
    local dz = pz - oz
    if dx * dx + dy * dy + dz * dz < 3.24 then
        orb.collected = true
        bubble.entity.set_visible(bubble.meuID, false)
        bubble.entity.remove_physics(bubble.meuID)
        bubble.log("Pickup collected.")
    end
end
