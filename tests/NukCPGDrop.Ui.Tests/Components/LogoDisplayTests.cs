using Xunit;
using Bunit;
using NukCPGDrop.Ui.Components;

namespace NukCPGDrop.Ui.Tests.Components;

public class LogoDisplayTests : TestContext
{
    [Fact]
    public void LogoDisplay_RendersPrefix()
    {
        var cut = RenderComponent<LogoDisplay>();
        Assert.Contains("Dropping", cut.Markup);
    }

    [Fact]
    public void LogoDisplay_RendersRestBar()
    {
        var cut = RenderComponent<LogoDisplay>();
        Assert.Contains("Rest when you're dead", cut.Markup);
    }
}
