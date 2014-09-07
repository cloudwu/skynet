local sprotoparser = require "sprotoparser"

local proto = sprotoparser.parse [[
.package {
	type 0 : integer
	session 1 : integer
}

handshake 1 {
	response {
		msg 0  : string
	}
}

get 2 {
	request {
		what 0 : string
	}
	response {
		result 0 : boolean
	}
}

set 3 {
	request {
		what 0 : string
		value 1 : string
	}
	response {
		ok 0 : boolean
	}
}

]]

return proto
