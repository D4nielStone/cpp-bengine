local collected = false
local player

function setup()
    player = GameObject.Find("PlayerCapsule")
end

function update()
    if collected or not player or not player.transform then return end
    local position = player.transform.position
    local pickup = transform.position
    local dx = position.x - pickup.x
    local dy = position.y - pickup.y
    local dz = position.z - pickup.z
    if dx * dx + dy * dy + dz * dz >= 3.24 then return end

    collected = true
    local visual = gameObject:GetComponent("Renderer")
    if visual then visual.enabled = false end
    local body = gameObject:GetComponent("Rigidbody")
    if body then Destroy(body) end
end
