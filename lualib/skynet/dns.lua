--[[
	lua dns resolver library
	See  https://github.com/xjdrew/levent/blob/master/levent/dns.lua for more detail

-- resource record type:
-- TYPE            value and meaning
-- A               1 a host address
-- NS              2 an authoritative name server
-- MD              3 a mail destination (Obsolete - use MX)
-- MF              4 a mail forwarder (Obsolete - use MX)
-- CNAME           5 the canonical name for an alias
-- SOA             6 marks the start of a zone of authority
-- MB              7 a mailbox domain name (EXPERIMENTAL)
-- MG              8 a mail group member (EXPERIMENTAL)
-- MR              9 a mail rename domain name (EXPERIMENTAL)
-- NULL            10 a null RR (EXPERIMENTAL)
-- WKS             11 a well known service description
-- PTR             12 a domain name pointer
-- HINFO           13 host information
-- MINFO           14 mailbox or mail list information
-- MX              15 mail exchange
-- TXT             16 text strings
-- AAAA            28 a ipv6 host address
-- only appear in the question section:
-- AXFR            252 A request for a transfer of an entire zone
-- MAILB           253 A request for mailbox-related records (MB, MG or MR)
-- MAILA           254 A request for mail agent RRs (Obsolete - see MX)
-- *               255 A request for all records
--
-- resource recode class:
-- IN              1 the Internet
-- CS              2 the CSNET class (Obsolete - used only for examples in some obsolete RFCs)
-- CH              3 the CHAOS class
-- HS              4 Hesiod [Dyer 87]
-- only appear in the question section:
-- *               255 any class
-- ]]

--[[
-- struct header {
--  uint16_t tid     # identifier assigned by the program that generates any kind of query.
--  uint16_t flags   # flags
--  uint16_t qdcount # the number of entries in the question section.
--  uint16_t ancount # the number of resource records in the answer section.
--  uint16_t nscount # the number of name server resource records in the authority records section.
--  uint16_t arcount # the number of resource records in the additional records section.
-- }
--
-- request body:
-- struct request {
--  string name
--  uint16_t atype
--  uint16_t class
-- }
--
-- response body:
-- struct response {
--  string name
--  uint16_t atype
--  uint16_t class
--  uint16_t ttl
--  uint16_t rdlength
--  string rdata
-- }
--]]

local skynet = require "skynet"
local socket = require "skynet.socket"

local MAX_DOMAIN_LEN = 1024
local MAX_LABEL_LEN = 63
local MAX_PACKET_LEN = 2048
local DNS_HEADER_LEN = 12
local TIMEOUT = 30 * 100	-- 30 seconds

local QTYPE = {
	A = 1,
	CNAME = 5,
	AAAA = 28,
}

local QCLASS = {
	IN = 1,
}

local weak = {__mode = "kv"}
local CACHE = {}

local dns = {}
local request_pool = {}
local local_hosts -- local static table lookup for hostnames

dns.DEFAULT_HOSTS = "/etc/hosts"
dns.DEFAULT_RESOLV_CONF = "/etc/resolv.conf"

-- return name type: 'ipv4', 'ipv6', or 'hostname'
local function guess_name_type(name)
	if name:match("^[%d%.]+$") then
		return "ipv4"
	end

	if name:find(":") then
		return "ipv6"
	end

	return "hostname"
end

-- http://man7.org/linux/man-pages/man5/hosts.5.html
local function parse_hosts()
	if not dns.DEFAULT_HOSTS then
		return
	end

	local f = io.open(dns.DEFAULT_HOSTS)
	if not f then
		return
	end

	local rts = {}
	for line in f:lines() do
		local ip, hosts = string.match(line, "^%s*([%[%]%x%.%:]+)%s+([^#;]+)")
		if not ip or not hosts then
			goto continue
		end

		local family = guess_name_type(ip)
		if family == "hostname" then
			goto continue
		end

		for host in hosts:gmatch("%S+") do
			host = host:lower()
			local rt = rts[host]
			if not rt then
				rt = {}
				rts[host] = rt
			end

			if not rt[family] then
				rt[family] = {}
			end
			table.insert(rt[family], ip)
		end
		
		::continue::
	end
	return rts
end

-- http://man7.org/linux/man-pages/man5/resolv.conf.5.html
local function parse_resolv_conf()
	if not dns.DEFAULT_RESOLV_CONF then
		return
	end

	local f = io.open(dns.DEFAULT_RESOLV_CONF)
	if not f then
		return
	end

	local server
	for line in f:lines() do
		server = line:match("^%s*nameserver%s+([^#;%s]+)")
		if server then
			break
		end
	end
	f:close()
	return server
