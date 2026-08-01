const std = @import("std");

fn parseFilename(allocator: std.mem.Allocator, args: std.process.Args) ![:0]const u8 {
    var args_it = try args.iterateAllocator(allocator);
    if (!args_it.skip()) return error.WrongArgs;
    return args_it.next() orelse return error.WrongArgs;
}

pub fn main(init: std.process.Init) !void {
    var stdout = std.Io.File.stdout().writerStreaming(init.io, "");
    const filename = try parseFilename(init.gpa, init.minimal.args);
    try stdout.interface.print("File to compile: {s}\n", .{filename});
    const file = std.Io.Dir.cwd().openFile(
        init.io,
        filename,
        .{ .mode = .read_only },
    ) catch |err| {
        std.debug.print("Failed to open {s}: {}\n", .{ filename, err });
        return;
    };
    file.close(init.io);
    return;
}
