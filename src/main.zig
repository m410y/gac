const std = @import("std");
const ts = @import("tree-sitter");
extern fn tree_sitter_ga() callconv(.c) *ts.Language;
const llvm = @import("llvm_bindings.zig");

const ts_symbol_identifiers = enum(u16) {
    anon_sym_function = 1,
    anon_sym_LPAREN = 2,
    anon_sym_COMMA = 3,
    anon_sym_RPAREN = 4,
    anon_sym_DASH_GT = 5,
    anon_sym_end = 6,
    anon_sym_return = 7,
    anon_sym_PLUS = 8,
    anon_sym_DASH = 9,
    aux_sym_identifier_token1 = 10,
    aux_sym_identifier_token2 = 11,
    aux_sym_terminator_token1 = 12,
    anon_sym_SEMI = 13,
    sym_source_file = 14,
    sym_function_def = 15,
    sym_function_decl = 16,
    sym_block = 17,
    sym_statement = 18,
    sym_ret_statement = 19,
    sym_expression = 20,
    sym_binary_plus = 21,
    sym_binary_minus = 22,
    sym_geom_product = 23,
    sym_var_decl = 24,
    sym_typename = 25,
    sym_variable = 26,
    sym_identifier = 27,
    aux_sym_source_file_repeat1 = 28,
    aux_sym_function_decl_repeat1 = 29,
    aux_sym_block_repeat1 = 30,
    _,
};

fn filenameFromArgs(
    args: std.process.Args,
    allocator: std.mem.Allocator,
) ![:0]const u8 {
    var args_it = try args.iterateAllocator(allocator);
    if (!args_it.skip()) unreachable;
    const filename = args_it.next();
    return filename orelse error.WrongArgs;
}

fn treeFromSource(source: []const u8) !*ts.Tree {
    const parser: *ts.Parser = ts.Parser.create();
    defer parser.destroy();

    const language: *ts.Language = tree_sitter_ga();
    try parser.setLanguage(language);

    return parser.parseString(source, null) orelse error.ParsingFailed;
}

fn nodeKind(node: ts.Node) ts_symbol_identifiers {
    return @enumFromInt(node.kindId());
}

fn nodeName(source: []const u8, node: ts.Node) []const u8 {
    return source[node.startByte()..node.endByte()];
}

fn toCStringBuf(
    str: []const u8,
    buf: *std.ArrayList(u8),
    allocator: std.mem.Allocator,
) ![*:0]const u8 {
    try buf.resize(allocator, str.len + 1);
    @memcpy(buf.items.ptr, str);
    buf.items[str.len] = 0;
    return @ptrCast(buf.items.ptr);
}

const Variable = struct {
    name: []const u8,
    ll_type: *llvm.core.Type,
};

