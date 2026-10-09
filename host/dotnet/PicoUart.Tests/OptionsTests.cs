using System.Globalization;
using Xunit;

namespace PicoUart.Tests;

[Trait("Category", "Unit")]
public sealed class OptionsTests
{
    [Fact]
    public void NoArgumentsSelectHelpAndDefaultSettings()
    {
        Assert.Equal(new Options("help", null, null, 5000, null), Options.Parse([]));
    }

    [Theory]
    [InlineData("help")]
    [InlineData("--help")]
    [InlineData("-h")]
    [InlineData("status")]
    [InlineData("monitor")]
    [InlineData("overruns")]
    [InlineData("hardware")]
    [InlineData("temperature")]
    [InlineData("version")]
    [InlineData("toggle-led")]
    [InlineData("reset")]
    [InlineData("web")]
    public void EverySupportedCommandIsAccepted(string command)
    {
        Assert.Equal(command, Options.Parse([command]).Command);
    }

    [Fact]
    public void SelectorsAndJsonFlagWorkBeforeOrAfterTheCommand()
    {
        Assert.Equal(new Options("status", "board-1", null, 5000, null),
            Options.Parse(["--json", "--serial", "board-1", "status"]));
        Assert.Equal(new Options("status", null, "/dev/hidraw0", 5000, null),
            Options.Parse(["status", "--device-path", "/dev/hidraw0", "--json"]));
    }

    [Theory]
    [InlineData("1", 1)]
    [InlineData("5001", 5001)]
    [InlineData("65535", 65535)]
    public void PortsAcceptBothBoundaries(string input, int expected)
    {
        Assert.Equal(expected, Options.Parse(["web", "--port", input]).Port);
    }

    [Theory]
    [InlineData("0")]
    [InlineData("-1")]
    [InlineData("65536")]
    [InlineData("2147483648")]
    [InlineData("1.5")]
    [InlineData("abc")]
    public void PortsRejectInvalidOrOutOfRangeValues(string input)
    {
        Assert.Throws<ArgumentException>(() => Options.Parse(["web", "--port", input]));
    }

    [Theory]
    [InlineData("0.01", 0.01)]
    [InlineData("10", 10)]
    [InlineData("1e2", 100)]
    [InlineData("4294967.295", 4294967.295)]
    public void DurationsAcceptPositiveFiniteValuesWithinTheTimerRange(string input, double expected)
    {
        Assert.Equal(expected, Options.Parse(["monitor", "--duration", input]).Duration);
    }

    [Theory]
    [InlineData("0")]
    [InlineData("-1")]
    [InlineData("NaN")]
    [InlineData("Infinity")]
    [InlineData("-Infinity")]
    [InlineData("1e300")]
    [InlineData("4294968")]
    [InlineData("abc")]
    public void DurationsRejectInvalidValues(string input)
    {
        Assert.Throws<ArgumentException>(() => Options.Parse(["monitor", "--duration", input]));
    }

    [Fact]
    public void DurationParsingDoesNotDependOnTheHostCulture()
    {
        var previous = CultureInfo.CurrentCulture;
        try
        {
            CultureInfo.CurrentCulture = CultureInfo.GetCultureInfo("fr-FR");
            Assert.Equal(1.5, Options.Parse(["monitor", "--duration", "1.5"]).Duration);
        }
        finally
        {
            CultureInfo.CurrentCulture = previous;
        }
    }

    [Theory]
    [InlineData("--serial")]
    [InlineData("--device-path")]
    [InlineData("--port")]
    [InlineData("--duration")]
    public void OptionsRequireTheirValue(string option)
    {
        var error = Assert.Throws<ArgumentException>(() => Options.Parse(["status", option]));

        Assert.Equal($"missing value for {option}", error.Message);
    }

    [Fact]
    public void DeviceSelectorsAreMutuallyExclusiveBeforeOpeningHardware()
    {
        Assert.Throws<ArgumentException>(() => Options.Parse(
            ["status", "--serial", "board-1", "--device-path", "/dev/hidraw0"]));
        Assert.Throws<ArgumentException>(() => new PicoUartHid("board-1", "/dev/hidraw0"));
    }

    [Theory]
    [InlineData("unknown")]
    [InlineData("--unknown")]
    public void UnknownCommandsAreRejected(string command)
    {
        Assert.Throws<ArgumentException>(() => Options.Parse([command]));
    }

    [Theory]
    [InlineData("extra")]
    [InlineData("--unknown")]
    [InlineData("version")]
    public void UnexpectedArgumentsAfterACommandAreRejected(string argument)
    {
        Assert.Throws<ArgumentException>(() => Options.Parse(["status", argument]));
    }
}