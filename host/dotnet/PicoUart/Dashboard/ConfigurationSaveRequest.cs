using System.Text.Json;

namespace PicoUart.Dashboard;

internal sealed record ConfigurationSaveRequest(uint Profile, bool UnsafeOverclock)
{
    public static ConfigurationSaveRequest Parse(JsonElement body)
    {
        if (body.ValueKind != JsonValueKind.Object || body.EnumerateObject().Count() != 2
            || !body.TryGetProperty("profile", out var profile) || profile.ValueKind != JsonValueKind.Number
            || !profile.TryGetUInt32(out var value) || value > 4
            || !body.TryGetProperty("unsafe_overclock", out var acknowledgement)
            || acknowledgement.ValueKind is not (JsonValueKind.True or JsonValueKind.False))
            throw new ArgumentException("expected profile (integer 0..4) and unsafe_overclock (boolean)");
        var unsafeOverclock = acknowledgement.GetBoolean();
        if (value != 0 && !unsafeOverclock)
            throw new ArgumentException("explicit unsafe overclock acknowledgement is required");
        return new(value, unsafeOverclock);
    }
}