pub fn main(init: std.process.Init) !void {
    const io: std.Io = init.io;
    const allocator: std.mem.Allocator = init.gpa;

    const filename = try filenameFromArgs(init.minimal.args, allocator);

    const source = try std.Io.Dir.cwd().readFileAlloc(
        io,
        filename,
        allocator,
        .unlimited,
    );
    defer allocator.free(source);

    ts.setAllocator(allocator);
    defer ts.setAllocator(null);

    const tree: *ts.Tree = try treeFromSource(source);
    defer tree.destroy();

    var cursor = tree.walk();
    defer cursor.destroy();

    var visited: bool = false;

    // There LLVM uses it's own allocators.
    // So, make sure to dispose all it's objects
    // or memory leaks are inevitable!
    defer llvm.core.shutdown();

    const context = try llvm.core.Context.create();
    defer context.dispose();

    const module = try context.createModuleWithName(filename);
    defer module.dispose();

    const builder = try context.createBuilder();
    defer builder.dispose();

    var values: std.ArrayList(*llvm.core.Value) = .empty;
    defer values.deinit(allocator);

    var types: std.ArrayList(*llvm.core.Type) = .empty;
    defer types.deinit(allocator);

    var names: std.ArrayList([]const u8) = .empty;
    defer names.deinit(allocator);

    var buffer: std.ArrayList(u8) = .empty;
    defer buffer.deinit(allocator);

    var namespace: std.StringHashMap(*llvm.core.Value) = .init(allocator);
    defer namespace.deinit();

    var typespace: std.StringHashMap(*llvm.core.Type) = .init(allocator);
    defer typespace.deinit();

    try typespace.put("scalar", context.getDouble());

    while (true) {
        const node = cursor.node();
        std.log.debug(
            "{s} {s}",
            .{ node.kind(), if (visited) "up" else "down" },
        );
        switch (nodeKind(node)) {
            .aux_sym_identifier_token1,
            .aux_sym_identifier_token2,
            .aux_sym_source_file_repeat1,
            .aux_sym_function_decl_repeat1,
            .aux_sym_block_repeat1,
            .sym_statement,
            .sym_expression,
            => unreachable,

            .anon_sym_function,
            .anon_sym_LPAREN,
            .anon_sym_RPAREN,
            .anon_sym_COMMA,
            .anon_sym_return,
            .anon_sym_PLUS,
            .anon_sym_DASH,
            .anon_sym_DASH_GT,
            => visited = !cursor.gotoNextSibling(),

            .anon_sym_end,
            .aux_sym_terminator_token1,
            .anon_sym_SEMI,
            => if (visited) return error.SyntaxError else {
                visited = cursor.gotoParent();
            },

            .sym_source_file => if (visited) break else {
                visited = !cursor.gotoFirstChild();
            },

            .sym_var_decl,
            .sym_function_def,
            => if (visited) {
                visited = !cursor.gotoNextSibling();
                if (visited)
                    visited = cursor.gotoParent();
            } else {
                visited = !cursor.gotoFirstChild();
            },

            .sym_function_decl => if (visited) {
                const ret_type = types.pop().?;
                const param_count = node.namedChildCount() - 2;
                const name = names.items[names.items.len - param_count - 1];
                std.log.debug("{d} {d}, {d}", .{ param_count, names.items.len, types.items.len });
                const func = module.addFunction(
                    try toCStringBuf(name, &buffer, allocator),
                    ret_type.function(
                        types.items[types.items.len - param_count .. types.items.len],
                        false,
                    ),
                );
                try values.append(allocator, func);
                for (0..param_count) |i|
                    try namespace.put(names.pop().?, func.getParam(param_count - i - 1));
                _ = names.pop();
                try namespace.put(name, func);
                visited = !cursor.gotoNextSibling();
            } else {
                visited = !cursor.gotoFirstChild();
            },

            .sym_block => if (visited) {
                visited = cursor.gotoParent();
            } else {
                const func = values.pop().?;
                const entry = context.appendBasicBlock(func, "");
                builder.positionAtEnd(entry);
                visited = !cursor.gotoFirstChild();
            },

            .sym_ret_statement => if (visited) {
                const value = values.pop().?;
                _ = builder.ret(value);
                visited = cursor.gotoParent();
            } else {
                visited = !cursor.gotoFirstChild();
            },

            .sym_binary_plus => if (visited) {
                const rhs = values.pop().?;
                const lhs = values.pop().?;
                values.appendAssumeCapacity(builder.fadd(lhs, rhs, ""));
                visited = !cursor.gotoNextSibling();
                if (visited)
                    visited = cursor.gotoParent();
            } else {
                visited = !cursor.gotoFirstChild();
            },

            .sym_binary_minus => if (visited) {
                const rhs = values.pop().?;
                const lhs = values.pop().?;
                values.appendAssumeCapacity(builder.fsub(lhs, rhs, ""));
                visited = !cursor.gotoNextSibling();
                if (visited)
                    visited = cursor.gotoParent();
            } else {
                visited = !cursor.gotoFirstChild();
            },

            .sym_geom_product => if (visited) {
                const rhs = values.pop().?;
                const lhs = values.pop().?;
                values.appendAssumeCapacity(builder.fmul(lhs, rhs, ""));
                visited = !cursor.gotoNextSibling();
                if (visited)
                    visited = cursor.gotoParent();
            } else {
                visited = !cursor.gotoFirstChild();
            },

            .sym_typename => if (visited) {
                try types.append(allocator, typespace.get(names.pop().?) orelse
                    return error.UnknownType);
                visited = !cursor.gotoNextSibling();
                if (visited)
                    visited = cursor.gotoParent();
            } else {
                visited = !cursor.gotoFirstChild();
            },

            .sym_variable => if (visited) {
                try values.append(allocator, namespace.get(names.pop().?) orelse
                    return error.UndefinedVariable);
                visited = !cursor.gotoNextSibling();
                if (visited)
                    visited = cursor.gotoParent();
            } else {
                visited = !cursor.gotoFirstChild();
            },

            .sym_identifier => if (visited) {
                visited = cursor.gotoParent();
            } else {
                try names.append(allocator, source[node.startByte()..node.endByte()]);
                visited = !cursor.gotoNextSibling();
                if (visited)
                    visited = cursor.gotoParent();
            },

            else => return error.ParsingError,
        }
    }

    module.dump();
}
