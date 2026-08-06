const std = @import("std");
const ts = @import("tree-sitter");
extern fn tree_sitter_ga() callconv(.c) *ts.Language;

fn depthFirst(cursor: *ts.TreeCursor) bool {
    if (cursor.gotoFirstChild())
        return true;

    while (!cursor.gotoNextSibling())
        if (!cursor.gotoParent())
            return false;

    return true;
}

// TODO: generate it from the node-types.json
const Sym = enum(u16) {
    anon_sym_PLUS = 1,
    anon_sym_DASH = 2,
    aux_sym_identifier_token1 = 3,
    aux_sym_identifier_token2 = 4,
    sym_source_file = 5,
    sym_expression = 6,
    sym_binary_plus = 7,
    sym_binary_minus = 8,
    sym_geom_product = 9,
    sym_identifier = 10,
};

const Node = struct {
    tag: Tag,
    id: Index,
    data: Data,

    pub const Tag = enum {
        root,
        plus,
        minus,
        geom,
        identifier,
    };

    pub const Index = usize;

    pub const Data = union(enum) {
        none,
        node: Index,
        node_pair: struct { Index, Index },
        identifier_index: usize,
    };
};

pub fn main(init: std.process.Init) !void {
    const io = init.io;
    const allocator = init.gpa;

    var args_it = init.minimal.args.iterateAllocator(allocator) catch |err|
        return std.log.err("Failed to iterate args: {}", .{err});
    if (!args_it.skip())
        return error.WrongArgs;
    const filename = args_it.next() orelse
        return error.WrongArgs;
    std.log.info("File to compile: {s}", .{filename});

    const source = std.Io.Dir.cwd().readFileAlloc(io, filename, allocator, .unlimited) catch |err|
        return std.log.err("Failed to read {s}: {}", .{ filename, err });
    defer allocator.free(source);

    const parser: *ts.Parser = ts.Parser.create();
    defer parser.destroy();

    const language: *ts.Language = tree_sitter_ga();
    parser.setLanguage(language) catch |err|
        return std.log.err("Failed to set parser language: {}", .{err});

    const tree: *ts.Tree = parser.parseString(source, null) orelse
        return std.log.err("Failed to parse source", .{});
    defer tree.destroy();

    var cursor = tree.walk();
    defer cursor.destroy();

    var nodes: std.ArrayList(Node) = .empty;
    defer nodes.deinit(allocator);

    var identifiers: std.StringHashMap(usize) = .init(allocator);
    defer identifiers.deinit();

    var iter = true;
    while (iter) : (iter = depthFirst(&cursor)) {
        const sym: Sym = @enumFromInt(cursor.node().kindId());
        const id: usize = nodes.items.len;

        const new_node = switch (sym) {
            .sym_source_file => Node{
                .tag = .root,
                .id = id,
                .data = .{ .node = id + 1 },
            },
            .sym_identifier => blk: {
                const start = cursor.node().startByte();
                const end = cursor.node().endByte();
                const res = identifiers.getOrPut(source[start..end]) catch |err|
                    return std.log.err("Failed add identifier: {}", .{err});
                if (!res.found_existing) res.value_ptr.* = identifiers.count();
                const idx = res.value_ptr.*;

                break :blk Node{
                    .tag = .identifier,
                    .id = id,
                    .data = .{ .identifier_index = idx },
                };
            },
            .sym_binary_plus => Node{
                .tag = .plus,
                .id = id,
                .data = .{ .node_pair = .{ id + 1, id + 2 } },
            },
            .sym_binary_minus => Node{
                .tag = .minus,
                .id = id,
                .data = .{ .node_pair = .{ id + 1, id + 2 } },
            },
            .sym_geom_product => Node{
                .tag = .geom,
                .id = id,
                .data = .{ .node_pair = .{ id + 1, id + 2 } },
            },
            else => continue,
        };

        nodes.append(allocator, new_node) catch |err|
            return std.log.err("Failed to append node: {}", .{err});
    }

    for (nodes.items) |node| {
        std.log.warn("Node {}", .{node});
    }

    var map_it = identifiers.keyIterator();
    while (map_it.next()) |id_ptr| {
        std.log.err("Identifier {s}", .{id_ptr.*});
    }
}
