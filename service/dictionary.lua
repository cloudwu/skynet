local skynet = require "skynet";
local cmd = {};
local data = {};
local requests = {};
local changed = nil;

function cmd.get(session, source, key)
    local value = data[key];
    if value then
        return skynet.ret(skynet.pack(value));
    end

    requests[key] = requests[key] or {};
    table.insert(requests[key], {session, source})
end

function cmd.set(session, source, key, value, sendmode)
    data[key] = value;

    if value then
        changed = changed or {};
        changed[key] = true;
    end 

    if not sendmode then
        skynet.ret(skynet.pack(true));
    end
end

function cmd.remove_request(session, source)
    for k, v in pairs(requests) do
        for i = #v, 1, -1 do
            local tmpsession, tmpsource = table.unpack(v[i]);
            if tmpsource == source then
                table.remove(i);
            end
        end

        if #v == 0 then
            requests[k] = nil;
        end
    end
end

local function process_request(key, value)
    local request = requests[key];
    if not request then
        return;
    end

    requests[key] = nil;
    if not next(requests) then
        requests = {};
    end

    for k,v in ipairs(request) do
        local session, source = table.unpack(v);
        local msg, size = skynet.pack(value); 
    
        skynet.redirect(source, 0, skynet.PTYPE_RESPONSE, session, msg, size);
    end
end

local function process_changed()
    if not changed then
        return;
    end

    for k, v in pairs(changed) do
        local value = data[k];
        if value then
            process_request(k, value);
        end
    end

    changed = nil;
end

local function query()
    while true do
        process_changed();
        skynet.sleep(1);
    end
end

skynet.start(function()
    skynet.dispatch("lua", function(session, source, command, ...)
        local f = cmd[command];
        if f then
            f(session, source, ...);
        else
            skynet.error("unknown cmd:", command);
        end
    end)

    skynet.fork(query);
end)
