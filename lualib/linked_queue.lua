local Queue = {}

function Queue.new()
    local obj = setmetatable({}, {__index = Queue})
    obj.head = nil
    obj.tail = nil
    obj.size = 0
    return obj
end


-- 出队列
function Queue:pop()
    if self:isEmpty() then
        return nil
    end
    local result = self.head
    self.head = self.head.next
	self.size = self.size - 1
    return result.elem
end

function Queue:isEmpty()
    return self.head == nil
end

-- 入队列
function Queue:push(value)
    local newNode = {elem = value, next = nil}
    if self:isEmpty() then
        self.head = newNode
        self.tail = newNode
    else
        self.tail.next = newNode
        self.tail = newNode
    end
    self.size = self.size + 1
end

function Queue:size()
    return self.size
end

return Queue