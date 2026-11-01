-- Editable per entity in the Inspector; read with properties.speed.
--@property {"name":"speed","type":"number","default":5}

local body = gameObject:GetComponent("Transform");
local lamp = gameObject:GetComponent("SpotLight");
-- Called once when the scene starts.
function setup()
end

-- Called every frame.
function update()
	lamp.direction = body.eulerAngles;
	lamp.position = body.position;
end
