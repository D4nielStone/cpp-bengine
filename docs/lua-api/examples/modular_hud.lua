local hud, health

function setup()
    hud = gameObject:AddHUD()
    if not hud then return end
    health = hud:Find("health")
    if health then return end
    local panel = hud:Add("button", "Inventory")
    panel.id = "inventory"
    panel.absolute = true
    panel.position = Vector3(24, 24, 0)
    panel.width, panel.height = 240, 140
    panel.background = Color(0.08, 0.1, 0.16, 1)
    panel.hoverColor = Color(0.12, 0.16, 0.24, 1)
    panel.pressedColor = Color(0.04, 0.06, 0.1, 1)
    panel.borderColor = Color(0.4, 0.7, 1, 1)
    panel.borderSize, panel.borderRadius = 2, 8
    panel.callback = "inventory_clicked"
    health = hud:Add("text", "Vida: 100", "inventory")
    health.id = "health"
    health.absolute = true -- posição livre, relativa ao pai por padrão
    health.position = Vector3(12, 50, 0)
    health.width, health.height = 200, 32
    -- Após importar os arquivos no projeto:
    -- health.font = "fonts/game.ttf" -- também .otf
    -- health.shader = "shaders/text_pulse.frag"
    -- panel.image = "textures/button.png"
    -- local icon = hud:Add("image", "", "inventory")
    -- icon.image = "textures/heart.png"
end

function inventory_clicked(id)
    hud:Get("health").text = "Inventário aberto"
end

function update() end
