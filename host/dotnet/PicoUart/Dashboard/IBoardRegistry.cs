// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
using PicoUart.Hid;

namespace PicoUart.Dashboard;

public interface IBoardRegistry
{
    BoardList Boards();
    string SnapshotJson(string? boardId);
    BoardControlStatus Action(
        string? boardId, string action, CancellationToken cancellationToken = default);
    BoardControlStatus SaveConfiguration(
        string? boardId, uint profile, bool unsafeOverclock, CancellationToken cancellationToken = default);
    string DiagnosticsJson(string? boardId);
}
