using Xunit;
using Bunit;
using NukCPGDrop.Ui.Components;

namespace NukCPGDrop.Ui.Tests.Components;

public class LogoDisplayTests : TestContext
{
    [Fact]
    public void LogoDisplay_RendersFallbackText()
    {
        var cut = RenderComponent<LogoDisplay>();
        Assert.Contains("NUKCPGDROP", cut.Markup);
    }
}
