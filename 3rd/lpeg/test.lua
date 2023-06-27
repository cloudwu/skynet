#!/usr/bin/env lua

-- require"strict"    -- just to be pedantic

local m = require"lpeg"


-- for general use
local a, b, c, d, e, f, g, p, t


-- compatibility with Lua 5.2
local unpack = rawget(table, "unpack") or unpack
local loadstring = rawget(_G, "loadstring") or load


local any = m.P(1)
local space = m.S" \t\n"^0

local function checkeq (x, y, p)
if p then print(x,y) end
  if type(x) ~= "table" then assert(x == y)
  else
    for k,v in pairs(x) do checkeq(v, y[k], p) end
    for k,v in pairs(y) do checkeq(v, x[k], p) end
  end
end


local mt = getmetatable(m.P(1))


local allchar = {}
for i=0,255 do allchar[i + 1] = i end
allchar = string.char(unpack(allchar))
assert(#allchar == 256)

local function cs2str (c)
  return m.match(m.Cs((c + m.P(1)/"")^0), allchar)
end

local function eqcharset (c1, c2)
  assert(cs2str(c1) == cs2str(c2))
end


print"General tests for LPeg library"

assert(type(m.version) == "string")
print(m.version)
assert(m.type("alo") ~= "pattern")
assert(m.type(io.input) ~= "pattern")
assert(m.type(m.P"alo") == "pattern")

-- tests for some basic optimizations
assert(m.match(m.P(false) + "a", "a") == 2)
assert(m.match(m.P(true) + "a", "a") == 1)
assert(m.match("a" + m.P(false), "b") == nil)
assert(m.match("a" + m.P(true), "b") == 1)

assert(m.match(m.P(false) * "a", "a") == nil)
assert(m.match(m.P(true) * "a", "a") == 2)
assert(m.match("a" * m.P(false), "a") == nil)
assert(m.match("a" * m.P(true), "a") == 2)

assert(m.match(#m.P(false) * "a", "a") == nil)
assert(m.match(#m.P(true) * "a", "a") == 2)
assert(m.match("a" * #m.P(false), "a") == nil)
assert(m.match("a" * #m.P(true), "a") == 2)

assert(m.match(m.P(1)^0, "abcd") == 5)
assert(m.match(m.S("")^0, "abcd") == 1)

-- tests for locale
do
  assert(m.locale(m) == m)
  local t = {}
  assert(m.locale(t, m) == t)
  local x = m.locale()
  for n,v in pairs(x) do
    assert(type(n) == "string")
    eqcharset(v, m[n])
  end
end


assert(m.match(3, "aaaa"))
assert(m.match(4, "aaaa"))
assert(not m.match(5, "aaaa"))
assert(m.match(-3, "aa"))
assert(not m.match(-3, "aaa"))
assert(not m.match(-3, "aaaa"))
assert(not m.match(-4, "aaaa"))
assert(m.P(-5):match"aaaa")

assert(m.match("a", "alo") == 2)
assert(m.match("al", "alo") == 3)
assert(not m.match("alu", "alo"))
assert(m.match(true, "") == 1)

local digit = m.S"0123456789"
local upper = m.S"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
local lower = m.S"abcdefghijklmnopqrstuvwxyz"
local letter = m.S"" + upper + lower
local alpha = letter + digit + m.R()

eqcharset(m.S"", m.P(false))
eqcharset(upper, m.R("AZ"))
eqcharset(lower, m.R("az"))
eqcharset(upper + lower, m.R("AZ", "az"))
eqcharset(upper + lower, m.R("AZ", "cz", "aa", "bb", "90"))
eqcharset(digit, m.S"01234567" + "8" + "9")
eqcharset(upper, letter - lower)
eqcharset(m.S(""), m.R())
assert(cs2str(m.S("")) == "")

eqcharset(m.S"\0", "\0")
eqcharset(m.S"\1\0\2", m.R"\0\2")
eqcharset(m.S"\1\0\2", m.R"\1\2" + "\0")
eqcharset(m.S"\1\0\2" - "\0", m.R"\1\2")

eqcharset(m.S("\0\255"), m.P"\0" + "\255")   -- charset extremes

local word = alpha^1 * (1 - alpha)^0

assert((word^0 * -1):match"alo alo")
assert(m.match(word^1 * -1, "alo alo"))
assert(m.match(word^2 * -1, "alo alo"))
assert(not m.match(word^3 * -1, "alo alo"))

assert(not m.match(word^-1 * -1, "alo alo"))
assert(m.match(word^-2 * -1, "alo alo"))
assert(m.match(word^-3 * -1, "alo alo"))

local eos = m.P(-1)

assert(m.match(digit^0 * letter * digit * eos, "1298a1"))
assert(not m.match(digit^0 * letter * eos, "1257a1"))

b = {
  [1] = "(" * (((1 - m.S"()") + #m.P"(" * m.V(1))^0) * ")"
}

assert(m.match(b, "(al())()"))
assert(not m.match(b * eos, "(al())()"))
assert(m.match(b * eos, "((al())()(é))"))
assert(not m.match(b, "(al()()"))

assert(not m.match(letter^1 - "for", "foreach"))
assert(m.match(letter^1 - ("for" * eos), "foreach"))
assert(not m.match(letter^1 - ("for" * eos), "for"))

function basiclookfor (p)
  return m.P {
    [1] = p + (1 * m.V(1))
  }
end

function caplookfor (p)
  return basiclookfor(p:C())
end

assert(m.match(caplookfor(letter^1), "   4achou123...") == "achou")
a = {m.match(caplookfor(letter^1)^0, " two words, one more  ")}
checkeq(a, {"two", "words", "one", "more"})

assert(m.match( basiclookfor((#m.P(b) * 1) * m.Cp()), "  (  (a)") == 7)

a = {m.match(m.C(digit^1 * m.Cc"d") + m.C(letter^1 * m.Cc"l"), "123")}
checkeq(a, {"123", "d"})

-- bug in LPeg 0.12  (nil value does not create a 'ktable')
assert(m.match(m.Cc(nil), "") == nil)

a = {m.match(m.C(digit^1 * m.Cc"d") + m.C(letter^1 * m.Cc"l"), "abcd")}
checkeq(a, {"abcd", "l"})

a = {m.match(m.Cc(10,20,30) * 'a' * m.Cp(), 'aaa')}
checkeq(a, {10,20,30,2})
a = {m.match(m.Cp() * m.Cc(10,20,30) * 'a' * m.Cp(), 'aaa')}
checkeq(a, {1,10,20,30,2})
a = m.match(m.Ct(m.Cp() * m.Cc(10,20,30) * 'a' * m.Cp()), 'aaa')
checkeq(a, {1,10,20,30,2})
a = m.match(m.Ct(m.Cp() * m.Cc(7,8) * m.Cc(10,20,30) * 'a' * m.Cp()), 'aaa')
checkeq(a, {1,7,8,10,20,30,2})
a = {m.match(m.Cc() * m.Cc() * m.Cc(1) * m.Cc(2,3,4) * m.Cc() * 'a', 'aaa')}
checkeq(a, {1,2,3,4})

a = {m.match(m.Cp() * letter^1 * m.Cp(), "abcd")}
checkeq(a, {1, 5})


t = {m.match({[1] = m.C(m.C(1) * m.V(1) + -1)}, "abc")}
checkeq(t, {"abc", "a", "bc", "b", "c", "c", ""})

-- bug in 0.12 ('hascapture' did not check for captures inside a rule)
do
  local pat = m.P{
    'S';
    S1 = m.C('abc') + 3,
    S = #m.V('S1')    -- rule has capture, but '#' must ignore it
  }
  assert(pat:match'abc' == 1)
end


-- bug: loop in 'hascaptures'
do
  local p = m.C(-m.P{m.P'x' * m.V(1) + m.P'y'})
  assert(p:match("xxx") == "")
end



-- test for small capture boundary
for i = 250,260 do
  assert(#m.match(m.C(i), string.rep('a', i)) == i)
  assert(#m.match(m.C(m.C(i)), string.rep('a', i)) == i)
end

-- tests for any*n and any*-n
for n = 1, 550, 13 do
  local x_1 = string.rep('x', n - 1)
  local x = x_1 .. 'a'
  assert(not m.P(n):match(x_1))
  assert(m.P(n):match(x) == n + 1)
  assert(n < 4 or m.match(m.P(n) + "xxx", x_1) == 4)
  assert(m.C(n):match(x) == x)
  assert(m.C(m.C(n)):match(x) == x)
  assert(m.P(-n):match(x_1) == 1)
  assert(not m.P(-n):match(x))
  assert(n < 13 or m.match(m.Cc(20) * ((n - 13) * m.P(10)) * 3, x) == 20)
  local n3 = math.floor(n/3)
  assert(m.match(n3 * m.Cp() * n3 * n3, x) == n3 + 1)
end

-- true values
assert(m.P(0):match("x") == 1)
assert(m.P(0):match("") == 1)
assert(m.C(0):match("x") == "")

assert(m.match(m.Cc(0) * m.P(10) + m.Cc(1) * "xuxu", "xuxu") == 1)
assert(m.match(m.Cc(0) * m.P(10) + m.Cc(1) * "xuxu", "xuxuxuxuxu") == 0)
assert(m.match(m.C(m.P(2)^1), "abcde") == "abcd")
p = m.Cc(0) * 1 + m.Cc(1) * 2 + m.Cc(2) * 3 + m.Cc(3) * 4


-- test for alternation optimization
assert(m.match(m.P"a"^1 + "ab" + m.P"x"^0, "ab") == 2)
assert(m.match((m.P"a"^1 + "ab" + m.P"x"^0 * 1)^0, "ab") == 3)
assert(m.match(m.P"ab" + "cd" + "" + "cy" + "ak", "98") == 1)
assert(m.match(m.P"ab" + "cd" + "ax" + "cy", "ax") == 3)
assert(m.match("a" * m.P"b"^0 * "c"  + "cd" + "ax" + "cy", "ax") == 3)
assert(m.match((m.P"ab" + "cd" + "ax" + "cy")^0, "ax") == 3)
assert(m.match(m.P(1) * "x" + m.S"" * "xu" + "ay", "ay") == 3)
assert(m.match(m.P"abc" + "cde" + "aka", "aka") == 4)
assert(m.match(m.S"abc" * "x" + "cde" + "aka", "ax") == 3)
assert(m.match(m.S"abc" * "x" + "cde" + "aka", "aka") == 4)
assert(m.match(m.S"abc" * "x" + "cde" + "aka", "cde") == 4)
assert(m.match(m.S"abc" * "x" + "ide" + m.S"ab" * "ka", "aka") == 4)
assert(m.match("ab" + m.S"abc" * m.P"y"^0 * "x" + "cde" + "aka", "ax") == 3)
assert(m.match("ab" + m.S"abc" * m.P"y"^0 * "x" + "cde" + "aka", "aka") == 4)
assert(m.match("ab" + m.S"abc" * m.P"y"^0 * "x" + "cde" + "aka", "cde") == 4)
assert(m.match("ab" + m.S"abc" * m.P"y"^0 * "x" + "ide" + m.S"ab" * "ka", "aka") == 4)
assert(m.match("ab" + m.S"abc" * m.P"y"^0 * "x" + "ide" + m.S"ab" * "ka", "ax") == 3)
assert(m.match(m.P(1) * "x" + "cde" + m.S"ab" * "ka", "aka") == 4)
assert(m.match(m.P(1) * "x" + "cde" + m.P(1) * "ka", "aka") == 4)
assert(m.match(m.P(1) * "x" + "cde" + m.P(1) * "ka", "cde") == 4)
assert(m.match(m.P"eb" + "cd" + m.P"e"^0 + "x", "ee") == 3)
assert(m.match(m.P"ab" + "cd" + m.P"e"^0 + "x", "abcd") == 3)
assert(m.match(m.P"ab" + "cd" + m.P"e"^0 + "x", "eeex") == 4)
assert(m.match(m.P"ab" + "cd" + m.P"e"^0 + "x", "cd") == 3)
assert(m.match(m.P"ab" + "cd" + m.P"e"^0 + "x", "x") == 1)
assert(m.match(m.P"ab" + "cd" + m.P"e"^0 + "x" + "", "zee") == 1)
assert(m.match(m.P"ab" + "cd" + m.P"e"^1 + "x", "abcd") == 3)
assert(m.match(m.P"ab" + "cd" + m.P"e"^1 + "x", "eeex") == 4)
assert(m.match(m.P"ab" + "cd" + m.P"e"^1 + "x", "cd") == 3)
assert(m.match(m.P"ab" + "cd" + m.P"e"^1 + "x", "x") == 2)
assert(m.match(m.P"ab" + "cd" + m.P"e"^1 + "x" + "", "zee") == 1)
assert(not m.match(("aa" * m.P"bc"^-1 + "aab") * "e", "aabe"))

assert(m.match("alo" * (m.P"\n" + -1), "alo") == 4)


-- bug in 0.12 (rc1)
assert(m.match((m.P"\128\187\191" + m.S"abc")^0, "\128\187\191") == 4)

assert(m.match(m.S"\0\128\255\127"^0, string.rep("\0\128\255\127", 10)) ==
    4*10 + 1)

-- optimizations with optional parts
assert(m.match(("ab" * -m.P"c")^-1, "abc") == 1)
assert(m.match(("ab" * #m.P"c")^-1, "abd") == 1)
assert(m.match(("ab" * m.B"c")^-1, "ab") == 1)
assert(m.match(("ab" * m.P"cd"^0)^-1, "abcdcdc") == 7)

assert(m.match(m.P"ab"^-1 - "c", "abcd") == 3)

p = ('Aa' * ('Bb' * ('Cc' * m.P'Dd'^0)^0)^0)^-1
assert(p:match("AaBbCcDdBbCcDdDdDdBb") == 21)


-- bug in 0.12.2
-- p = { ('ab' ('c' 'ef'?)*)? }
p = m.C(('ab' * ('c' * m.P'ef'^-1)^0)^-1)
s = "abcefccefc"
assert(s == p:match(s))
 

pi = "3.14159 26535 89793 23846 26433 83279 50288 41971 69399 37510"
assert(m.match(m.Cs((m.P"1" / "a" + m.P"5" / "b" + m.P"9" / "c" + 1)^0), pi) ==
  m.match(m.Cs((m.P(1) / {["1"] = "a", ["5"] = "b", ["9"] = "c"})^0), pi))
print"+"


-- tests for capture optimizations
assert(m.match((m.P(3) +  4 * m.Cp()) * "a", "abca") == 5)
t = {m.match(((m.P"a" + m.Cp()) * m.P"x")^0, "axxaxx")}
checkeq(t, {3, 6})


-- tests for numbered captures
p = m.C(1)
assert(m.match(m.C(m.C(p * m.C(2)) * m.C(3)) / 3, "abcdefgh") == "a")
assert(m.match(m.C(m.C(p * m.C(2)) * m.C(3)) / 1, "abcdefgh") == "abcdef")
assert(m.match(m.C(m.C(p * m.C(2)) * m.C(3)) / 4, "abcdefgh") == "bc")
assert(m.match(m.C(m.C(p * m.C(2)) * m.C(3)) / 0, "abcdefgh") == 7)

a, b, c = m.match(p * (m.C(p * m.C(2)) * m.C(3) / 4) * p, "abcdefgh")
assert(a == "a" and b == "efg" and c == "h")

-- test for table captures
t = m.match(m.Ct(letter^1), "alo")
checkeq(t, {})

t, n = m.match(m.Ct(m.C(letter)^1) * m.Cc"t", "alo")
assert(n == "t" and table.concat(t) == "alo")

t = m.match(m.Ct(m.C(m.C(letter)^1)), "alo")
assert(table.concat(t, ";") == "alo;a;l;o")

t = m.match(m.Ct(m.C(m.C(letter)^1)), "alo")
assert(table.concat(t, ";") == "alo;a;l;o")

t = m.match(m.Ct(m.Ct((m.Cp() * letter * m.Cp())^1)), "alo")
assert(table.concat(t[1], ";") == "1;2;2;3;3;4")

t = m.match(m.Ct(m.C(m.C(1) * 1 * m.C(1))), "alo")
checkeq(t, {"alo", "a", "o"})


-- tests for groups
p = m.Cg(1)   -- no capture
assert(p:match('x') == 'x')
p = m.Cg(m.P(true)/function () end * 1)   -- no value
assert(p:match('x') == 'x')
p = m.Cg(m.Cg(m.Cg(m.C(1))))
assert(p:match('x') == 'x')
p = m.Cg(m.Cg(m.Cg(m.C(1))^0) * m.Cg(m.Cc(1) * m.Cc(2)))
t = {p:match'abc'}
checkeq(t, {'a', 'b', 'c', 1, 2})

p = m.Ct(m.Cg(m.Cc(10), "hi") * m.C(1)^0 * m.Cg(m.Cc(20), "ho"))
t = p:match''
checkeq(t, {hi = 10, ho = 20})
t = p:match'abc'
checkeq(t, {hi = 10, ho = 20, 'a', 'b', 'c'})

-- non-string group names
p = m.Ct(m.Cg(1, print) * m.Cg(1, 23.5) * m.Cg(1, io))
t = p:match('abcdefghij')
assert(t[print] == 'a' and t[23.5] == 'b' and t[io] == 'c')


-- test for error messages
local function checkerr (msg, f, ...)
  local st, err = pcall(f, ...)
  assert(not st and m.match({ m.P(msg) + 1 * m.V(1) }, err))
end

checkerr("rule '1' may be left recursive", m.match, { m.V(1) * 'a' }, "a")
checkerr("rule '1' used outside a grammar", m.match, m.V(1), "")
checkerr("rule 'hiii' used outside a grammar", m.match, m.V('hiii'), "")
checkerr("rule 'hiii' undefined in given grammar", m.match, { m.V('hiii') }, "")
checkerr("undefined in given grammar", m.match, { m.V{} }, "")

checkerr("rule 'A' is not a pattern", m.P, { m.P(1), A = {} })
checkerr("grammar has no initial rule", m.P, { [print] = {} })

-- grammar with a long call chain before left recursion
p = {'a',
  a = m.V'b' * m.V'c' * m.V'd' * m.V'a',
  b = m.V'c',
  c = m.V'd',
  d = m.V'e',
  e = m.V'f',
  f = m.V'g',
  g = m.P''
}
checkerr("rule 'a' may be left recursive", m.match, p, "a")

-- Bug in peephole optimization of LPeg 0.12 (IJmp -> ICommit)
-- the next grammar has an original sequence IJmp -> ICommit -> IJmp L1
-- that is optimized to ICommit L1

p = m.P { (m.P {m.P'abc'} + 'ayz') * m.V'y'; y = m.P'x' }
assert(p:match('abcx') == 5 and p:match('ayzx') == 5 and not p:match'abc')


do
  print "testing large dynamic Cc"
  local lim = 2^16 - 1
  local c = 0
  local function seq (n) 
    if n == 1 then c = c + 1; return m.Cc(c)
    else
      local m = math.floor(n / 2)
      return seq(m) * seq(n - m)
    end
  end
  p = m.Ct(seq(lim))
  t = p:match('')
  assert(t[lim] == lim)
  checkerr("too many", function () p = p / print end)
  checkerr("too many", seq, lim + 1)
end


do
  -- nesting of captures too deep
  local p = m.C(1)
  for i = 1, 300 do
    p = m.Ct(p)
  end
  checkerr("too deep", p.match, p, "x")
end


-- tests for non-pattern as arguments to pattern functions

p = { ('a' * m.V(1))^-1 } * m.P'b' * { 'a' * m.V(2); m.V(1)^-1 }
assert(m.match(p, "aaabaac") == 7)

p = m.P'abc' * 2 * -5 * true * 'de'  -- mix of numbers and strings and booleans

assert(p:match("abc01de") == 8)
assert(p:match("abc01de3456") == nil)

p = 'abc' * (2 * (-5 * (true * m.P'de')))

assert(p:match("abc01de") == 8)
assert(p:match("abc01de3456") == nil)

p = { m.V(2), m.P"abc" } *
     (m.P{ "xx", xx = m.P"xx" } + { "x", x = m.P"a" * m.V"x" + "" })
assert(p:match("abcaaaxx") == 7)
assert(p:match("abcxx") == 6)


-- a large table capture
t = m.match(m.Ct(m.C('a')^0), string.rep("a", 10000))
assert(#t == 10000 and t[1] == 'a' and t[#t] == 'a')

print('+')


-- bug in 0.10 (rechecking a grammar, after tail-call optimization)
m.P{ m.P { (m.P(3) + "xuxu")^0 * m.V"xuxu", xuxu = m.P(1) } }

local V = m.V

local Space = m.S(" \n\t")^0
local Number = m.C(m.R("09")^1) * Space
local FactorOp = m.C(m.S("+-")) * Space
local TermOp = m.C(m.S("*/")) * Space
local Open = "(" * Space
local Close = ")" * Space


local function f_factor (v1, op, v2, d)
  assert(d == nil)
  if op == "+" then return v1 + v2
  else return v1 - v2
  end
end


local function f_term (v1, op, v2, d)
  assert(d == nil)
  if op == "*" then return v1 * v2
  else return v1 / v2
  end
end

G = m.P{ "Exp",
  Exp = V"Factor" * (FactorOp * V"Factor" % f_factor)^0;
  Factor = V"Term" * (TermOp * V"Term" % f_term)^0;
  Term = Number / tonumber  +  Open * V"Exp" * Close;
}

G = Space * G * -1

for _, s in ipairs{" 3 + 5*9 / (1+1) ", "3+4/2", "3+3-3- 9*2+3*9/1-  8"} do
  assert(m.match(G, s) == loadstring("return "..s)())
end


-- test for grammars (errors deep in calling non-terminals)
g = m.P{
  [1] = m.V(2) + "a",
  [2] = "a" * m.V(3) * "x",
  [3] = "b" * m.V(3) + "c"
}

assert(m.match(g, "abbbcx") == 7)
assert(m.match(g, "abbbbx") == 2)


-- tests for \0
assert(m.match(m.R("\0\1")^1, "\0\1\0") == 4)
assert(m.match(m.S("\0\1ab")^1, "\0\1\0a") == 5)
assert(m.match(m.P(1)^3, "\0\1\0a") == 5)
assert(not m.match(-4, "\0\1\0a"))
assert(m.match("\0\1\0a", "\0\1\0a") == 5)
assert(m.match("\0\0\0", "\0\0\0") == 4)
assert(not m.match("\0\0\0", "\0\0"))


-- tests for predicates
assert(not m.match(-m.P("a") * 2, "alo"))
assert(m.match(- -m.P("a") * 2, "alo") == 3)
assert(m.match(#m.P("a") * 2, "alo") == 3)
assert(m.match(##m.P("a") * 2, "alo") == 3)
assert(not m.match(##m.P("c") * 2, "alo"))
assert(m.match(m.Cs((##m.P("a") * 1 + m.P(1)/".")^0), "aloal") == "a..a.")
assert(m.match(m.Cs((#((#m.P"a")/"") * 1 + m.P(1)/".")^0), "aloal") == "a..a.")
assert(m.match(m.Cs((- -m.P("a") * 1 + m.P(1)/".")^0), "aloal") == "a..a.")
assert(m.match(m.Cs((-((-m.P"a")/"") * 1 + m.P(1)/".")^0), "aloal") == "a..a.")


-- fixed length
do
  -- 'and' predicate using fixed length
  local p = m.C(#("a" * (m.P("bd") + "cd")) * 2)
  assert(p:match("acd") == "ac")

  p = #m.P{ "a" * m.V(2), m.P"b" } * 2
  assert(p:match("abc") == 3)

  p = #(m.P"abc" * m.B"c")
  assert(p:match("abc") == 1 and not p:match("ab"))
 
  p = m.P{ "a" * m.V(2), m.P"b"^1 }
  checkerr("pattern may not have fixed length", m.B, p)

  p = "abc" * (m.P"b"^1 + m.P"a"^0)
  checkerr("pattern may not have fixed length", m.B, p)
end


p = -m.P'a' * m.Cc(1) + -m.P'b' * m.Cc(2) + -m.P'c' * m.Cc(3)
assert(p:match('a') == 2 and p:match('') == 1 and p:match('b') == 1)

p = -m.P'a' * m.Cc(10) + #m.P'a' * m.Cc(20)
assert(p:match('a') == 20 and p:match('') == 10 and p:match('b') == 10)



-- look-behind predicate
assert(not m.match(m.B'a', 'a'))
assert(m.match(1 * m.B'a', 'a') == 2)
assert(not m.match(m.B(1), 'a'))
assert(m.match(1 * m.B(1), 'a') == 2)
assert(m.match(-m.B(1), 'a') == 1)
assert(m.match(m.B(250), string.rep('a', 250)) == nil)
assert(m.match(250 * m.B(250), string.rep('a', 250)) == 251)

-- look-behind with an open call
checkerr("pattern may not have fixed length", m.B, m.V'S1')
checkerr("too long to look behind", m.B, 260)

B = #letter * -m.B(letter) + -letter * m.B(letter)
x = m.Ct({ (B * m.Cp())^-1 * (1 * m.V(1) + m.P(true)) })
checkeq(m.match(x, 'ar cal  c'), {1,3,4,7,9,10})
checkeq(m.match(x, ' ar cal  '), {2,4,5,8})
checkeq(m.match(x, '   '), {})
checkeq(m.match(x, 'aloalo'), {1,7})

assert(m.match(B, "a") == 1)
assert(m.match(1 * B, "a") == 2)
assert(not m.B(1 - letter):match(""))
assert((-m.B(letter)):match("") == 1)

assert((4 * m.B(letter, 4)):match("aaaaaaaa") == 5)
assert(not (4 * m.B(#letter * 5)):match("aaaaaaaa"))
assert((4 * -m.B(#letter * 5)):match("aaaaaaaa") == 5)

-- look-behind with grammars
assert(m.match('a' * m.B{'x', x = m.P(3)},  'aaa') == nil)
assert(m.match('aa' * m.B{'x', x = m.P('aaa')},  'aaaa') == nil)
assert(m.match('aaa' * m.B{'x', x = m.P('aaa')},  'aaaaa') == 4)



-- bug in 0.9
assert(m.match(('a' * #m.P'b'), "ab") == 2)
assert(not m.match(('a' * #m.P'b'), "a"))

assert(not m.match(#m.S'567', ""))
assert(m.match(#m.S'567' * 1, "6") == 2)


-- tests for Tail Calls

p = m.P{ 'a' * m.V(1) + '' }
assert(p:match(string.rep('a', 1000)) == 1001)

-- create a grammar for a simple DFA for even number of 0s and 1s
--
--  ->1 <---0---> 2
--    ^           ^
--    |           |
--    1           1
--    |           |
--    V           V
--    3 <---0---> 4
--
-- this grammar should keep no backtracking information

p = m.P{
  [1] = '0' * m.V(2) + '1' * m.V(3) + -1,
  [2] = '0' * m.V(1) + '1' * m.V(4),
  [3] = '0' * m.V(4) + '1' * m.V(1),
  [4] = '0' * m.V(3) + '1' * m.V(2),
}

assert(p:match(string.rep("00", 10000)))
assert(p:match(string.rep("01", 10000)))
assert(p:match(string.rep("011", 10000)))
assert(not p:match(string.rep("011", 10000) .. "1"))
assert(not p:match(string.rep("011", 10001)))


-- this grammar does need backtracking info.
local lim = 10000
p = m.P{ '0' * m.V(1) + '0' }
checkerr("stack overflow", m.match, p, string.rep("0", lim))
m.setmaxstack(2*lim)
checkerr("stack overflow", m.match, p, string.rep("0", lim))
m.setmaxstack(2*lim + 4)
assert(m.match(p, string.rep("0", lim)) == lim + 1)

-- this repetition should not need stack space (only the call does)
p = m.P{ ('a' * m.V(1))^0 * 'b' + 'c' }
m.setmaxstack(200)
assert(p:match(string.rep('a', 180) .. 'c' .. string.rep('b', 180)) == 362)

m.setmaxstack(100)   -- restore low limit

-- tests for optional start position
assert(m.match("a", "abc", 1))
assert(m.match("b", "abc", 2))
assert(m.match("c", "abc", 3))
assert(not m.match(1, "abc", 4))
assert(m.match("a", "abc", -3))
assert(m.match("b", "abc", -2))
assert(m.match("c", "abc", -1))
assert(m.match("abc", "abc", -4))   -- truncate to position 1

assert(m.match("", "abc", 10))   -- empty string is everywhere!
assert(m.match("", "", 10))
assert(not m.match(1, "", 1))
assert(not m.match(1, "", -1))
assert(not m.match(1, "", 0))

print("+")


-- tests for argument captures
checkerr("invalid argument", m.Carg, 0)
checkerr("invalid argument", m.Carg, -1)
checkerr("invalid argument", m.Carg, 2^18)
checkerr("absent extra argument #1", m.match, m.Carg(1), 'a', 1)
assert(m.match(m.Carg(1), 'a', 1, print) == print)
x = {m.match(m.Carg(1) * m.Carg(2), '', 1, 10, 20)}
checkeq(x, {10, 20})

assert(m.match(m.Cmt(m.Cg(m.Carg(3), "a") *
                     m.Cmt(m.Cb("a"), function (s,i,x)
                                        assert(s == "a" and i == 1);
                                        return i, x+1
                                      end) *
                     m.Carg(2), function (s,i,a,b,c)
                                  assert(s == "a" and i == 1 and c == nil);
				  return i, 2*a + 3*b
                                end) * "a",
               "a", 1, false, 100, 1000) == 2*1001 + 3*100)


-- tests for Lua functions

t = {}
s = ""
p = m.P(function (s1, i) assert(s == s1); t[#t + 1] = i; return nil end) * false
s = "hi, this is a test"
assert(m.match(((p - m.P(-1)) + 2)^0, s) == string.len(s) + 1)
assert(#t == string.len(s)/2 and t[1] == 1 and t[2] == 3)

assert(not m.match(p, s))

p = mt.__add(function (s, i) return i end, function (s, i) return nil end)
assert(m.match(p, "alo"))

p = mt.__mul(function (s, i) return i end, function (s, i) return nil end)
assert(not m.match(p, "alo"))


t = {}
p = function (s1, i) assert(s == s1); t[#t + 1] = i; return i end
s = "hi, this is a test"
assert(m.match((m.P(1) * p)^0, s) == string.len(s) + 1)
assert(#t == string.len(s) and t[1] == 2 and t[2] == 3)

t = {}
p = m.P(function (s1, i) assert(s == s1); t[#t + 1] = i;
                         return i <= s1:len() and i end) * 1
s = "hi, this is a test"
assert(m.match(p^0, s) == string.len(s) + 1)
assert(#t == string.len(s) + 1 and t[1] == 1 and t[2] == 2)

p = function (s1, i) return m.match(m.P"a"^1, s1, i) end
assert(m.match(p, "aaaa") == 5)
assert(m.match(p, "abaa") == 2)
assert(not m.match(p, "baaa"))

checkerr("invalid position", m.match, function () return 2^20 end, s)
checkerr("invalid position", m.match, function () return 0 end, s)
checkerr("invalid position", m.match, function (s, i) return i - 1 end, s)
checkerr("invalid position", m.match,
             m.P(1)^0 * function (_, i) return i - 1 end, s)
assert(m.match(m.P(1)^0 * function (_, i) return i end * -1, s))
checkerr("invalid position", m.match,
             m.P(1)^0 * function (_, i) return i + 1 end, s)
assert(m.match(m.P(function (s, i) return s:len() + 1 end) * -1, s))
checkerr("invalid position", m.match, m.P(function (s, i) return s:len() + 2 end) * -1, s)
assert(not m.match(m.P(function (s, i) return s:len() end) * -1, s))
assert(m.match(m.P(1)^0 * function (_, i) return true end, s) ==
       string.len(s) + 1)
for i = 1, string.len(s) + 1 do
  assert(m.match(function (_, _) return i end, s) == i)
end

p = (m.P(function (s, i) return i%2 == 0 and i end) * 1
  +  m.P(function (s, i) return i%2 ~= 0 and i + 2 <= s:len() and i end) * 3)^0
  * -1
assert(p:match(string.rep('a', 14000)))

-- tests for Function Replacements
f = function (a, ...) if a ~= "x" then return {a, ...} end end

t = m.match(m.C(1)^0/f, "abc")
checkeq(t, {"a", "b", "c"})

t = m.match(m.C(1)^0/f/f, "abc")
checkeq(t, {{"a", "b", "c"}})

t = m.match(m.P(1)^0/f/f, "abc")   -- no capture
checkeq(t, {{"abc"}})

t = m.match((m.P(1)^0/f * m.Cp())/f, "abc")
checkeq(t, {{"abc"}, 4})

t = m.match((m.C(1)^0/f * m.Cp())/f, "abc")
checkeq(t, {{"a", "b", "c"}, 4})

t = m.match((m.C(1)^0/f * m.Cp())/f, "xbc")
checkeq(t, {4})

t = m.match(m.C(m.C(1)^0)/f, "abc")
checkeq(t, {"abc", "a", "b", "c"})

g = function (...) return 1, ... end
t = {m.match(m.C(1)^0/g/g, "abc")}
checkeq(t, {1, 1, "a", "b", "c"})

t = {m.match(m.Cc(nil,nil,4) * m.Cc(nil,3) * m.Cc(nil, nil) / g / g, "")}
t1 = {1,1,nil,nil,4,nil,3,nil,nil}
for i=1,10 do assert(t[i] == t1[i]) end

-- bug in 0.12.2: ktable with only nil could be eliminated when joining
-- with a pattern without ktable
assert((m.P"aaa" * m.Cc(nil)):match"aaa" == nil)

t = {m.match((m.C(1) / function (x) return x, x.."x" end)^0, "abc")}
checkeq(t, {"a", "ax", "b", "bx", "c", "cx"})

t = m.match(m.Ct((m.C(1) / function (x,y) return y, x end * m.Cc(1))^0), "abc")
checkeq(t, {nil, "a", 1, nil, "b", 1, nil, "c", 1})

-- tests for Query Replacements

assert(m.match(m.C(m.C(1)^0)/{abc = 10}, "abc") == 10)
assert(m.match(m.C(1)^0/{a = 10}, "abc") == 10)
assert(m.match(m.S("ba")^0/{ab = 40}, "abc") == 40)
t = m.match(m.Ct((m.S("ba")/{a = 40})^0), "abc")
checkeq(t, {40})

assert(m.match(m.Cs((m.C(1)/{a=".", d=".."})^0), "abcdde") == ".bc....e")
assert(m.match(m.Cs((m.C(1)/{f="."})^0), "abcdde") == "abcdde")
assert(m.match(m.Cs((m.C(1)/{d="."})^0), "abcdde") == "abc..e")
assert(m.match(m.Cs((m.C(1)/{e="."})^0), "abcdde") == "abcdd.")
assert(m.match(m.Cs((m.C(1)/{e=".", f="+"})^0), "eefef") == "..+.+")
assert(m.match(m.Cs((m.C(1))^0), "abcdde") == "abcdde")
assert(m.match(m.Cs(m.C(m.C(1)^0)), "abcdde") == "abcdde")
assert(m.match(1 * m.Cs(m.P(1)^0), "abcdde") == "bcdde")
assert(m.match(m.Cs((m.C('0')/'x' + 1)^0), "abcdde") == "abcdde")
assert(m.match(m.Cs((m.C('0')/'x' + 1)^0), "0ab0b0") == "xabxbx")
assert(m.match(m.Cs((m.C('0')/'x' + m.P(1)/{b=3})^0), "b0a0b") == "3xax3")
assert(m.match(m.P(1)/'%0%0'/{aa = -3} * 'x', 'ax') == -3)
assert(m.match(m.C(1)/'%0%1'/{aa = 'z'}/{z = -3} * 'x', 'ax') == -3)

assert(m.match(m.Cs(m.Cc(0) * (m.P(1)/"")), "4321") == "0")

assert(m.match(m.Cs((m.P(1) / "%0")^0), "abcd") == "abcd")
assert(m.match(m.Cs((m.P(1) / "%0.%0")^0), "abcd") == "a.ab.bc.cd.d")
assert(m.match(m.Cs((m.P("a") / "%0.%0" + 1)^0), "abcad") == "a.abca.ad")
assert(m.match(m.C("a") / "%1%%%0", "a") == "a%a")
assert(m.match(m.Cs((m.P(1) / ".xx")^0), "abcd") == ".xx.xx.xx.xx")
assert(m.match(m.Cp() * m.P(3) * m.Cp()/"%2%1%1 - %0 ", "abcde") ==
   "411 - abc ")

assert(m.match(m.P(1)/"%0", "abc") == "a")
checkerr("invalid capture index", m.match, m.P(1)/"%1", "abc")
checkerr("invalid capture index", m.match, m.P(1)/"%9", "abc")

p = m.C(1)
p = p * p; p = p * p; p = p * p * m.C(1) / "%9 - %1"
assert(p:match("1234567890") == "9 - 1")

assert(m.match(m.Cc(print), "") == print)

-- too many captures (just ignore extra ones)
p = m.C(1)^0 / "%2-%9-%0-%9"
assert(p:match"01234567890123456789" == "1-8-01234567890123456789-8")
s = string.rep("12345678901234567890", 20)
assert(m.match(m.C(1)^0 / "%9-%1-%0-%3", s) == "9-1-" .. s .. "-3")

-- string captures with non-string subcaptures
p = m.Cc('alo') * m.C(1) / "%1 - %2 - %1"
assert(p:match'x' == 'alo - x - alo')

checkerr("invalid capture value (a boolean)", m.match, m.Cc(true) / "%1", "a")

-- long strings for string capture
l = 10000
s = string.rep('a', l) .. string.rep('b', l) .. string.rep('c', l)

p = (m.C(m.P'a'^1) * m.C(m.P'b'^1) * m.C(m.P'c'^1)) / '%3%2%1'

assert(p:match(s) == string.rep('c', l) ..
                     string.rep('b', l) .. 
                     string.rep('a', l))

print"+"

-- accumulator capture
function f (x) return x + 1 end
assert(m.match(m.Cf(m.Cc(0) * m.C(1)^0, f), "alo alo") == 7)
assert(m.match(m.Cc(0) * (m.C(1) % f)^0, "alo alo") == 7)

t = {m.match(m.Cf(m.Cc(1,2,3), error), "")}
checkeq(t, {1})
p = m.Cf(m.Ct(true) * m.Cg(m.C(m.R"az"^1) * "=" * m.C(m.R"az"^1) * ";")^0,
         rawset)
t = p:match("a=b;c=du;xux=yuy;")
checkeq(t, {a="b", c="du", xux="yuy"})


-- errors in fold capture

-- no initial capture
checkerr("no initial value", m.match, m.Cf(m.P(5), print), 'aaaaaa')
-- no initial capture (very long match forces fold to be a pair open-close)
checkerr("no initial value", m.match, m.Cf(m.P(500), print),
                               string.rep('a', 600))


-- errors in accumulator capture

-- no initial capture
checkerr("no previous value", m.match, m.P(5) % print, 'aaaaaa')
-- no initial capture (very long match forces fold to be a pair open-close)
checkerr("no previous value", m.match, m.P(500) % print,
                               string.rep('a', 600))


-- tests for loop checker

local function isnullable (p)
  checkerr("may accept empty string", function (p) return p^0 end, m.P(p))
end

isnullable(m.P("x")^-4)
assert(m.match(((m.P(0) + 1) * m.S"al")^0, "alo") == 3)
assert(m.match((("x" + #m.P(1))^-4 * m.S"al")^0, "alo") == 3)
isnullable("")
isnullable(m.P("x")^0)
isnullable(m.P("x")^-1)
isnullable(m.P("x") + 1 + 2 + m.P("a")^-1)
isnullable(-m.P("ab"))
isnullable(- -m.P("ab"))
isnullable(# #(m.P("ab") + "xy"))
isnullable(- #m.P("ab")^0)
isnullable(# -m.P("ab")^1)
isnullable(#m.V(3))
isnullable(m.V(3) + m.V(1) + m.P('a')^-1)
isnullable({[1] = m.V(2) * m.V(3), [2] = m.V(3), [3] = m.P(0)})
assert(m.match(m.P{[1] = m.V(2) * m.V(3), [2] = m.V(3), [3] = m.P(1)}^0, "abc")
       == 3)
assert(m.match(m.P""^-3, "a") == 1)

local function find (p, s)
  return m.match(basiclookfor(p), s)
end


local function badgrammar (g, expected)
  local stat, msg = pcall(m.P, g)
  assert(not stat)
  if expected then assert(find(expected, msg)) end
end

badgrammar({[1] = m.V(1)}, "rule '1'")
badgrammar({[1] = m.V(2)}, "rule '2'")   -- invalid non-terminal
badgrammar({[1] = m.V"x"}, "rule 'x'")   -- invalid non-terminal
badgrammar({[1] = m.V{}}, "rule '(a table)'")   -- invalid non-terminal
badgrammar({[1] = #m.P("a") * m.V(1)}, "rule '1'")  -- left-recursive
badgrammar({[1] = -m.P("a") * m.V(1)}, "rule '1'")  -- left-recursive
badgrammar({[1] = -1 * m.V(1)}, "rule '1'")  -- left-recursive
badgrammar({[1] = -1 + m.V(1)}, "rule '1'")  -- left-recursive
badgrammar({[1] = 1 * m.V(2), [2] = m.V(2)}, "rule '2'")  -- left-recursive
badgrammar({[1] = 1 * m.V(2)^0, [2] = m.P(0)}, "rule '1'")  -- inf. loop
badgrammar({ m.V(2), m.V(3)^0, m.P"" }, "rule '2'")  -- inf. loop
badgrammar({ m.V(2) * m.V(3)^0, m.V(3)^0, m.P"" }, "rule '1'")  -- inf. loop
badgrammar({"x", x = #(m.V(1) * 'a') }, "rule '1'")  -- inf. loop
badgrammar({ -(m.V(1) * 'a') }, "rule '1'")  -- inf. loop
badgrammar({"x", x = m.P'a'^-1 * m.V"x"}, "rule 'x'")  -- left recursive
badgrammar({"x", x = m.P'a' * m.V"y"^1, y = #m.P(1)}, "rule 'x'")

assert(m.match({'a' * -m.V(1)}, "aaa") == 2)
assert(m.match({'a' * -m.V(1)}, "aaaa") == nil)


-- good x bad grammars
m.P{ ('a' * m.V(1))^-1 }
m.P{ -('a' * m.V(1)) }
m.P{ ('abc' * m.V(1))^-1 }
m.P{ -('abc' * m.V(1)) }
badgrammar{ #m.P('abc') * m.V(1) }
badgrammar{ -('a' + m.V(1)) }
m.P{ #('a' * m.V(1)) }
badgrammar{ #('a' + m.V(1)) }
m.P{ m.B{ m.P'abc' } * 'a' * m.V(1) }
badgrammar{ m.B{ m.P'abc' } * m.V(1) }
badgrammar{ ('a' + m.P'bcd')^-1 * m.V(1) }


-- simple tests for maximum sizes:
local p = m.P"a"
for i=1,14 do p = p * p end

p = {}
for i=1,100 do p[i] = m.P"a" end
p = m.P(p)


-- strange values for rule labels

p = m.P{ "print",
     print = m.V(print),
     [print] = m.V(_G),
     [_G] = m.P"a",
   }

assert(p:match("a"))

-- initial rule
g = {}
for i = 1, 10 do g["i"..i] =  "a" * m.V("i"..i+1) end
g.i11 = m.P""
for i = 1, 10 do
  g[1] = "i"..i
  local p = m.P(g)
  assert(p:match("aaaaaaaaaaa") == 11 - i + 1)
end



print "testing back references"

checkerr("back reference 'x' not found", m.match, m.Cb('x'), '')
checkerr("back reference 'b' not found", m.match, m.Cg(1, 'a') * m.Cb('b'), 'a')

p = m.Cg(m.C(1) * m.C(1), "k") * m.Ct(m.Cb("k"))
t = p:match("ab")
checkeq(t, {"a", "b"})


do
  -- some basic cases
  assert(m.match(m.Cg(m.Cc(3), "a") * m.Cb("a"), "a") == 3)
  assert(m.match(m.Cg(m.C(1), 133) * m.Cb(133), "X") == "X")

  -- first reference to 'x' should not see the group enclosing it
  local p = m.Cg(m.Cb('x'), 'x') * m.Cb('x')
  checkerr("back reference 'x' not found", m.match, p, '')

  local p = m.Cg(m.Cb('x') * m.C(1), 'x') * m.Cb('x')
  checkerr("back reference 'x' not found", m.match, p, 'abc')

  -- reference to 'x' should not see the group enclosed in another capture
  local s = string.rep("a", 30)
  local p = (m.C(1)^-4 * m.Cg(m.C(1), 'x')) / {} * m.Cb('x')
  checkerr("back reference 'x' not found", m.match, p, s)

  local p = (m.C(1)^-20 * m.Cg(m.C(1), 'x')) / {} * m.Cb('x')
  checkerr("back reference 'x' not found", m.match, p, s)

  -- second reference 'k' should refer to 10 and first ref. 'k'
  p = m.Cg(m.Cc(20), 'k') * m.Cg(m.Cc(10) * m.Cb('k') * m.C(1), 'k')
      * (m.Cb('k') / function (a,b,c) return a*10 + b + tonumber(c) end)
  -- 10 * 10 (Cc) + 20 (Cb) + 7 (C) == 127
  assert(p:match("756") == 127)

end

p = m.P(true)
for i = 1, 10 do p = p * m.Cg(1, i) end
for i = 1, 10 do
  local p = p * m.Cb(i)
  assert(p:match('abcdefghij') == string.sub('abcdefghij', i, i))
end


t = {}
function foo (p) t[#t + 1] = p; return p .. "x" end

p = m.Cg(m.C(2)    / foo, "x") * m.Cb"x" *
    m.Cg(m.Cb('x') / foo, "x") * m.Cb"x" *
    m.Cg(m.Cb('x') / foo, "x") * m.Cb"x" *
    m.Cg(m.Cb('x') / foo, "x") * m.Cb"x"
x = {p:match'ab'}
checkeq(x, {'abx', 'abxx', 'abxxx', 'abxxxx'})
checkeq(t, {'ab',
            'ab', 'abx',
            'ab', 'abx', 'abxx',
            'ab', 'abx', 'abxx', 'abxxx'})



-- tests for match-time captures

p = m.P'a' * (function (s, i) return (s:sub(i, i) == 'b') and i + 1 end)
  + 'acd'

assert(p:match('abc') == 3)
assert(p:match('acd') == 4)

local function id (s, i, ...)
  return true, ...
end

do   -- run-time capture in an end predicate (should discard its value)
  local x = 0
  function foo (s, i)
      x = x + 1
      return true, x
  end

  local p = #(m.Cmt("", foo) * "xx") * m.Cmt("", foo)
  assert(p:match("xx") == 2)
end

assert(m.Cmt(m.Cs((m.Cmt(m.S'abc' / { a = 'x', c = 'y' }, id) +
              m.R'09'^1 /  string.char +
              m.P(1))^0), id):match"acb98+68c" == "xyb\98+\68y")

p = m.P{'S',
  S = m.V'atom' * space
    + m.Cmt(m.Ct("(" * space * (m.Cmt(m.V'S'^1, id) + m.P(true)) * ")" * space), id),
  atom = m.Cmt(m.C(m.R("AZ", "az", "09")^1), id)
}
x = p:match"(a g () ((b) c) (d (e)))"
checkeq(x, {'a', 'g', {}, {{'b'}, 'c'}, {'d', {'e'}}});

x = {(m.Cmt(1, id)^0):match(string.rep('a', 500))}
assert(#x == 500)

local function id(s, i, x)
  if x == 'a' then return i, 1, 3, 7
  else return nil, 2, 4, 6, 8
  end   
end     

p = ((m.P(id) * 1 + m.Cmt(2, id) * 1  + m.Cmt(1, id) * 1))^0
assert(table.concat{p:match('abababab')} == string.rep('137', 4))

local function ref (s, i, x)
  return m.match(x, s, i - x:len())
end

assert(m.Cmt(m.P(1)^0, ref):match('alo') == 4)
assert((m.P(1) * m.Cmt(m.P(1)^0, ref)):match('alo') == 4)
assert(not (m.P(1) * m.Cmt(m.C(1)^0, ref)):match('alo'))

ref = function (s,i,x) return i == tonumber(x) and i, 'xuxu' end

assert(m.Cmt(1, ref):match'2')
assert(not m.Cmt(1, ref):match'1')
assert(m.Cmt(m.P(1)^0, ref):match'03')

function ref (s, i, a, b)
  if a == b then return i, a:upper() end
end

p = m.Cmt(m.C(m.R"az"^1) * "-" * m.C(m.R"az"^1), ref)
p = (any - p)^0 * p * any^0 * -1

assert(p:match'abbbc-bc ddaa' == 'BC')

do   -- match-time captures cannot be optimized away
  local touch = 0
  f = m.P(function () touch = touch + 1; return true end)

  local function check(n) n = n or 1; assert(touch == n); touch = 0 end

  assert(m.match(f * false + 'b', 'a') == nil); check()
  assert(m.match(f * false + 'b', '') == nil); check()
  assert(m.match( (f * 'a')^0 * 'b', 'b') == 2); check()
  assert(m.match( (f * 'a')^0 * 'b', '') == nil); check()
  assert(m.match( (f * 'a')^-1 * 'b', 'b') == 2); check()
  assert(m.match( (f * 'a')^-1 * 'b', '') == nil); check()
  assert(m.match( ('b' + f * 'a')^-1 * 'b', '') == nil); check()
  assert(m.match( (m.P'b'^-1 * f * 'a')^-1 * 'b', '') == nil); check()
  assert(m.match( (-m.P(1) * m.P'b'^-1 * f * 'a')^-1 * 'b', '') == nil);
     check()
  assert(m.match( (f * 'a' + 'b')^-1 * 'b', '') == nil); check()
  assert(m.match(f * 'a' + f * 'b', 'b') == 2); check(2)
  assert(m.match(f * 'a' + f * 'b', 'a') == 2); check(1)
  assert(m.match(-f * 'a' + 'b', 'b') == 2); check(1)
  assert(m.match(-f * 'a' + 'b', '') == nil); check(1)
end

c = '[' * m.Cg(m.P'='^0, "init") * '[' *
    { m.Cmt(']' * m.C(m.P'='^0) * ']' * m.Cb("init"), function (_, _, s1, s2)
                                               return s1 == s2 end)
       + 1 * m.V(1) } / 0

assert(c:match'[==[]]====]]]]==]===[]' == 18)
assert(c:match'[[]=]====]=]]]==]===[]' == 14)
assert(not c:match'[[]=]====]=]=]==]===[]')


-- old bug: optimization of concat with fail removed match-time capture
p = m.Cmt(0, function (s) p = s end) * m.P(false)
assert(not p:match('alo'))
assert(p == 'alo')


-- ensure that failed match-time captures are not kept on Lua stack
do
  local t = {__mode = "kv"}; setmetatable(t,t)
  local c = 0

  local function foo (s,i)
    collectgarbage();
    assert(next(t) == "__mode" and next(t, "__mode") == nil)
    local x = {}
    t[x] = true
    c = c + 1
    return i, x
  end

  local p = m.P{ m.Cmt(0, foo) * m.P(false) + m.P(1) * m.V(1) + m.P"" }
  p:match(string.rep('1', 10))
  assert(c == 11)
end


-- Return a match-time capture that returns 'n' captures
local function manyCmt (n)
    return m.Cmt("a", function ()
             local a = {}; for i = 1, n do a[i] = n - i end
             return true, unpack(a)
           end)
end

-- bug in 1.0: failed match-time that used previous match-time results
do
  local x
  local function aux (...) x = #{...}; return false end
  local res = {m.match(m.Cmt(manyCmt(20), aux) + manyCmt(10), "a")}
  assert(#res == 10 and res[1] == 9 and res[10] == 0)
end


-- bug in 1.0: problems with math-times returning too many captures
if _VERSION >= "Lua 5.2" then
  local lim = 2^11 - 10
  local res = {m.match(manyCmt(lim), "a")}
  assert(#res == lim and res[1] == lim - 1 and res[lim] == 0)
  checkerr("too many", m.match, manyCmt(2^15), "a")
end

p = (m.P(function () return true, "a" end) * 'a'
  + m.P(function (s, i) return i, "aa", 20 end) * 'b'
  + m.P(function (s,i) if i <= #s then return i, "aaa" end end) * 1)^0

t = {p:match('abacc')}
checkeq(t, {'a', 'aa', 20, 'a', 'aaa', 'aaa'})


do  print"testing large grammars"
  local lim = 1000    -- number of rules
  local t = {}

  for i = 3, lim do
    t[i] = m.V(i - 1)   -- each rule calls previous one
  end
  t[1] = m.V(lim)    -- start on last rule
  t[2] = m.C("alo")  -- final rule

  local P = m.P(t)   -- build grammar
  assert(P:match("alo") == "alo")

  t[#t + 1] = m.P("x")   -- one more rule...
  checkerr("too many rules", m.P, t)
end


print "testing UTF-8 ranges"

do   -- a few typical UTF-8 ranges
  local p = m.utfR(0x410, 0x44f)^1 / "cyr: %0"
          + m.utfR(0x4e00, 0x9fff)^1 / "cjk: %0"
          + m.utfR(0x1F600, 0x1F64F)^1 / "emot: %0"
          + m.utfR(0, 0x7f)^1 / "ascii: %0"
          + m.utfR(0, 0x10ffff) / "other: %0"

  p = m.Ct(p^0) * -m.P(1)

  local cyr = "ждюя"
  local emot = "\240\159\152\128\240\159\153\128"   --  😀🙀
  local cjk = "专举乸"
  local ascii = "alo"
  local last = "\244\143\191\191"                -- U+10FFFF

  local s = cyr .. "—" .. emot .. "—" .. cjk .. "—" .. ascii .. last
  t = (p:match(s))

  assert(t[1] == "cyr: " .. cyr and t[2] == "other: —" and
         t[3] == "emot: " .. emot and t[4] == "other: —" and
         t[5] == "cjk: " .. cjk and t[6] == "other: —" and
         t[7] == "ascii: " .. ascii and t[8] == "other: " .. last and
         t[9] == nil)

  -- failing UTF-8 matches and borders
  assert(not m.match(m.utfR(10, 0x2000), "\9"))
  assert(not m.match(m.utfR(10, 0x2000), "\226\128\129"))
  assert(m.match(m.utfR(10, 0x2000), "\10") == 2)
  assert(m.match(m.utfR(10, 0x2000), "\226\128\128") == 4)
end


do   -- valid and invalid code points
  local p = m.utfR(0, 0x10ffff)^0
  assert(p:match("汉字\128") == #"汉字" + 1)
  assert(p:match("\244\159\191") == 1)
  assert(p:match("\244\159\191\191") == 1)
  assert(p:match("\255") == 1)

   -- basic errors
  checkerr("empty range", m.utfR, 1, 0)
  checkerr("invalid code point", m.utfR, 1, 0x10ffff + 1)
end


do  -- back references (fixed width)
  -- match a byte after a CJK point
  local p = m.B(m.utfR(0x4e00, 0x9fff)) * m.C(1)
  p = m.P{ p + m.P(1) * m.V(1) }   -- search for 'p'
  assert(p:match("ab д 专X x") == "X")

  -- match a byte after a hebrew point
  local p = m.B(m.utfR(0x5d0, 0x5ea)) * m.C(1)
  p = m.P(#"ש") * p
  assert(p:match("שX") == "X")

  checkerr("fixed length", m.B, m.utfR(0, 0x10ffff))
end



-------------------------------------------------------------------
-- Tests for 're' module
-------------------------------------------------------------------
print"testing 're' module"

local re = require "re"

local match, compile = re.match, re.compile



assert(match("a", ".") == 2)
assert(match("a", "''") == 1)
assert(match("", " ! . ") == 1)
assert(not match("a", " ! . "))
assert(match("abcde", "  ( . . ) * ") == 5)
assert(match("abbcde", " [a-c] +") == 5)
assert(match("0abbc1de", "'0' [a-c]+ '1'") == 7)
assert(match("0zz1dda", "'0' [^a-c]+ 'a'") == 8)
assert(match("abbc--", " [a-c] + +") == 5)
assert(match("abbc--", " [ac-] +") == 2)
assert(match("abbc--", " [-acb] + ") == 7)
assert(not match("abbcde", " [b-z] + "))
assert(match("abb\"de", '"abb"["]"de"') == 7)
assert(match("abceeef", "'ac' ? 'ab' * 'c' { 'e' * } / 'abceeef' ") == "eee")
assert(match("abceeef", "'ac'? 'ab'* 'c' { 'f'+ } / 'abceeef' ") == 8)

assert(re.match("aaand", "[a]^2") == 3)

local t = {match("abceefe", "( ( & 'e' {} ) ? . ) * ")}
checkeq(t, {4, 5, 7})
local t = {match("abceefe", "((&&'e' {})? .)*")}
checkeq(t, {4, 5, 7})
local t = {match("abceefe", "( ( ! ! 'e' {} ) ? . ) *")}
checkeq(t, {4, 5, 7})
local t = {match("abceefe", "(( & ! & ! 'e' {})? .)*")}
checkeq(t, {4, 5, 7})

assert(match("cccx" , "'ab'? ('ccc' / ('cde' / 'cd'*)? / 'ccc') 'x'+") == 5)
assert(match("cdx" , "'ab'? ('ccc' / ('cde' / 'cd'*)? / 'ccc') 'x'+") == 4)
assert(match("abcdcdx" , "'ab'? ('ccc' / ('cde' / 'cd'*)? / 'ccc') 'x'+") == 8)

assert(match("abc", "a <- (. a)?") == 4)
b = "balanced <- '(' ([^()] / balanced)* ')'"
assert(match("(abc)", b))
assert(match("(a(b)((c) (d)))", b))
assert(not match("(a(b ((c) (d)))", b))

b = compile[[  balanced <- "(" ([^()] / balanced)* ")" ]]
assert(b == m.P(b))
assert(b:match"((((a))(b)))")

local g = [[
  S <- "0" B / "1" A / ""   -- balanced strings
  A <- "0" S / "1" A A      -- one more 0
  B <- "1" S / "0" B B      -- one more 1
]]
assert(match("00011011", g) == 9)

local g = [[
  S <- ("0" B / "1" A)*
  A <- "0" / "1" A A
  B <- "1" / "0" B B
]]
assert(match("00011011", g) == 9)
assert(match("000110110", g) == 9)
assert(match("011110110", g) == 3)
assert(match("000110010", g) == 1)

s = "aaaaaaaaaaaaaaaaaaaaaaaa"
assert(match(s, "'a'^3") == 4)
assert(match(s, "'a'^0") == 1)
assert(match(s, "'a'^+3") == s:len() + 1)
assert(not match(s, "'a'^+30"))
assert(match(s, "'a'^-30") == s:len() + 1)
assert(match(s, "'a'^-5") == 6)
for i = 1, s:len() do
  assert(match(s, string.format("'a'^+%d", i)) >= i + 1)
  assert(match(s, string.format("'a'^-%d", i)) <= i + 1)
  assert(match(s, string.format("'a'^%d", i)) == i + 1)
end
assert(match("01234567890123456789", "[0-9]^3+") == 19)


assert(match("01234567890123456789", "({....}{...}) -> '%2%1'") == "4560123")
t = match("0123456789", "{| {.}* |}")
checkeq(t, {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"})
assert(match("012345", "{| (..) -> '%0%0' |}")[1] == "0101")

assert(match("abcdef", "( {.} {.} {.} {.} {.} ) -> 3") == "c")
assert(match("abcdef", "( {:x: . :} {.} {.} {.} {.} ) -> 3") == "d")
assert(match("abcdef", "( {:x: . :} {.} {.} {.} {.} ) -> 0") == 6)

assert(not match("abcdef", "{:x: ({.} {.} {.}) -> 2 :} =x"))
assert(match("abcbef", "{:x: ({.} {.} {.}) -> 2 :} =x"))

eqcharset(compile"[]]", "]")
eqcharset(compile"[][]", m.S"[]")
eqcharset(compile"[]-]", m.S"-]")
eqcharset(compile"[-]", m.S"-")
eqcharset(compile"[az-]", m.S"a-z")
eqcharset(compile"[-az]", m.S"a-z")
eqcharset(compile"[a-z]", m.R"az")
eqcharset(compile"[]['\"]", m.S[[]['"]])

eqcharset(compile"[^]]", any - "]")
eqcharset(compile"[^][]", any - m.S"[]")
eqcharset(compile"[^]-]", any - m.S"-]")
eqcharset(compile"[^]-]", any - m.S"-]")
eqcharset(compile"[^-]", any - m.S"-")
eqcharset(compile"[^az-]", any - m.S"a-z")
eqcharset(compile"[^-az]", any - m.S"a-z")
eqcharset(compile"[^a-z]", any - m.R"az")
eqcharset(compile"[^]['\"]", any - m.S[[]['"]])

-- tests for comments in 're'
e = compile[[
A  <- _B   -- \t \n %nl .<> <- -> --
_B <- 'x'  --]]
assert(e:match'xy' == 2)

-- tests for 're' with pre-definitions
defs = {digits = m.R"09", letters = m.R"az", _=m.P"__"}
e = compile("%letters (%letters / %digits)*", defs)
assert(e:match"x123" == 5)
e = compile("%_", defs)
assert(e:match"__" == 3)

e = compile([[
  S <- A+
  A <- %letters+ B
  B <- %digits+
]], defs)

e = compile("{[0-9]+'.'?[0-9]*} -> sin", math)
assert(e:match("2.34") == math.sin(2.34))

e = compile("'pi' -> math", _G)
assert(e:match("pi") == math.pi)

e = compile("[ ]* 'version' -> _VERSION", _G)
assert(e:match("  version") == _VERSION)


function eq (_, _, a, b) return a == b end

c = re.compile([[
  longstring <- '[' {:init: '='* :} '[' close
  close <- ']' =init ']' / . close
]])

assert(c:match'[==[]]===]]]]==]===[]' == 17)
assert(c:match'[[]=]====]=]]]==]===[]' == 14)
assert(not c:match'[[]=]====]=]=]==]===[]')

c = re.compile" '[' {:init: '='* :} '[' (!(']' =init ']') .)* ']' =init ']' !. "

assert(c:match'[==[]]===]]]]==]')
assert(c:match'[[]=]====]=][]==]===[]]')
assert(not c:match'[[]=]====]=]=]==]===[]')

assert(re.find("hi alalo", "{:x:..:} =x") == 4)
assert(re.find("hi alalo", "{:x:..:} =x", 4) == 4)
assert(not re.find("hi alalo", "{:x:..:} =x", 5))
assert(re.find("hi alalo", "{'al'}", 5) == 6)
assert(re.find("hi aloalolo", "{:x:..:} =x") == 8)
assert(re.find("alo alohi x x", "{:word:%w+:}%W*(=word)!%w") == 11)

-- re.find discards any captures
local a,b,c = re.find("alo", "{.}{'o'}")
assert(a == 2 and b == 3 and c == nil)

local function match (s,p)
  local i,e = re.find(s,p)
  if i then return s:sub(i, e) end
end
assert(match("alo alo", '[a-z]+') == "alo")
assert(match("alo alo", '{:x: [a-z]+ :} =x') == nil)
assert(match("alo alo", "{:x: [a-z]+ :} ' ' =x") == "alo alo")

assert(re.gsub("alo alo", "[abc]", "x") == "xlo xlo")
assert(re.gsub("alo alo", "%w+", ".") == ". .")
assert(re.gsub("hi, how are you", "[aeiou]", string.upper) ==
               "hI, hOw ArE yOU")

s = 'hi [[a comment[=]=] ending here]] and [=[another]]=]]'
c = re.compile" '[' {:i: '='* :} '[' (!(']' =i ']') .)* ']' { =i } ']' "
assert(re.gsub(s, c, "%2") == 'hi  and =]')
assert(re.gsub(s, c, "%0") == s)
assert(re.gsub('[=[hi]=]', c, "%2") == '=')

assert(re.find("", "!.") == 1)
assert(re.find("alo", "!.") == 4)

function addtag (s, i, t, tag) t.tag = tag; return i, t end

c = re.compile([[
  doc <- block !.
  block <- (start {| (block / { [^<]+ })* |} end?) => addtag
  start <- '<' {:tag: [a-z]+ :} '>'
  end <- '</' { =tag } '>'
]], {addtag = addtag})

x = c:match[[
<x>hi<b>hello</b>but<b>totheend</x>]]
checkeq(x, {tag='x', 'hi', {tag = 'b', 'hello'}, 'but',
                     {'totheend'}})


-- test for folding captures
c = re.compile([[
  S <- (number (%s+ number)*) ~> add
  number <- %d+ -> tonumber
]], {tonumber = tonumber, add = function (a,b) return a + b end})
assert(c:match("3 401 50") == 3 + 401 + 50)

-- test for accumulator captures
c = re.compile([[
  S <- number (%s+ number >> add)*
  number <- %d+ -> tonumber
]], {tonumber = tonumber, add = function (a,b) return a + b end})
assert(c:match("3 401 50") == 3 + 401 + 50)

-- tests for look-ahead captures
x = {re.match("alo", "&(&{.}) !{'b'} {&(...)} &{..} {...} {!.}")}
checkeq(x, {"", "alo", ""})

assert(re.match("aloalo",
   "{~ (((&'al' {.}) -> 'A%1' / (&%l {.}) -> '%1%1') / .)* ~}")
       == "AallooAalloo")

-- bug in 0.9 (and older versions), due to captures in look-aheads
x = re.compile[[   {~ (&(. ([a-z]* -> '*')) ([a-z]+ -> '+') ' '*)* ~}  ]]
assert(x:match"alo alo" == "+ +")

-- valid capture in look-ahead (used inside the look-ahead itself)
x = re.compile[[
      S <- &({:two: .. :} . =two) {[a-z]+} / . S
]]
assert(x:match("hello aloaLo aloalo xuxu") == "aloalo")


p = re.compile[[
  block <- {| {:ident:space*:} line
           ((=ident !space line) / &(=ident space) block)* |}
  line <- {[^%nl]*} %nl
  space <- '_'     -- should be ' ', but '_' is simpler for editors
]]

t= p:match[[
1
__1.1
__1.2
____1.2.1
____
2
__2.1
]]
checkeq(t, {"1", {"1.1", "1.2", {"1.2.1", "", ident = "____"}, ident = "__"},
            "2", {"2.1", ident = "__"}, ident = ""})


-- nested grammars
p = re.compile[[
       s <- a b !.
       b <- ( x <- ('b' x)? )
       a <- ( x <- 'a' x? )
]]

assert(p:match'aaabbb')
assert(p:match'aaa')
assert(not p:match'bbb')
assert(not p:match'aaabbba')

-- testing groups
t = {re.match("abc", "{:S <- {:.:} {S} / '':}")}
checkeq(t, {"a", "bc", "b", "c", "c", ""})

t = re.match("1234", "{| {:a:.:} {:b:.:} {:c:.{.}:} |}")
checkeq(t, {a="1", b="2", c="4"})
t = re.match("1234", "{|{:a:.:} {:b:{.}{.}:} {:c:{.}:}|}")
checkeq(t, {a="1", b="2", c="4"})
t = re.match("12345", "{| {:.:} {:b:{.}{.}:} {:{.}{.}:} |}")
checkeq(t, {"1", b="2", "4", "5"})
t = re.match("12345", "{| {:.:} {:{:b:{.}{.}:}:} {:{.}{.}:} |}")
checkeq(t, {"1", "23", "4", "5"})
t = re.match("12345", "{| {:.:} {{:b:{.}{.}:}} {:{.}{.}:} |}")
checkeq(t, {"1", "23", "4", "5"})


-- testing pre-defined names
assert(os.setlocale("C") == "C")

function eqlpeggsub (p1, p2)
  local s1 = cs2str(re.compile(p1))
  local s2 = string.gsub(allchar, "[^" .. p2 .. "]", "")
  -- if s1 ~= s2 then print(#s1,#s2) end
  assert(s1 == s2)
end


eqlpeggsub("%w", "%w")
eqlpeggsub("%a", "%a")
eqlpeggsub("%l", "%l")
eqlpeggsub("%u", "%u")
eqlpeggsub("%p", "%p")
eqlpeggsub("%d", "%d")
eqlpeggsub("%x", "%x")
eqlpeggsub("%s", "%s")
eqlpeggsub("%c", "%c")

eqlpeggsub("%W", "%W")
eqlpeggsub("%A", "%A")
eqlpeggsub("%L", "%L")
eqlpeggsub("%U", "%U")
eqlpeggsub("%P", "%P")
eqlpeggsub("%D", "%D")
eqlpeggsub("%X", "%X")
eqlpeggsub("%S", "%S")
eqlpeggsub("%C", "%C")

eqlpeggsub("[%w]", "%w")
eqlpeggsub("[_%w]", "_%w")
eqlpeggsub("[^%w]", "%W")
eqlpeggsub("[%W%S]", "%W%S")

re.updatelocale()


-- testing nested substitutions x string captures

p = re.compile[[
      text <- {~ item* ~}
      item <- macro / [^()] / '(' item* ')'
      arg <- ' '* {~ (!',' item)* ~}
      args <- '(' arg (',' arg)* ')'
      macro <- ('apply' args) -> '%1(%2)'
             / ('add' args) -> '%1 + %2'
             / ('mul' args) -> '%1 * %2'
]]

assert(p:match"add(mul(a,b), apply(f,x))" == "a * b + f(x)")

rev = re.compile[[ R <- (!.) -> '' / ({.} R) -> '%2%1']]

assert(rev:match"0123456789" == "9876543210")


-- testing error messages in re

local function errmsg (p, err)
  checkerr(err, re.compile, p)
end

errmsg('aaaa', "rule 'aaaa'")
errmsg('a', 'outside')
errmsg('b <- a', 'undefined')
errmsg("x <- 'a'  x <- 'b'", 'already defined')
errmsg("'a' -", "near '-'")


print"OK"


