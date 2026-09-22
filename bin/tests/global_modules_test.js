function assert(condition, message) {
    if (!condition)
        throw new Error(message);
}

assert(typeof Color === "object", "Color must be global");
assert(typeof Image === "function", "Image must be global");
assert(typeof Screen === "object", "Screen must be global");
assert(typeof Timer === "object", "Timer must be global");
assert(typeof Vector === "object", "Vector must be global");
assert(typeof Matrix4 === "object", "Matrix4 must be global");
assert(typeof System === "object", "System must be global");
assert(typeof Thread === "object", "Thread must be global");
assert(typeof Mutex === "object", "Mutex must be global");
assert(typeof IOP === "object", "IOP must be global");

print("global_modules_test passed");
