-- Shared game logic for the FPP demo.
local collected = false
local player

function setup()
    player = GameObject.Find(106) -- PlayerCapsule in main.scene
    Debug.Log("FPP demo started.")
end

function update()
    if collected or not player or not player.transform then return end
    local position = player.transform.position
    local pickup = transform.position
    local dx, dy, dz = position.x - pickup.x, position.y - pickup.y, position.z - pickup.z
    if dx * dx + dy * dy + dz * dz < 3.24 then
        collected = true
        local visual = gameObject:GetComponent("Renderer")
        if visual then visual.enabled = false end
        local body = gameObject:GetComponent("Rigidbody")
        if body then Destroy(body) end
        Debug.Log("Pickup collected.")
    end
end
