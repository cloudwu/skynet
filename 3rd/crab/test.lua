local crab = require "crab.c"
local utf8 = require "utf8.c"

local words = {}
for line in io.lines("words.txt") do
    local t = {}
    assert(utf8.toutf32(line, t), "non utf8 words detected:"..line)
    table.insert(words, t)
end

crab.open(words)

local input = io.input("texts.txt"):read("*a")
local texts = {}
assert(utf8.toutf32(input, texts), "non utf8 words detected:", texts)
crab.filter(texts)
local output = utf8.toutf8(texts)

print(output)

