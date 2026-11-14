local yaw, pitch = -90, -24
local captured, jumpHeld, phase, reported = true, false, 0, false
local obstacles = {{-16.3,-32,-9.7,-26,4.65},{-16.3,-20,-9.7,-14,5.0},{-16.3,-8,-9.7,-2,5.35},{-16.3,9,-9.7,15,4.65},{-16.3,23,-9.7,29,5.0},{9.7,-32,16.3,-26,5.35},{9.7,-20,16.3,-14,4.65},{9.7,15,16.3,21,5.0},{9.7,28,16.3,34,5.35},{-28.3,-28,-21.7,-22,4.65},{-28.3,-13,-21.7,-7,5.0},{-28.3,13,-21.7,19,5.35},{-28.3,27,-21.7,33,4.65},{22.7,-27,29.3,-21,5.0},{22.7,20,29.3,26,5.35},{22.7,32,29.3,38,4.65}}

local function cameraPosition(target, desired)
    local delta = desired - target
    local nearest = 1
    for _, b in ipairs(obstacles) do
        local low, high = 0, 1
        local mins, maxs = {b[1],0,b[2]}, {b[3],b[5],b[4]}
        local origins, dirs = {target.x,target.y,target.z}, {delta.x,delta.y,delta.z}
        for axis=1,3 do
            if math.abs(dirs[axis]) < 0.0001 then
                if origins[axis] < mins[axis] or origins[axis] > maxs[axis] then low=2 end
            else
                local a = (mins[axis]-origins[axis])/dirs[axis]
                local c = (maxs[axis]-origins[axis])/dirs[axis]
                if a>c then a,c=c,a end
                low=math.max(low,a); high=math.min(high,c)
            end
        end
        if low<=high and low>=0 and low<nearest then nearest=math.max(.12,low-.06) end
    end
    return target + delta*nearest
end

function setup()
    Input.SetMouseCaptured(true)
    local camera = GameObject.Find("Camera_TerceiraPessoa")
    assert(camera and camera:GetComponent("Camera"), "Camera de terceira pessoa ausente")
    assert(gameObject:GetComponent("Rigidbody"), "Colisor do jogador ausente")
    Debug.Log("Vila Aurora pronta | WASD: andar | Shift: correr | Espaco: pular | Mouse: camera | Esc: liberar | Enter: capturar")
end

function update()
    local dt=math.min(Time.deltaTime,.05)
    local body=gameObject:GetComponent("Rigidbody")
    local camera=GameObject.Find("Camera_TerceiraPessoa")
    if not body or not camera then return end
    if Input.GetKey("Escape") then captured=false; Input.SetMouseCaptured(false) end
    if Input.GetKey("Return") then captured=true; Input.SetMouseCaptured(true) end
    local mx,my=Input.GetMouseDelta()
    if captured then yaw=yaw+mx*.13; pitch=math.max(-65,math.min(-8,pitch-my*.13)) end
    local rad=math.rad(yaw)
    local forward=Vector3(math.cos(rad),0,math.sin(rad))
    local right=forward:cross(Vector3(0,1,0))
    local direction=Vector3(0,0,0)
    if captured then direction=forward*Input.GetAxis("Vertical")+right*Input.GetAxis("Horizontal") end
    local moving=direction:length()>.01
    if direction:length()>1 then direction=direction:normalized() end
    local speed=Input.GetKey("LeftShift") and 7.5 or 4.5
    local velocity=direction*speed
    velocity.y=body.velocity.y
    local pressed=Input.GetKey("Space")
    if captured and pressed and not jumpHeld and gameObject.transform.position.y<1.22 and math.abs(velocity.y)<.4 then velocity.y=5.6 end
    jumpHeld=pressed
    body.velocity=velocity
    local visual=GameObject.Find("Visual_Jogador")
    if moving and visual then visual.transform.localEulerAngles=Vector3(0,math.deg(math.atan(direction.x,direction.z)),0) end
    phase=phase+dt*(moving and speed*2.5 or 0)
    for _,entry in ipairs({{"Perna_E",1},{"Perna_D",-1},{"Braco_E",-1},{"Braco_D",1}}) do
        local limb=GameObject.Find(entry[1])
        if limb then limb.transform.localEulerAngles=Vector3(moving and math.sin(phase)*28*entry[2] or 0,0,0) end
    end
    if gameObject.transform.position.y < -5 then gameObject.transform.position=Vector3(0,1.1,18); body.velocity=Vector3(0,0,0) end
    local target=gameObject.transform.position+Vector3(0,.6,0)
    local tilt=math.rad(pitch)
    local aim=Vector3(math.cos(rad)*math.cos(tilt),math.sin(tilt),math.sin(rad)*math.cos(tilt))
    local desired=cameraPosition(target,target-aim*7.2)
    camera.transform.position=camera.transform.position:lerp(desired,1-math.exp(-12*dt))
    camera.transform.localEulerAngles=Vector3(pitch,yaw,0)
    if not reported and Time.time>1 then Debug.Log("Third-person controller ativo; jogador e camera validos"); reported=true end
end
