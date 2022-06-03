-- This file shows the API I want to be able to use.
-- It does not actually work yet.

ez = require "ezserv"

s = assert(ez.start_server(80))
s:accept()

-- This should fail gracefully
success, s2 = pcall(
    function()
        return ez.start_server(80)
    end
)

assert(success ~= true)
print(s2)

deftab = {
    __index = function(t,k)
        if (rawget(t,k) == nil) then
            t[k] = {}
        end
        return rawget(t,k)
    end
}
ephemeron = {
    __mode = "k"
}
ws_sessions = {}
setmetatable(ws_sessions, ephemeron)

filemap = {
    ["/"] = "index.html",
    ["/index.html"] = "index.html",
    ["/index.htm"] = "index.html",
    ["/index"] = "index.html",
    ["/hello.js"] = "hello.js"
}

print("------------BEGIN------------")

quit = false
while not quit do
    -- ev has the event data, which can be different depending
    -- on what happened. src is the client that generated the
    -- event. 
    -- e.type could be "connect", "request", "data", "close", 
    -- or "error". We will say that upgraded connections will
    -- report closure on the old http object, and report connect
    -- on a new ws object.
    --   - But what if the lua code was never aware of the original
    --     HTTP connection? Is it still okay to report the closure?
    --   - If it was an error, we would give extra info, like
    --     whether it was EOS
    -- src is a userdata that can represent either an HTTP session
    -- or a websocket session. It should be possible to use it as
    -- the index in a table, or to say "did this message come from
    -- this handle I saved earlier"
    ev,src = s:next_event()
    print("ev.type = ", ev.type)
    status,msg = pcall( function()
        if (ev.type == "connect") then
            print("is upgrade: ", tostring(ev.is_upgrade))
            if (ev.is_upgrade) then
                ws_sessions[src] = true
            else
                s:accept()
            end
            src:recv()
        elseif (ev.type == "request") then
            print("Request:", ev.method, ev.target)
            print("Request body = [" .. ev.data .. "]")
            print("Is upgrade: ", tostring(ev.is_upgrade));
            if (ev.is_upgrade) then
                print("upgrade requested")
                src:upgrade()
            else
                local filename = filemap[ev.target]
                --print("Using filename", tostring(filename))
                local status,f = pcall(function()
                    local fp = io.open(filename, "rb")
                    return fp:read("*a")
                end)
                if (status == true) then
                    -- A new event on src will only be generated if
                    -- the write fails
                    f = f:gsub("world", "from lua")
                    src:send(f, true)
                else
                    src:send(ez.http.not_found)
                end
                src:recv()
            end
        elseif (ev.type == "data") then
            print("Received websocket data")
            print("data = [" .. ev.data .. "]")
    
            if (ev.data == "super secret string") then
                --FIXME: need better way to shut down server
                print("Quitting server")
                quit = true
            end
            
            -- Broadcast to all clients
            for ws,_ in pairs(ws_sessions) do
                print("broadcasting...")
                ws:send(ev.data, true)
            end

            print("starting async ws_recv...")
            src:recv()
        elseif (ev.type == "ack") then
            print("Received an ack on ", src)
        elseif (ev.type == "close") then
            ws_sessions[src] = nil -- Don't even need to care whether 
                                   -- it was there already. P.S. allows
                                   -- userdata to be gc'ed
        elseif (ev.type == "error") then
            print("ezserv reported an error:", ev.message)
        end
        print("--------------------------------")
    end)

    if (not status) then
        print("Lua error while handling event:", msg)
        break
    end
    -- Other idea: keep a table mapping socket/websocket handles
    -- to resumable coroutines
end