local path = assert(arg[1], "missing generated VM path")
local input = assert(io.open(path, "rb"))
local source = input:read("*a")
input:close()

local function programWords(wrapper)
    local blockStart, blockEnd, body = wrapper:find(
        "local%s+l[%w]+%s*=%s*{%s*([%d,%s]+)%s*}%s*local%s+l[%w]+%s*=%s*{}")
    assert(blockStart and body, "could not locate the VM instruction stream")
    local bodyStart, bodyEnd = wrapper:find(body, blockStart, true)
    assert(bodyStart and bodyEnd and bodyEnd <= blockEnd, "could not locate VM instruction bytes")
    local words = {}
    for value in body:gmatch("%d+") do
        words[#words + 1] = assert(tonumber(value))
    end
    assert(#words > 1, "VM instruction stream is unexpectedly short")
    return words, bodyStart, bodyEnd
end

local function replaceProgram(wrapper, mutate)
    local words, bodyStart, bodyEnd = programWords(wrapper)
    mutate(words)
    return wrapper:sub(1, bodyStart - 1) .. table.concat(words, ",") .. wrapper:sub(bodyEnd + 1)
end

local function expectIntegrity(name, wrapper)
    local variantPath = path .. "." .. name .. ".lua"
    local output = assert(io.open(variantPath, "wb"))
    output:write(wrapper)
    output:close()

    local chunk, loadError = loadfile(variantPath)
    assert(chunk, loadError)
    local ok, runtimeError = pcall(chunk)
    os.remove(variantPath)
    assert(not ok, name .. ": tampered VM unexpectedly executed")
    assert(tostring(runtimeError):find("integrity:vm", 1, true), name .. ": " .. tostring(runtimeError))
end

expectIntegrity("unknown-opcode", replaceProgram(source, function(words)
    words[1] = 0
end))

expectIntegrity("missing-halt", replaceProgram(source, function(words)
    table.remove(words)
end))

expectIntegrity("duplicate-halt", replaceProgram(source, function(words)
    words[#words + 1] = words[#words]
end))

expectIntegrity("instruction-after-halt", replaceProgram(source, function(words)
    words[#words + 1] = words[1]
end))

local emitOpcode = assert(tonumber(source:match("elseif%s+l[%w]+==(%d+)%s+then%s+local e=")),
    "could not locate EMIT opcode")
expectIntegrity("payload-checksum", replaceProgram(source, function(words)
    for index, word in ipairs(words) do
        if math.floor(word / 16777216) % 256 == emitOpcode then
            local encoded = math.floor(word / 65536) % 256
            words[index] = encoded == 255 and word - 65536 or word + 65536
            return
        end
    end
    error("could not locate an EMIT instruction")
end))

local lengthMutations = 0
local badLength = source:gsub("(if%s+#l[%w]+~=)(%d+)(%s+then%s+error%(%\"integrity:vm%\",0%))", function(prefix, length, suffix)
    lengthMutations = lengthMutations + 1
    return prefix .. tostring(tonumber(length) + 1) .. suffix
end, 1)
assert(lengthMutations == 1, "could not locate source length check")
expectIntegrity("source-length", badLength)

io.write("VM rejects unknown opcodes, invalid HALT flow, payload tampering, and wrong length\n")
