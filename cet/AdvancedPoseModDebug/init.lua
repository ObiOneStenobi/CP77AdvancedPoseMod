-- AdvancedPoseMod - developer debug window (Cyber Engine Tweaks)
-- Not meant for end users: the final UI will live inside Photo Mode.
-- Open the CET overlay while in Photo Mode to use it.

local state = {
    overlayOpen = false,
    bones = {},          -- bone names of V's main skeleton
    filter = "",
    selected = nil,      -- selected bone name
    rot = { 0, 0, 0 },   -- degrees
    trans = { 0, 0, 0 }, -- meters
    poseName = "my_pose",
    poses = {},
    message = "",
}

local function call(name, ...)
    local fn = Game[name]
    if not fn then
        state.message = "Missing native " .. name .. " (is AdvancedPoseMod.dll loaded?)"
        return nil
    end
    local ok, result = pcall(fn, ...)
    if not ok then
        state.message = "Error calling " .. name .. ": " .. tostring(result)
        return nil
    end
    return result
end

local function refreshBones()
    state.bones = {}
    local count = call("APM_GetBoneCount") or 0
    for i = 0, count - 1 do
        local cname = call("APM_GetBoneName", i)
        if cname then
            table.insert(state.bones, Game.NameToString(cname))
        end
    end
    state.message = string.format("%d bones", #state.bones)
end

local function refreshPoses()
    state.poses = {}
    local count = call("APM_GetPoseCount") or 0
    for i = 0, count - 1 do
        table.insert(state.poses, call("APM_GetPoseName", i))
    end
end

local function readSelected()
    if not state.selected then return end
    local bone = CName.new(state.selected)
    local r = call("APM_GetBoneRotation", bone)
    local t = call("APM_GetBoneTranslation", bone)
    if r then state.rot = { r.x, r.y, r.z } end
    if t then state.trans = { t.x, t.y, t.z } end
end

local function applySelected()
    if not state.selected then return end
    local bone = CName.new(state.selected)
    call("APM_SetBoneRotation", bone, state.rot[1], state.rot[2], state.rot[3])
    call("APM_SetBoneTranslation", bone, state.trans[1], state.trans[2], state.trans[3])
end

local function drawBoneList()
    state.filter = ImGui.InputTextWithHint("##filter", "Filter bones...", state.filter, 64)
    ImGui.SameLine()
    if ImGui.Button("Refresh") then refreshBones() end

    ImGui.BeginChild("bones", 220, 360, true)
    local filter = string.lower(state.filter)
    for _, name in ipairs(state.bones) do
        if filter == "" or string.find(string.lower(name), filter, 1, true) then
            if ImGui.Selectable(name, state.selected == name) then
                state.selected = name
                readSelected()
            end
        end
    end
    ImGui.EndChild()
end

local function drawBoneEditor()
    ImGui.BeginGroup()
    if not state.selected then
        ImGui.Text("Select a bone")
        ImGui.EndGroup()
        return
    end

    ImGui.Text("Bone: " .. state.selected)
    ImGui.Separator()
    ImGui.Text("Rotation (degrees, local axes)")
    local changed = false
    local used
    for i, axis in ipairs({ "X", "Y", "Z" }) do
        state.rot[i], used = ImGui.SliderFloat("Rot " .. axis, state.rot[i], -180, 180, "%.1f")
        changed = changed or used
    end
    ImGui.Text("Translation (meters, parent space)")
    for i, axis in ipairs({ "X", "Y", "Z" }) do
        state.trans[i], used = ImGui.SliderFloat("Move " .. axis, state.trans[i], -0.5, 0.5, "%.3f")
        changed = changed or used
    end
    if changed then applySelected() end

    if ImGui.Button("Reset bone") then
        call("APM_ResetBone", CName.new(state.selected))
        readSelected()
    end
    ImGui.SameLine()
    if ImGui.Button("Reset all") then
        call("APM_ResetAll")
        readSelected()
    end
    ImGui.EndGroup()
end

local function drawPoseFiles()
    ImGui.Separator()
    state.poseName = ImGui.InputText("Pose name", state.poseName, 64)
    if ImGui.Button("Save") then
        state.message = call("APM_SavePose", state.poseName) and ("Saved " .. state.poseName) or "Save failed"
        refreshPoses()
    end
    ImGui.SameLine()
    if ImGui.Button("Refresh list") then refreshPoses() end

    for _, name in ipairs(state.poses) do
        if ImGui.Selectable("Load: " .. name, false) then
            state.message = call("APM_LoadPose", name) and ("Loaded " .. name) or "Load failed"
            state.poseName = name
            readSelected()
        end
    end
end

registerForEvent("onOverlayOpen", function() state.overlayOpen = true end)
registerForEvent("onOverlayClose", function() state.overlayOpen = false end)

registerForEvent("onDraw", function()
    if not state.overlayOpen then return end

    if ImGui.Begin("AdvancedPoseMod (debug)") then
        local active = call("APM_IsPhotoModeActive")
        ImGui.Text(active and "Photo Mode: active" or "Photo Mode: not active (open Photo Mode first)")
        if active and #state.bones == 0 then
            refreshBones()
            refreshPoses()
        end

        local enabled = call("APM_IsEnabled") or false
        local newEnabled, pressed = ImGui.Checkbox("Custom pose enabled", enabled)
        if pressed then call("APM_SetEnabled", newEnabled) end
        ImGui.Separator()

        drawBoneList()
        ImGui.SameLine()
        drawBoneEditor()
        drawPoseFiles()

        if state.message ~= "" then
            ImGui.Separator()
            ImGui.TextWrapped(state.message)
        end
    end
    ImGui.End()
end)
