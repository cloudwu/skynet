local skynet = require "skynet"
local blocking = require("blocking")
local blockingfile = require("blockingfile")

local function test_blocking()
    blocking.start_service(1)
    for i = 1, 3 do
        skynet.fork(function()
            print(blocking.popen("ip ro"))
            print(blocking.execute("sleep 11"))
            print(blocking.writefile("a.txt", os.time()))
            print(blocking.appendfile("a.txt", os.time()))
            print(blocking.readfile("a.txt"))

            --local fp = blocking_open("a.txt", "r")
            --print(type(fp), fp)
            print("done")
        end)
    end
end

local function test_files()
    --files.start_service(10)
    skynet.fork(function()
        local fp, err = blockingfile.open("a.txt")
        print(skynet.now(), blockingfile.seek(fp, "set", 4))
        print(skynet.now(), blockingfile.read(fp, 5))
        --blockingfile.close(fp)
    end)
    skynet.fork(function()
        local fp, err = blockingfile.open("b.txt", "a+")
        print(skynet.now(), blockingfile.write(fp, "123456\n"))
        print(skynet.now(), blockingfile.write(fp, "000000\n"))
        print(blockingfile.flush(fp))
        --blockingfile.close(fp)
    end)
end

skynet.start(function()
    --test_files()
    test_blocking()
    skynet.fork(function ()
        while true do
            print(skynet.now())
            skynet.sleep(100)
        end
    end)
end)

