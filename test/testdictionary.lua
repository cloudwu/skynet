local skynet = require "skynet"
local test = {};
local dictionary = nil;

function test.normal()
	local key = "test1";
	local value = 5;
	skynet.call(dictionary, "lua", "set", key, value); 

	local check = skynet.call(dictionary, "lua", "get", key, value); 
	assert(check == value);
end	

function test.delay()
	local key = "test2";
	local value = 6;

	skynet.timeout(2, function ()
		skynet.call(dictionary, "lua", "set", key, value); 
	end)

	local check = skynet.call(dictionary, "lua", "get", key, value); 
	assert(check == value);
end	

function test.set_again()
	local key = "test3";
	local value = 6;

	skynet.timeout(2, function ()
		skynet.call(dictionary, "lua", "set", key, value); 
	end)

	local check = skynet.call(dictionary, "lua", "get", key, value); 
	assert(check == value);

	skynet.call(dictionary, "lua", "set", key, value); 
end	

function test.multiple()
	local key = "test4";
	local value = 6;

	for i = 1, 10 do
		skynet.timeout(1, function ()
			local check = skynet.call(dictionary, "lua", "get", key, value); 
			assert(check == value);
		end)
	end

	skynet.timeout(2, function ()
		skynet.call(dictionary, "lua", "set", key, value); 
	end)


	local check = skynet.call(dictionary, "lua", "get", key, value); 
	assert(check == value);	
end	

skynet.start(function()
	dictionary = skynet.newservice("dictionary"); 
	for k, v in pairs(test) do
		v();
	end
    skynet.fork(skynet.exit);  
end)
