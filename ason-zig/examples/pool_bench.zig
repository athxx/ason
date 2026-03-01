const std = @import("std");
const ason = @import("ason");
const Timer = std.time.Timer;
const Allocator = std.mem.Allocator;
const print = std.debug.print;

// ===========================================================================
// Struct
// ===========================================================================

const User = struct {
    id: i64,
    name: []const u8,
    email: []const u8,
    age: i64,
    score: f64,
    active: bool,
};

// ===========================================================================
// Data generator
// ===========================================================================

fn generateUsers(alloc: Allocator, n: usize) ![]User {
    const names = [_][]const u8{ "Alice", "Bob", "Carol", "David", "Eve", "Frank", "Grace", "Hank" };
    const emails = [_][]const u8{ "alice@example.com", "bob@example.com", "carol@example.com", "david@example.com", "eve@example.com", "frank@example.com", "grace@example.com", "hank@example.com" };
    const users = try alloc.alloc(User, n);
    for (users, 0..) |*u, i| {
        u.* = User{
            .id = @intCast(i),
            .name = names[i % names.len],
            .email = emails[i % emails.len],
            .age = @as(i64, @intCast(25 + i % 40)),
            .score = 50.0 + @as(f64, @floatFromInt(i % 50)) + 0.5,
            .active = i % 3 != 0,
        };
    }
    return users;
}

// ===========================================================================
// Benchmark result
// ===========================================================================

const BenchResult = struct {
    scenario: []const u8,
    input_bytes: usize,
    old_pool_initial: usize,
    new_pool_initial: usize,
    old_blocks: usize,
    new_blocks: usize,
    old_ns_per_op: f64,
    new_ns_per_op: f64,
    old_total_alloc: usize,
    new_total_alloc: usize,
};

fn poolTotalAlloc(pool: *const ason.PoolAllocator) usize {
    var total: usize = 0;
    for (pool.blocks[0..pool.block_count]) |blk| {
        total += blk.len;
    }
    return total;
}

fn printHeader() void {
    print("{s:<38} {s:>9} | {s:>10} {s:>5} {s:>8} | {s:>10} {s:>5} {s:>8} | {s:>7} {s:>7}\n", .{
        "Scenario",      "Input",
        "Old Pool",      "Blks", "ns/op",
        "New Pool",      "Blks", "ns/op",
        "Speedup",       "MemSav",
    });
    print("{s:<38} {s:>9} | {s:>10} {s:>5} {s:>8} | {s:>10} {s:>5} {s:>8} | {s:>7} {s:>7}\n", .{
        "--------------------------------------", "---------",
        "----------",                             "-----", "--------",
        "----------",                             "-----", "--------",
        "-------",                                "-------",
    });
}

fn printResult(r: *const BenchResult) void {
    const speedup = r.old_ns_per_op / r.new_ns_per_op;
    const mem_save = if (r.old_total_alloc > 0)
        (1.0 - @as(f64, @floatFromInt(r.new_total_alloc)) / @as(f64, @floatFromInt(r.old_total_alloc))) * 100.0
    else
        0.0;
    print("{s:<38} {d:>7}B | {d:>8}B {d:>5} {d:>7.0}ns | {d:>8}B {d:>5} {d:>7.0}ns | {d:>6.2}x {d:>5.0}%\n", .{
        r.scenario,
        r.input_bytes,
        r.old_pool_initial,
        r.old_blocks,
        r.old_ns_per_op,
        r.new_pool_initial,
        r.new_blocks,
        r.new_ns_per_op,
        speedup,
        mem_save,
    });
}

// ===========================================================================
// Benchmarks
// ===========================================================================

const OLD_POOL_SIZE: usize = 128 * 1024;
const WARMUP: usize = 20;
const ITERS: usize = 2000;

// Backing allocator for pools (page_allocator is fine; the pool itself minimizes syscalls)
const backing_alloc: Allocator = std.heap.page_allocator;

