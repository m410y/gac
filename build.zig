const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const gac = b.addExecutable(.{
        .name = "gac",
        .root_module = b.createModule(.{
            .root_source_file = b.path("src/main.zig"),
            .target = target,
            .optimize = optimize,
        }),
    });

    const ts = b.dependency("tree_sitter", .{
        .target = target,
        .optimize = optimize,
    });
    gac.root_module.addImport("tree-sitter", ts.module("tree_sitter"));
    gac.root_module.addCSourceFile(.{ .file = b.path("src/parser.c") });

    gac.root_module.linkSystemLibrary("LLVM", .{});

    b.installArtifact(gac);
}
