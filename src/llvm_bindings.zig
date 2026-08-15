const c = @cImport({
    @cDefine("__STDC_CONSTANT_MACROS", "");
    @cDefine("__STDC_FORMAT_MACROS", "");
    @cDefine("__STDC_LIMIT_MACROS", "");
    @cInclude("llvm-c/Core.h");
});

const Error = error{FailedAlloc};

pub const core = struct {
    pub fn shutdown() void {
        c.LLVMShutdown();
    }

    pub const Context = opaque {
        const Self = @This();

        pub fn create() !*Self {
            const ptr = c.LLVMContextCreate();
            if (ptr == null) return Error.FailedAlloc;
            return @ptrCast(ptr);
        }

        pub fn dispose(self: *Self) void {
            c.LLVMContextDispose(@ptrCast(self));
        }

        pub fn createModuleWithName(
            self: *Self,
            name: [*:0]const u8,
        ) !*Module {
            const ptr = c.LLVMModuleCreateWithNameInContext(
                name,
                @ptrCast(self),
            );
            if (ptr == null) return Error.FailedAlloc;
            return @ptrCast(ptr);
        }

        pub fn getDouble(self: *Self) *Type {
            const ptr = c.LLVMDoubleTypeInContext(@ptrCast(self));
            if (ptr == null) unreachable;
            return @ptrCast(ptr);
        }

        pub fn appendBasicBlock(
            self: *Self,
            func: *Value,
            name: [*:0]const u8,
        ) *Block {
            const ptr = c.LLVMAppendBasicBlockInContext(
                @ptrCast(self),
                @ptrCast(func),
                name,
            );
            if (ptr == null) unreachable;
            return @ptrCast(ptr);
        }

        pub fn createBuilder(self: *Self) !*Builder {
            const ptr = c.LLVMCreateBuilderInContext(@ptrCast(self));
            if (ptr == null) return Error.FailedAlloc;
            return @ptrCast(ptr);
        }
    };

    pub const Module = opaque {
        const Self = @This();

        pub fn dispose(self: *Self) void {
            c.LLVMDisposeModule(@ptrCast(self));
        }

        pub fn dump(self: *Self) void {
            c.LLVMDumpModule(@ptrCast(self));
        }

        pub fn addFunction(
            self: *Self,
            name: [*:0]const u8,
            proto: *Type,
        ) *Value {
            const ptr = c.LLVMAddFunction(
                @ptrCast(self),
                name,
                @ptrCast(proto),
            );
            if (ptr == null) unreachable;
            return @ptrCast(ptr);
        }
    };

    pub const Block = opaque {};

    pub const Builder = opaque {
        const Self = @This();

        pub fn dispose(self: *Self) void {
            c.LLVMDisposeBuilder(@ptrCast(self));
        }

        pub fn positionAtEnd(self: *Self, block: *Block) void {
            c.LLVMPositionBuilderAtEnd(@ptrCast(self), @ptrCast(block));
        }

        pub fn fadd(
            self: *Self,
            lhs: *Value,
            rhs: *Value,
            name: [*:0]const u8,
        ) *Value {
            const ptr = c.LLVMBuildFAdd(
                @ptrCast(self),
                @ptrCast(lhs),
                @ptrCast(rhs),
                name,
            );
            if (ptr == null) unreachable;
            return @ptrCast(ptr);
        }

        pub fn fsub(
            self: *Self,
            lhs: *Value,
            rhs: *Value,
            name: [*:0]const u8,
        ) *Value {
            const ptr = c.LLVMBuildFSub(
                @ptrCast(self),
                @ptrCast(lhs),
                @ptrCast(rhs),
                name,
            );
            if (ptr == null) unreachable;
            return @ptrCast(ptr);
        }

        pub fn fmul(
            self: *Self,
            lhs: *Value,
            rhs: *Value,
            name: [*:0]const u8,
        ) *Value {
            const ptr = c.LLVMBuildFMul(
                @ptrCast(self),
                @ptrCast(lhs),
                @ptrCast(rhs),
                name,
            );
            if (ptr == null) unreachable;
            return @ptrCast(ptr);
        }

        pub fn ret(self: *Self, value: *Value) *Value {
            const ptr = c.LLVMBuildRet(@ptrCast(self), @ptrCast(value));
            if (ptr == null) unreachable;
            return @ptrCast(ptr);
        }
    };

    pub const Type = opaque {
        const Self = @This();

        pub fn function(
            ret: *Self,
            params: []const *Self,
            is_var_arg: bool,
        ) *Self {
            const ptr = c.LLVMFunctionType(
                @ptrCast(ret),
                @ptrCast(@constCast(params.ptr)),
                @intCast(params.len),
                @intFromBool(is_var_arg),
            );
            if (ptr == null) unreachable;
            return @ptrCast(ptr);
        }
    };

    pub const Value = opaque {
        const Self = @This();

        pub fn getParam(self: *Self, index: usize) *Self {
            const ptr = c.LLVMGetParam(@ptrCast(self), @intCast(index));
            if (ptr == null) unreachable;
            return @ptrCast(ptr);
        }
    };
};
