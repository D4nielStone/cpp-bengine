-- New cameras and Empty/FPP projects already use this neutral gray sky.
-- Use the same API to customize a camera from a script.
function setup()
    local camera = gameObject:GetComponent("Camera")
    if camera then
        camera.backgroundColor = Color(0.16, 0.16, 0.16, 1)
    end
end
