local skynet = require "skynet"

print("Log server start")

skynet.launch("snlua","globallog")

skynet.exit()
