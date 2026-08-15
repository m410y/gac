const std = @import("std");
const ts = @import("tree-sitter");
extern fn tree_sitter_ga() callconv(.c) *ts.Language;
const llvm = @import("llvm_bindings.zig");

const ts_symbol_identifiers = enum(u16) {
    anon_sym_function = 1,
    anon_sym_LPAREN = 2,
    anon_sym_COMMA = 3,
    anon_sym_RPAREN = 4,
    anon_sym_end = 5,
    anon_sym_return = 6,
    anon_sym_PLUS = 7,
    anon_sym_DASH = 8,
    aux_sym_identifier_token1 = 9,
    aux_sym_identifier_token2 = 10,
    aux_sym_terminator_token1 = 11,
    anon_sym_SEMI = 12,
    sym_source_file = 13,
    sym_function_def = 14,
    sym_function_decl = 15,
    sym_block = 16,
    sym_statement = 17,
    sym_ret_statement = 18,
    sym_expression = 19,
    sym_binary_plus = 20,
    sym_binary_minus = 21,
    sym_geom_product = 22,
    sym_identifier = 23,
    aux_sym_source_file_repeat1 = 24,
    aux_sym_function_decl_repeat1 = 25,
    aux_sym_block_repeat1 = 26,
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

    const scalar_t = context.getDouble();

    var decls: std.MultiArrayList(Variable) = .empty;
    defer decls.deinit(allocator);

    var args: std.ArrayList(*llvm.core.Value) = .empty;
    defer args.deinit(allocator);

    var buffer: std.ArrayList(u8) = .empty;
    defer buffer.deinit(allocator);

    var namespace: std.StringHashMap(*llvm.core.Value) = .init(allocator);
    defer namespace.deinit();

    var declare: bool = undefined;

    var counter: usize = 0;
    while (true) {
        const node = cursor.node();
        // std.log.debug(
        //     "{d} {s} {s}",
        //     .{ args.items.len, node.kind(), if (visited) "up" else "down" },
        // );
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
            .anon_sym_COMMA,
            .anon_sym_return,
            .anon_sym_PLUS,
            .anon_sym_DASH,
            => visited = !cursor.gotoNextSibling(),

            .anon_sym_RPAREN,
            .anon_sym_end,
            .aux_sym_terminator_token1,
            .anon_sym_SEMI,
            => if (visited) return error.SyntaxError else {
                visited = cursor.gotoParent();
            },

            .sym_source_file => if (visited) break else {
                visited = !cursor.gotoFirstChild();
            },

            .sym_function_def => visited =
                if (visited) cursor.gotoParent() else !cursor.gotoFirstChild(),

            .sym_function_decl => if (visited) {
                const func = decls.get(0);
                const params = decls.slice().subslice(1, decls.len - 1);
                try buffer.resize(allocator, func.name.len + 1);
                @memcpy(buffer.items.ptr, func.name);
                buffer.items[func.name.len] = 0;
                const value = module.addFunction(
                    @ptrCast(buffer.items.ptr),
                    func.ll_type.function(params.items(.ll_type), false),
                );
                try namespace.put(func.name, value);
                for (params.items(.name), 0..) |name, i|
                    try namespace.put(name, value.getParam(i));
                decls.clearRetainingCapacity();
                try args.append(allocator, value);
                visited = !cursor.gotoNextSibling();
            } else {
                declare = true;
                visited = !cursor.gotoFirstChild();
            },

            .sym_block => if (visited) {
                visited = cursor.gotoParent();
            } else {
                const func = args.pop().?;
                const entry = context.appendBasicBlock(func, "");
                builder.positionAtEnd(entry);
                declare = false;
                visited = !cursor.gotoFirstChild();
            },

            .sym_ret_statement => if (visited) {
                const value = args.pop().?;
                _ = builder.ret(value);
                visited = cursor.gotoParent();
            } else {
                visited = !cursor.gotoFirstChild();
            },

            .sym_binary_plus => if (visited) {
                const rhs = args.pop().?;
                const lhs = args.pop().?;
                args.appendAssumeCapacity(builder.fadd(lhs, rhs, ""));
                visited = !cursor.gotoNextSibling();
                if (visited)
                    visited = cursor.gotoParent();
            } else {
                visited = !cursor.gotoFirstChild();
            },

            .sym_binary_minus => if (visited) {
                const rhs = args.pop().?;
                const lhs = args.pop().?;
                args.appendAssumeCapacity(builder.fsub(lhs, rhs, ""));
                visited = !cursor.gotoNextSibling();
                if (visited)
                    visited = cursor.gotoParent();
            } else {
                visited = !cursor.gotoFirstChild();
            },

            .sym_geom_product => if (visited) {
                const rhs = args.pop().?;
                const lhs = args.pop().?;
                args.appendAssumeCapacity(builder.fmul(lhs, rhs, ""));
                visited = !cursor.gotoNextSibling();
                if (visited)
                    visited = cursor.gotoParent();
            } else {
                visited = !cursor.gotoFirstChild();
            },

            .sym_identifier => {
                counter += 1;
                if (counter > 10) return error.WhileTrue;
                const name = nodeName(source, node);
                const entry = try namespace.getOrPut(name);
                if (declare and entry.found_existing)
                    return error.NameTaken
                else if (declare and !entry.found_existing)
                    try decls.append(allocator, .{
                        .name = name,
                        .ll_type = scalar_t,
                    })
                else if (!declare and entry.found_existing) {
                    try args.append(allocator, entry.value_ptr.*);
                } else if (!declare and !entry.found_existing)
                    return error.NameUnknown;
                visited = !cursor.gotoNextSibling();
                if (visited)
                    visited = cursor.gotoParent();
            },

            else => return error.ParsingError,
        }
    }

    module.dump();
}