fn benchDecodeText(comptime T: type, label: []const u8, input: []const u8) BenchResult {
    var r = BenchResult{
        .scenario = label,
        .input_bytes = input.len,
        .old_pool_initial = OLD_POOL_SIZE,
        .new_pool_initial = 0,
        .old_blocks = 0,
        .new_blocks = 0,
        .old_ns_per_op = 0,
        .new_ns_per_op = 0,
        .old_total_alloc = 0,
        .new_total_alloc = 0,
    };

    // Compute smart estimate
    const new_est = if (comptime ason.isStructSlice(T)) blk: {
        const Child = @typeInfo(T).pointer.child;
        const tc = ason.countTuples(input);
        const e = tc * @sizeOf(Child) + input.len;
        break :blk if (e < ason.PoolAllocator.MIN_SIZE) ason.PoolAllocator.MIN_SIZE else e;
    } else if (input.len < ason.PoolAllocator.MIN_SIZE) ason.PoolAllocator.MIN_SIZE else input.len;
    r.new_pool_initial = new_est;

    // --- OLD (128KB) ---
    // Warmup + capture
    for (0..WARMUP) |i| {
        var pool = ason.PoolAllocator.initSized(backing_alloc, OLD_POOL_SIZE) catch unreachable;
        defer pool.deinit();
        _ = ason.decode(T, input, pool.allocator()) catch unreachable;
        if (i == WARMUP - 1) {
            r.old_blocks = pool.block_count;
            r.old_total_alloc = poolTotalAlloc(&pool);
        }
    }
    // Timed
    var timer = Timer.start() catch unreachable;
    for (0..ITERS) |_| {
        var pool = ason.PoolAllocator.initSized(backing_alloc, OLD_POOL_SIZE) catch unreachable;
        defer pool.deinit();
        _ = ason.decode(T, input, pool.allocator()) catch unreachable;
    }
    r.old_ns_per_op = @as(f64, @floatFromInt(timer.read())) / @as(f64, ITERS);

    // --- NEW (smart-sized) ---
    for (0..WARMUP) |i| {
        var pool = ason.PoolAllocator.initSized(backing_alloc, new_est) catch unreachable;
        defer pool.deinit();
        _ = ason.decode(T, input, pool.allocator()) catch unreachable;
        if (i == WARMUP - 1) {
            r.new_blocks = pool.block_count;
            r.new_total_alloc = poolTotalAlloc(&pool);
        }
    }
    timer = Timer.start() catch unreachable;
    for (0..ITERS) |_| {
        var pool = ason.PoolAllocator.initSized(backing_alloc, new_est) catch unreachable;
        defer pool.deinit();
        _ = ason.decode(T, input, pool.allocator()) catch unreachable;
    }
    r.new_ns_per_op = @as(f64, @floatFromInt(timer.read())) / @as(f64, ITERS);

    return r;
}

fn benchDecodeBinary(comptime T: type, label: []const u8, data: []const u8) BenchResult {
    var r = BenchResult{
        .scenario = label,
        .input_bytes = data.len,
        .old_pool_initial = OLD_POOL_SIZE,
        .new_pool_initial = 0,
        .old_blocks = 0,
        .new_blocks = 0,
        .old_ns_per_op = 0,
        .new_ns_per_op = 0,
        .old_total_alloc = 0,
        .new_total_alloc = 0,
    };

    const new_est = if (comptime ason.isStructSlice(T)) blk: {
        const Child = @typeInfo(T).pointer.child;
        const count: u32 = if (data.len >= 4) @bitCast(data[0..4].*) else 0;
        const e = @as(usize, count) * @sizeOf(Child) + data.len;
        break :blk if (e < ason.PoolAllocator.MIN_SIZE) ason.PoolAllocator.MIN_SIZE else e;
    } else if (data.len < ason.PoolAllocator.MIN_SIZE) ason.PoolAllocator.MIN_SIZE else data.len;
    r.new_pool_initial = new_est;

    // --- OLD ---
    for (0..WARMUP) |i| {
        var pool = ason.PoolAllocator.initSized(backing_alloc, OLD_POOL_SIZE) catch unreachable;
        defer pool.deinit();
        _ = ason.decodeBinary(T, data, pool.allocator()) catch unreachable;
        if (i == WARMUP - 1) {
            r.old_blocks = pool.block_count;
            r.old_total_alloc = poolTotalAlloc(&pool);
        }
    }
    var timer = Timer.start() catch unreachable;
    for (0..ITERS) |_| {
        var pool = ason.PoolAllocator.initSized(backing_alloc, OLD_POOL_SIZE) catch unreachable;
        defer pool.deinit();
        _ = ason.decodeBinary(T, data, pool.allocator()) catch unreachable;
    }
    r.old_ns_per_op = @as(f64, @floatFromInt(timer.read())) / @as(f64, ITERS);

    // --- NEW ---
    for (0..WARMUP) |i| {
        var pool = ason.PoolAllocator.initSized(backing_alloc, new_est) catch unreachable;
        defer pool.deinit();
        _ = ason.decodeBinary(T, data, pool.allocator()) catch unreachable;
        if (i == WARMUP - 1) {
            r.new_blocks = pool.block_count;
            r.new_total_alloc = poolTotalAlloc(&pool);
        }
    }
    timer = Timer.start() catch unreachable;
    for (0..ITERS) |_| {
        var pool = ason.PoolAllocator.initSized(backing_alloc, new_est) catch unreachable;
        defer pool.deinit();
        _ = ason.decodeBinary(T, data, pool.allocator()) catch unreachable;
    }
    r.new_ns_per_op = @as(f64, @floatFromInt(timer.read())) / @as(f64, ITERS);

    return r;
}