end

function dns.flush()
	CACHE[QTYPE.A] = setmetatable({},weak)
	CACHE[QTYPE.AAAA] = setmetatable({},weak)
end

dns.flush()

local function verify_domain_name(name)
	if #name > MAX_DOMAIN_LEN then
		return false
	end
	if not name:match("^[_%l%d%-%.]+$") then
		return false
	end
	for w in name:gmatch("([_%w%-]+)%.?") do
		if #w > MAX_LABEL_LEN then
			return false
		end
	end
	return true
end

local next_tid = 1
local function gen_tid()
	local tid = next_tid
	if request_pool[tid] then
		tid = nil
		for i = 1, 65535 do
			-- find available tid
			if not request_pool[i] then
				tid = i
				break
			end
		end
		assert(tid)
	end
	next_tid = tid + 1
	if next_tid > 65535 then
		next_tid = 1
	end
	return tid
end

local function pack_header(t)
	return string.pack(">HHHHHH",
		t.tid, t.flags, t.qdcount, t.ancount or 0, t.nscount or 0, t.arcount or 0)
end

local function pack_question(name, qtype, qclass)
	local labels = {}
	for w in name:gmatch("([_%w%-]+)%.?") do
		table.insert(labels, string.pack("s1",w))
	end
	table.insert(labels, '\0')
	table.insert(labels, string.pack(">HH", qtype, qclass))
	return table.concat(labels)
end

local function unpack_header(chunk)
	local tid, flags, qdcount, ancount, nscount, arcount, left = string.unpack(">HHHHHH", chunk)
	return {
		tid = tid,
		flags = flags,
		qdcount = qdcount,
		ancount = ancount,
		nscount = nscount,
		arcount = arcount
	}, left
end

