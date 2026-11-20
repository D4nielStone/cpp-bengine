-- Atribua este script a uma entidade com Camera e copie exposure.frag
-- para shaders/exposure.frag no projeto.
function setup()
    if not Globals.Has("exposure") then Globals.Set("exposure", 1.0) end
    local camera = gameObject:GetComponent("Camera")
    if camera then camera.postProcessShader = "shaders/exposure.frag" end
end

function update()
    local camera = gameObject:GetComponent("Camera")
    if camera then camera:SetUniform("exposure", "float", Globals.Get("exposure") or 1.0) end
end
-- Em outro script: Globals.Set("exposure", 1.5)
