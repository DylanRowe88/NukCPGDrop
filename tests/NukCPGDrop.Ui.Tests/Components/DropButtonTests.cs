using Xunit;
using Bunit;
using NukCPGDrop.Ui.Components;

namespace NukCPGDrop.Ui.Tests.Components;

public class DropButtonTests : TestContext
{
    [Fact]
    public void DropButton_RendersLabel()
    {
        var cut = RenderComponent<DropButton>(p => p
            .Add(b => b.Label, "DROP ALL")
            .Add(b => b.Subtitle, "Start sequence")
        );

        cut.MarkupMatches(m => m.HasChild("button")
            .And(b => b.HasAttribute("class").HasValue("drop-btn"))
        );
        Assert.Contains("DROP ALL", cut.Markup);
    }

    [Theory]
    [InlineData(true)]
    [InlineData(false)]
    public void DropButton_DisabledState(bool disabled)
    {
        var cut = RenderComponent<DropButton>(p => p
            .Add(b => b.Label, "DROP")
            .Add(b => b.Disabled, disabled)
        );

        var button = cut.Find("button");
        Assert.Equal(disabled, button.IsDisabled);
    }

    [Fact]
    public void DropButton_FiresOnClick()
    {
        var clicked = false;
        var cut = RenderComponent<DropButton>(p => p
            .Add(b => b.Label, "DROP")
            .Add(b => b.OnClick, EventCallback.Factory.Create(this, () => clicked = true))
        );

        cut.Find("button").Click();
        Assert.True(clicked);
    }

    [Fact]
    public void DropButton_DoesNotFireWhenDisabled()
    {
        var clicked = false;
        var cut = RenderComponent<DropButton>(p => p
            .Add(b => b.Label, "DROP")
            .Add(b => b.Disabled, true)
            .Add(b => b.OnClick, EventCallback.Factory.Create(this, () => clicked = true))
        );

        cut.Find("button").Click();
        Assert.False(clicked);
    }
}
