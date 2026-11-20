-- Attach to an entity with SoundPlayer and Transform; clip is project-relative.
local elapsed = 0
local started = false
function setup()
    elapsed = 0
    started = false
end
function update()
    elapsed = elapsed + Time.deltaTime
    if not started and elapsed >= 1 then
        local audio = gameObject:GetComponent("SoundPlayer")
        if audio then
            audio.looping = false
            started = audio:Play()
        end
    end
end