-- unpack a resource name
local function unpack_name(chunk, left)
	local t = {}
	local jump_pointer
	local tag, offset, label
	while true do
		tag, left = string.unpack("B", chunk, left)
		if tag & 0xc0 == 0xc0 then
			-- pointer
			offset,left = string.unpack(">H", chunk, left - 1)
			offset = offset & 0x3fff
			if not jump_pointer then
				jump_pointer = left
			end
			-- offset is base 0, need to plus 1
			left = offset + 1
		elseif tag == 0 then
			break
		else
			label, left = string.unpack("s1", chunk, left - 1)
			t[#t+1] = label
		end
	end
	return table.concat(t, "."), jump_pointer or left
end

local function unpack_question(chunk, left)
	local name, left = unpack_name(chunk, left)
	local atype, class, left = string.unpack(">HH", chunk, left)
	return {
		name = name,
		atype = atype,
		class = class
	}, left
end

local function unpack_answer(chunk, left)
	local name, left = unpack_name(chunk, left)
	local atype, class, ttl, rdata, left = string.unpack(">HHI4s2", chunk, left)
	return {
		name = name,
		atype = atype,
		class = class,
		ttl = ttl,
		rdata = rdata
	},left
end

local function unpack_rdata(qtype, chunk)
	if qtype == QTYPE.A then
		local a,b,c,d = string.unpack("BBBB", chunk)
		return string.format("%d.%d.%d.%d", a,b,c,d)
	elseif qtype == QTYPE.AAAA then
		local a,b,c,d,e,f,g,h = string.unpack(">HHHHHHHH", chunk)
		return string.format("%x:%x:%x:%x:%x:%x:%x:%x", a, b, c, d, e, f, g, h)
	else
		error("Error qtype " .. qtype)
	end
end

local dns_server = {
	fd = nil,
	address = nil,
	port = nil,
	retire = nil,
}

local function resolve(content)
	if #content < DNS_HEADER_LEN then
		-- drop
		skynet.error("Recv an invalid package when dns query")
		return
	end
	local answer_header,left = unpack_header(content)
	-- verify answer
	assert(answer_header.qdcount == 1, "malformed packet")

	local question,left = unpack_question(content, left)

	local ttl
	local answer
	local answers_ipv4
	local answers_ipv6

	for i=1, answer_header.ancount do
		answer, left = unpack_answer(content, left)
		local answers
		if answer.atype == QTYPE.A then
			answers_ipv4 = answers_ipv4 or {}
			answers = answers_ipv4
		elseif answer.atype == QTYPE.AAAA then
			answers_ipv6 = answers_ipv6 or {}
			answers = answers_ipv6
		end
		if answers then
			local ip = unpack_rdata(answer.atype, answer.rdata)
			ttl = ttl and math.min(ttl, answer.ttl) or answer.ttl
			answers[#answers+1] = ip
		end
	end

	if answers_ipv4 then
		CACHE[QTYPE.A][question.name] = { answers = answers_ipv4, ttl = skynet.now() + ttl * 100 }
	end

	if answers_ipv6 then
		CACHE[QTYPE.AAAA][question.name] = { answers = answers_ipv6, ttl = skynet.now() + ttl * 100 }
	end

	local resp = request_pool[answer_header.tid]
	if not resp then
		-- the resp may be timeout
		return
	end

	if question.name ~= resp.name then
		skynet.error("Recv an invalid name when dns query")
	end

	local r = CACHE[resp.qtype][resp.name]
	if r then
		resp.answers = r.answers
	end

	skynet.wakeup(resp.co)
end

local function connect_server()
	local fd = socket.udp(function(str, from)
		resolve(str)
	end)

	if not dns_server.address then
		dns_server.address = parse_resolv_conf()
		dns_server.port = 53
	end

	assert(dns_server.address, "Call dns.server first")

	local ok, err = pcall(socket.udp_connect,fd, dns_server.address, dns_server.port)
	if not ok then
		socket.close(fd)
		error(err)
	end

	dns_server.fd = fd
	skynet.error(string.format("Udp server open %s:%s (%d)", dns_server.address, dns_server.port, fd))
end

local DNS_SERVER_RETIRE = 60 * 100
local function touch_server()
	dns_server.retire = skynet.now()
	if dns_server.fd then
		return
	end

	connect_server()

	local function check_alive()
		if skynet.now() > dns_server.retire + DNS_SERVER_RETIRE then
			local fd = dns_server.fd
			if fd then
				dns_server.fd = nil
				socket.close(fd)
				skynet.error(string.format("Udp server close %s:%s (%d)", dns_server.address, dns_server.port, fd))
			end
		else
			skynet.timeout( 2 * DNS_SERVER_RETIRE, check_alive)
		end
	end

	skynet.timeout( 2 * DNS_SERVER_RETIRE, check_alive)
end

function dns.server(server, port)
	dns_server.address = server
	dns_server.port = port or 53
end

local function lookup_cache(name, qtype, ignorettl)
	local result = CACHE[qtype][name]
	if result then
		if ignorettl or (result.ttl > skynet.now()) then
			return result.answers
		end
	end
end

local function suspend(tid, name, qtype)
	local req = {
		name = name,
		tid = tid,
		qtype = qtype,
		co = coroutine.running(),
	}
	request_pool[tid] = req
	skynet.fork(function()
		skynet.sleep(TIMEOUT)
		local req = request_pool[tid]
		if req then
			-- cancel tid
			skynet.error(string.format("DNS query %s timeout", name))
			request_pool[tid] = nil
			skynet.wakeup(req.co)
		end
	end)
	skynet.wait(req.co)
	request_pool[tid] = nil
	if not req.answers then
		local answers = lookup_cache(name, qtype, true)
		if answers then
			return answers[1], answers
		end
		error "timeout or no answer"
	end
	return req.answers[1], req.answers
end

-- lookup local static table
local function local_resolve(name, ipv6)
	if not local_hosts then
		local_hosts = parse_hosts()
	end

	if not local_hosts then
		return
	end

	local family = ipv6 and "ipv6" or "ipv4"
	local t = local_hosts[name]
	if t then
		local answers = t[family]
		if answers then
			return answers[1], answers
		end
	end
	return nil
end

-- lookup dns server
local function remote_resolve(name, ipv6)
	local qtype = ipv6 and QTYPE.AAAA or QTYPE.A
	local answers = lookup_cache(name, qtype)
	if answers then
		return answers[1], answers
	end
	local question_header = {
		tid = gen_tid(),
		flags = 0x100, -- flags: 00000001 00000000, set RD
		qdcount = 1,
	}
	local req = pack_header(question_header) .. pack_question(name, qtype, QCLASS.IN)
	touch_server()
	socket.write(dns_server.fd, req)
	return suspend(question_header.tid, name, qtype)
end

function dns.resolve(name, ipv6)
	local name = name:lower()
	local ntype = guess_name_type(name)
	if ntype ~= "hostname" then
		if (ipv6 and name == "ipv4") or (not ipv6 and name == "ipv6") then
			return nil, "illegal ip address"
		end
		return name
	end

	if not verify_domain_name(name) then
		return nil, "illegal name"
	end

	local answer, answers = local_resolve(name, ipv6)
	if answer then
		return answer, answers
	end

	return remote_resolve(name, ipv6)
end

return dns