// ===========================================================================
// Main
// ===========================================================================

pub fn main() !void {
    const alloc = std.heap.page_allocator;

    print("=== Pool Allocator Benchmark: Fixed 128KB vs Smart-Sized (Zig) ===\n", .{});
    print("    Iterations per scenario: {d} (warmup: {d})\n\n", .{ ITERS, WARMUP });

    // Generate data
    const users3 = try generateUsers(alloc, 3);
    const users100 = try generateUsers(alloc, 100);
    const users10k = try generateUsers(alloc, 10000);

    const one_user = User{
        .id = 42,
        .name = "Alice",
        .email = "alice@example.com",
        .age = 30,
        .score = 95.5,
        .active = true,
    };

    // Encode text payloads
    const txt_single = try ason.encode(User, one_user, alloc);
    const txt_vec3 = try ason.encode([]const User, users3, alloc);
    const txt_vec100 = try ason.encode([]const User, users100, alloc);
    const txt_vec10k = try ason.encode([]const User, users10k, alloc);

    // Encode binary payloads
    const bin_single = try ason.encodeBinary(User, one_user, alloc);
    const bin_vec100 = try ason.encodeBinary([]const User, users100, alloc);
    const bin_vec10k = try ason.encodeBinary([]const User, users10k, alloc);

    print("Payload sizes:\n", .{});
    print("  Text single:   {d:>6} bytes\n", .{txt_single.len});
    print("  Text vec x3:   {d:>6} bytes\n", .{txt_vec3.len});
    print("  Text vec x100: {d:>6} bytes\n", .{txt_vec100.len});
    print("  Text vec x10k: {d:>6} bytes\n", .{txt_vec10k.len});
    print("  Bin single:    {d:>6} bytes\n", .{bin_single.len});
    print("  Bin vec x100:  {d:>6} bytes\n", .{bin_vec100.len});
    print("  Bin vec x10k:  {d:>6} bytes\n\n", .{bin_vec10k.len});

    printHeader();

    var results: [7]BenchResult = undefined;
    results[0] = benchDecodeText(User, "Text single struct", txt_single);
    results[1] = benchDecodeText([]User, "Text vec x3 (tiny)", txt_vec3);
    results[2] = benchDecodeText([]User, "Text vec x100 (medium)", txt_vec100);
    results[3] = benchDecodeText([]User, "Text vec x10000 (large)", txt_vec10k);
    results[4] = benchDecodeBinary(User, "Binary single struct", bin_single);
    results[5] = benchDecodeBinary([]User, "Binary vec x100 (medium)", bin_vec100);
    results[6] = benchDecodeBinary([]User, "Binary vec x10000 (large)", bin_vec10k);

    for (&results) |*r| printResult(r);

    print("\nSummary:\n", .{});
    for (&results) |*r| {
        const speedup = r.old_ns_per_op / r.new_ns_per_op;
        const mem_save = if (r.old_total_alloc > 0)
            (1.0 - @as(f64, @floatFromInt(r.new_total_alloc)) / @as(f64, @floatFromInt(r.old_total_alloc))) * 100.0
        else
            0.0;
        print("  {s:<38}  speedup: {d:.2}x  mem saved: {d:.0}%  blocks: {d}->{d}\n", .{
            r.scenario,
            speedup,
            mem_save,
            r.old_blocks,
            r.new_blocks,
        });
    }
}
