// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
namespace PicoUart.Hid;

/// <summary>Own native transfers and defer handle cleanup until every transfer has returned.</summary>
internal sealed class HidStreamLifetime(Action dispose, string? devicePath = null)
{
    private static readonly object CleanupGate = new();
    private static readonly Dictionary<string, HashSet<Task>> PendingDeviceCleanup = new();
    private readonly object gate = new();
    private readonly TaskCompletionSource cleanup = new(TaskCreationOptions.RunContinuationsAsynchronously);
    private int activeTransfers;
    private bool disposeRequested;
    private bool disposeStarted;

    public Task Cleanup => cleanup.Task;

    public static void EnsureDeviceCleanupComplete(string devicePath)
    {
        while (true)
        {
            Task[] pending;
            lock (CleanupGate)
            {
                if (!PendingDeviceCleanup.TryGetValue(devicePath, out var tasks)) return;
                pending = tasks.ToArray();
            }

            var incomplete = pending.Any(task => !task.IsCompleted);
            if (incomplete)
                throw new IOException("previous HID cleanup is still pending; retry before opening this device");
            Task.WhenAll(pending).GetAwaiter().GetResult();

            lock (CleanupGate)
            {
                if (!PendingDeviceCleanup.TryGetValue(devicePath, out var tasks)) continue;
                foreach (var task in pending) tasks.Remove(task);
                if (tasks.Count == 0) PendingDeviceCleanup.Remove(devicePath);
            }
        }
    }

    public void TrackDeviceCleanup(string devicePath)
    {
        var completion = Cleanup;
        if (completion.IsCompletedSuccessfully) return;
        lock (CleanupGate)
        {
            if (!PendingDeviceCleanup.TryGetValue(devicePath, out var tasks))
                PendingDeviceCleanup.Add(devicePath, tasks = []);
            tasks.Add(completion);
        }
        _ = completion.ContinueWith(completed =>
        {
            if (completed.IsFaulted) _ = completed.Exception;
            if (!completed.IsCompletedSuccessfully) return;
            lock (CleanupGate)
            {
                if (!PendingDeviceCleanup.TryGetValue(devicePath, out var tasks)) return;
                tasks.Remove(completion);
                if (tasks.Count == 0) PendingDeviceCleanup.Remove(devicePath);
            }
        }, CancellationToken.None, TaskContinuationOptions.ExecuteSynchronously, TaskScheduler.Default);
    }

    public T Transfer<T>(Func<T> transfer, int timeoutMs)
    {
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(timeoutMs);
        lock (gate)
        {
            ObjectDisposedException.ThrowIf(disposeRequested, this);
            activeTransfers++;
        }

        Task<T> operation;
        try
        {
            operation = Task.Run(() =>
            {
                try { return transfer(); }
                finally { CompleteTransfer(); }
            });
        }
        catch
        {
            CompleteTransfer();
            throw;
        }
        try { return operation.WaitAsync(TimeSpan.FromMilliseconds(timeoutMs)).GetAwaiter().GetResult(); }
        catch (TimeoutException)
        {
            if (devicePath is not null) TrackDeviceCleanup(devicePath);
            throw;
        }
    }

    public void Dispose()
    {
        bool disposeNow;
        lock (gate)
        {
            disposeRequested = true;
            disposeNow = activeTransfers == 0 && !disposeStarted;
            if (disposeNow) disposeStarted = true;
        }
        if (disposeNow) DisposeNative();
    }

    private void CompleteTransfer()
    {
        bool disposeNow;
        lock (gate)
        {
            activeTransfers--;
            disposeNow = disposeRequested && activeTransfers == 0 && !disposeStarted;
            if (disposeNow) disposeStarted = true;
        }
        if (disposeNow) DisposeNative();
    }

    private void DisposeNative()
    {
        try
        {
            dispose();
            cleanup.TrySetResult();
        }
        catch (Exception error)
        {
            cleanup.TrySetException(error);
            throw;
        }
    }
}
